/*
Copyright 2025 General Atomics

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

// libfdpio v2.0: POSIX-style I/O over native POSIX (local) and
// XrdCl::File (URL). No XrdPosix in the chain — the destructor race in
// XrdPosixObject::Shutdown that aborted ~50% of pelican-using processes
// at exit is gone because libXrdPosixPreload is never loaded.

#include <fdpio.h>

#include <XrdCl/XrdClFile.hh>
#include <XrdCl/XrdClFileSystem.hh>
#include <XrdCl/XrdClURL.hh>
#include <XrdCl/XrdClXRootDResponses.hh>

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Synthetic fds for URL-backed handles. Real kernel fds are
// effectively always < 2^20 even under heavy load; this gap keeps
// the two namespaces fully disjoint so fdpio_read/write/lseek/close
// can dispatch by fd value alone.
constexpr int SYNTH_FD_BASE = 1 << 20;

struct FdEntry {
    std::unique_ptr<XrdCl::File> file;
    uint64_t pos = 0;   // libfdpio tracks lseek position; XrdCl is
                        // offset-based and has no kernel cursor.
    uint64_t size = 0;  // Cached at open via XrdCl::File::Stat. Used by
                        // fdpio_read to short-circuit reads past EOF
                        // (XrdCl returns a non-OK status for those, but
                        // POSIX read() must return 0 at EOF, not -1).
    bool size_known = false;
};

std::mutex                          g_table_mu;
std::unordered_map<int, FdEntry>    g_table;
int                                 g_next_fd = SYNTH_FD_BASE;

thread_local char                   g_errbuf[256];

// URL schemes we recognize, populated by fdpio_init from
// FDPIO_PROTOCOLS env or a default list.
std::vector<std::string>            g_protocols;

void set_error(const char *msg) {
    std::strncpy(g_errbuf, msg, sizeof(g_errbuf) - 1);
    g_errbuf[sizeof(g_errbuf) - 1] = '\0';
}

void set_error_status(const XrdCl::XRootDStatus &st) {
    set_error(st.ToString().c_str());
}

void set_error_errno() {
    set_error(std::strerror(errno));
}

// Map XRootD error codes to errno. Anything we don't have a clean
// mapping for becomes EIO so callers can still fail predictably.
int errno_from_status(const XrdCl::XRootDStatus &st) {
    using namespace XrdCl;
    switch (st.code) {
        case errNotFound:         return ENOENT;
        case errAuthFailed:       return EACCES;
        case errOperationExpired: return ETIMEDOUT;
        case errInvalidArgs:      return EINVAL;
        case errSocketError:      return ECONNRESET;
        case errNotSupported:     return ENOTSUP;
        default:                  return EIO;
    }
}

void parse_protocols(const char *raw) {
    g_protocols.clear();
    std::string s = raw ? raw : "";
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(',', start);
        if (end == std::string::npos) end = s.size();
        std::string proto = s.substr(start, end - start);
        if (!proto.empty()) g_protocols.push_back(std::move(proto));
        start = end + 1;
    }
}

// Forward declaration so ensure_initialized() can call fdpio_init(),
// which is defined in the extern "C" block below.
extern "C" int fdpio_init(void);

// Lazy-init bookkeeping. Each public entry point that needs XrdCl state
// (fdpio_open, fdpio_stat) calls ensure_initialized() before doing work.
// Pre-2.0.2 callers had to call fdpio_init() themselves; that contract is
// preserved (still callable, still idempotent), but no longer required.
std::atomic_bool g_initialized{false};
std::mutex      g_init_mutex;
std::once_flag  g_atfork_once;

void atfork_child() {
    // After fork, the parent's XrdCl plugin manager + sockets are unusable
    // in the child. Reset so the next URL op re-initializes locally.
    g_initialized.store(false, std::memory_order_relaxed);
}

int ensure_initialized() {
    if (g_initialized.load(std::memory_order_acquire)) return 0;
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (g_initialized.load(std::memory_order_relaxed)) return 0;

    if (fdpio_init() != 0) return -1;        // fdpio_init() populates g_errbuf

    std::call_once(g_atfork_once, [] {
        pthread_atfork(nullptr, nullptr, atfork_child);
    });
    g_initialized.store(true, std::memory_order_release);
    return 0;
}

}  // namespace

extern "C" {

int fdpio_init(void) {
    // Protocol list (URL schemes routed through XrdCl).
    const char *protos = std::getenv("FDPIO_PROTOCOLS");
    parse_protocols(protos ? protos
                            : "pelican,http,https,xroot,xroots,root,roots");

    // Resolve auth token: BEARER_TOKEN env wins; else BEARER_TOKEN_FILE;
    // else first line of ~/.fdp/token. Matches v1 behavior.
    if (!std::getenv("BEARER_TOKEN")) {
        const char *token_file = std::getenv("BEARER_TOKEN_FILE");
        std::string token_path;
        if (token_file && *token_file) {
            token_path = token_file;
        } else {
            const char *home = std::getenv("HOME");
            if (home) token_path = std::string(home) + "/.fdp/token";
        }
        if (!token_path.empty()) {
            std::ifstream ifs(token_path);
            if (ifs.is_open()) {
                std::string token;
                std::getline(ifs, token);
                if (!token.empty()) setenv("BEARER_TOKEN", token.c_str(), 1);
            }
        }
    }

    // Allow plaintext HTTP for xrdcl-pelican (matches v1).
    setenv("XRDCP_ALLOW_HTTP", "true", 0);

    g_errbuf[0] = '\0';
    return 0;
}

void fdpio_cleanup(void) {
    // Close any URL handles still open. Native fds are the caller's
    // responsibility (we only track the ones we minted).
    std::lock_guard<std::mutex> lk(g_table_mu);
    for (auto &kv : g_table) {
        if (kv.second.file) kv.second.file->Close();
    }
    g_table.clear();
}

int fdpio_is_url(const char *path) {
    if (!path) return 0;
    for (const auto &proto : g_protocols) {
        size_t n = proto.size();
        if (std::strncmp(path, proto.c_str(), n) == 0
            && path[n] == ':' && path[n+1] == '/' && path[n+2] == '/') {
            return 1;
        }
    }
    return 0;
}

void fdpio_set_token(const char *token) {
    if (token) setenv("BEARER_TOKEN", token, 1);
}

const char *fdpio_error_string(void) {
    return g_errbuf;
}

// Stub implementations for the file-ops surface — filled in by later tasks.
// Returning ENOSYS means tests that only exercise init/cleanup/is_url pass
// while remaining tasks are landed.

int fdpio_open(const char *path, int oflag, ...) {
    if (ensure_initialized() != 0) {
        errno = EIO;
        return -1;                  // fdpio_error_string() already populated
    }
    if (!path) { errno = EINVAL; set_error("null path"); return -1; }

    // Local path: native POSIX. Returns a real kernel fd (< SYNTH_FD_BASE).
    if (!fdpio_is_url(path)) {
        mode_t mode = 0;
        if (oflag & O_CREAT) {
            va_list ap;
            va_start(ap, oflag);
            mode = static_cast<mode_t>(va_arg(ap, int));
            va_end(ap);
        }
        int fd = ::open(path, oflag, mode);
        if (fd < 0) set_error_errno();
        return fd;
    }

    // URL path: open via XrdCl::File and return a synthetic fd.
    using XrdCl::OpenFlags;

    // Translate POSIX open flags to XrdCl::OpenFlags. v2 only supports
    // the cases ptdata and mdsplus actually use:
    //   O_RDONLY            -> Read
    //   O_WRONLY|O_CREAT    -> New (or Delete if O_TRUNC)
    //   O_RDWR              -> Update
    OpenFlags::Flags xrd_flags;
    int access = oflag & O_ACCMODE;
    if (access == O_RDONLY) {
        xrd_flags = OpenFlags::Read;
    } else if (access == O_WRONLY) {
        xrd_flags = (oflag & O_TRUNC) ? OpenFlags::Delete : OpenFlags::New;
    } else if (access == O_RDWR) {
        xrd_flags = OpenFlags::Update;
    } else {
        errno = EINVAL;
        set_error("fdpio_open: unsupported access flags");
        return -1;
    }

    auto file = std::make_unique<XrdCl::File>();
    auto st = file->Open(std::string(path), xrd_flags);
    if (!st.IsOK()) {
        set_error_status(st);
        errno = errno_from_status(st);
        return -1;
    }

    // Cache the file size so fdpio_read can detect EOF without making a
    // call that XrdCl::File would reject. Stat(force=false, ...) uses
    // the cached open-time response when available, avoiding an extra
    // network round-trip; if it fails (some servers don't pre-populate
    // StatInfo), size_known stays false and fdpio_read falls back to
    // its legacy behaviour.
    uint64_t size = 0;
    bool size_known = false;
    if ((oflag & O_ACCMODE) == O_RDONLY) {
        XrdCl::StatInfo *info = nullptr;
        auto sst = file->Stat(false, info);
        if (sst.IsOK() && info) {
            size = info->GetSize();
            size_known = true;
        }
        delete info;
    }

    std::lock_guard<std::mutex> lk(g_table_mu);
    int fd = g_next_fd++;
    g_table.emplace(fd, FdEntry{std::move(file), 0, size, size_known});
    return fd;
}
int fdpio_close(int fildes) {
    if (fildes < SYNTH_FD_BASE) {
        int rc = ::close(fildes);
        if (rc < 0) set_error_errno();
        return rc;
    }

    // Move the unique_ptr out of the table while holding the lock,
    // then call Close() / let the destructor run *outside* the lock.
    // XrdCl::File destruction can block (cancel pending callbacks);
    // holding g_table_mu during that would deadlock other callers.
    std::unique_ptr<XrdCl::File> file;
    {
        std::lock_guard<std::mutex> lk(g_table_mu);
        auto it = g_table.find(fildes);
        if (it == g_table.end()) {
            errno = EBADF; set_error("fdpio_close: bad fd"); return -1;
        }
        file = std::move(it->second.file);
        g_table.erase(it);
    }
    auto st = file->Close();
    if (!st.IsOK()) {
        set_error_status(st);
        errno = errno_from_status(st);
        return -1;
    }
    return 0;
}

ssize_t fdpio_read(int fildes, void *buf, size_t nbyte) {
    if (fildes < SYNTH_FD_BASE) {
        ssize_t n = ::read(fildes, buf, nbyte);
        if (n < 0) set_error_errno();
        return n;
    }

    XrdCl::File *file;
    uint64_t off, size;
    bool size_known;
    {
        std::lock_guard<std::mutex> lk(g_table_mu);
        auto it = g_table.find(fildes);
        if (it == g_table.end()) {
            errno = EBADF; set_error("fdpio_read: bad fd"); return -1;
        }
        file = it->second.file.get();
        off  = it->second.pos;
        size = it->second.size;
        size_known = it->second.size_known;
    }

    // POSIX read() returns 0 at EOF, never -1. XrdCl::File::Read past
    // end returns a non-OK status ("Error response: bad address") which
    // we'd otherwise propagate as -1. Short-circuit reads at/past EOF
    // and clamp the request length so the last successful read returns
    // exactly what's left.
    if (size_known) {
        if (off >= size) return 0;
        if (off + nbyte > size) nbyte = static_cast<size_t>(size - off);
    }

    uint32_t got = 0;
    auto st = file->Read(off, static_cast<uint32_t>(nbyte), buf, got);
    if (!st.IsOK()) {
        set_error_status(st);
        errno = errno_from_status(st);
        return -1;
    }

    {
        std::lock_guard<std::mutex> lk(g_table_mu);
        auto it = g_table.find(fildes);
        if (it != g_table.end()) it->second.pos += got;
    }
    return static_cast<ssize_t>(got);
}

ssize_t fdpio_write(int fildes, const void *buf, size_t nbyte) {
    if (fildes < SYNTH_FD_BASE) {
        ssize_t n = ::write(fildes, buf, nbyte);
        if (n < 0) set_error_errno();
        return n;
    }

    XrdCl::File *file;
    uint64_t off;
    {
        std::lock_guard<std::mutex> lk(g_table_mu);
        auto it = g_table.find(fildes);
        if (it == g_table.end()) {
            errno = EBADF; set_error("fdpio_write: bad fd"); return -1;
        }
        file = it->second.file.get();
        off  = it->second.pos;
    }

    auto st = file->Write(off, static_cast<uint32_t>(nbyte), buf);
    if (!st.IsOK()) {
        set_error_status(st);
        errno = errno_from_status(st);
        return -1;
    }

    {
        std::lock_guard<std::mutex> lk(g_table_mu);
        auto it = g_table.find(fildes);
        if (it != g_table.end()) it->second.pos += nbyte;
    }
    return static_cast<ssize_t>(nbyte);
}
off_t fdpio_lseek(int fildes, off_t offset, int whence) {
    if (fildes < SYNTH_FD_BASE) {
        off_t r = ::lseek(fildes, offset, whence);
        if (r == (off_t)-1) set_error_errno();
        return r;
    }

    std::lock_guard<std::mutex> lk(g_table_mu);
    auto it = g_table.find(fildes);
    if (it == g_table.end()) {
        errno = EBADF; set_error("fdpio_lseek: bad fd"); return -1;
    }
    FdEntry &entry = it->second;
    uint64_t new_pos = entry.pos;

    switch (whence) {
        case SEEK_SET:
            if (offset < 0) { errno = EINVAL; return -1; }
            new_pos = static_cast<uint64_t>(offset);
            break;
        case SEEK_CUR: {
            int64_t signed_pos = static_cast<int64_t>(entry.pos) + offset;
            if (signed_pos < 0) { errno = EINVAL; return -1; }
            new_pos = static_cast<uint64_t>(signed_pos);
            break;
        }
        case SEEK_END: {
            // Issue Stat to learn the size; do it without releasing the
            // lock since Stat is fast (cached XrdCl response) and lseek
            // is rarely called from many threads at once.
            XrdCl::StatInfo *info = nullptr;
            auto st = entry.file->Stat(false, info);
            if (!st.IsOK()) {
                delete info;
                set_error_status(st);
                errno = errno_from_status(st);
                return -1;
            }
            int64_t signed_pos =
                static_cast<int64_t>(info->GetSize()) + offset;
            delete info;
            if (signed_pos < 0) { errno = EINVAL; return -1; }
            new_pos = static_cast<uint64_t>(signed_pos);
            break;
        }
        default:
            errno = EINVAL;
            set_error("fdpio_lseek: bad whence");
            return -1;
    }

    entry.pos = new_pos;
    return static_cast<off_t>(new_pos);
}

int fdpio_stat(const char *path, struct stat *out) {
    if (ensure_initialized() != 0) {
        errno = EIO;
        return -1;
    }
    if (!path || !out) { errno = EINVAL; return -1; }

    if (!fdpio_is_url(path)) {
        int rc = ::stat(path, out);
        if (rc < 0) set_error_errno();
        return rc;
    }

    XrdCl::URL url(std::string{path});
    if (!url.IsValid()) {
        errno = EINVAL; set_error("fdpio_stat: invalid URL"); return -1;
    }
    XrdCl::FileSystem fs(url);
    XrdCl::StatInfo *info = nullptr;
    auto st = fs.Stat(url.GetPath(), info);
    if (!st.IsOK()) {
        delete info;
        set_error_status(st);
        errno = errno_from_status(st);
        return -1;
    }

    std::memset(out, 0, sizeof(*out));
    out->st_size  = info->GetSize();
    out->st_mtime = info->GetModTime();
    mode_t mode   = 0;
    if (info->TestFlags(XrdCl::StatInfo::IsReadable)) mode |= S_IRUSR;
    if (info->TestFlags(XrdCl::StatInfo::IsWritable)) mode |= S_IWUSR;
    mode |= info->TestFlags(XrdCl::StatInfo::IsDir) ? S_IFDIR : S_IFREG;
    out->st_mode = mode;
    delete info;
    return 0;
}

int fdpio_listdir(const char *path,
                  int (*cb)(const char *name, int is_dir, void *ctx),
                  void *ctx) {
    if (ensure_initialized() != 0) { errno = EIO; return -1; }
    if (!path || !cb) {
        errno = EINVAL; set_error("fdpio_listdir: null arg"); return -1;
    }

    if (!fdpio_is_url(path)) {
        DIR *d = ::opendir(path);
        if (!d) { set_error_errno(); return -1; }
        int rc = 0;
        for (;;) {
            errno = 0;
            struct dirent *ent = ::readdir(d);
            if (ent == nullptr) {
                break;   // end-of-dir (errno==0) or error (errno!=0); checked below
            }
            if (std::strcmp(ent->d_name, ".") == 0 ||
                std::strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            int is_dir;
            if (ent->d_type == DT_DIR) {
                is_dir = 1;
            } else if (ent->d_type == DT_UNKNOWN) {
                // Some filesystems leave d_type unset; stat to decide.
                std::string full = std::string(path) + "/" + ent->d_name;
                struct ::stat st;
                is_dir = (::stat(full.c_str(), &st) == 0 &&
                          S_ISDIR(st.st_mode)) ? 1 : 0;
            } else {
                is_dir = 0;
            }
            rc = cb(ent->d_name, is_dir, ctx);
            if (rc != 0) break;   // caller requested early stop
        }
        // Distinguish a readdir error from normal end-of-iteration. Only treat
        // it as an error if the callback did NOT request an early stop (rc==0),
        // since an early-stop break can leave errno set by the in-loop ::stat.
        if (rc == 0 && errno != 0) {
            set_error_errno();
            ::closedir(d);
            return -1;
        }
        ::closedir(d);
        return rc;
    }

    XrdCl::URL url(std::string{path});
    if (!url.IsValid()) {
        errno = EINVAL; set_error("fdpio_listdir: invalid URL"); return -1;
    }
    XrdCl::FileSystem fs(url);
    XrdCl::DirectoryList *list = nullptr;
    auto st = fs.DirList(url.GetPath(), XrdCl::DirListFlags::Stat, list);
    if (!st.IsOK()) {
        delete list;
        set_error_status(st);
        errno = errno_from_status(st);
        return -1;
    }
    int rc = 0;
    for (auto it = list->Begin(); it != list->End(); ++it) {
        XrdCl::DirectoryList::ListEntry *entry = *it;
        const std::string &name = entry->GetName();
        XrdCl::StatInfo *info = entry->GetStatInfo();
        int is_dir = (info && info->TestFlags(XrdCl::StatInfo::IsDir)) ? 1 : 0;
        rc = cb(name.c_str(), is_dir, ctx);
        if (rc != 0) break;
    }
    delete list;
    return rc;
}

}  // extern "C"

// Library-load atexit hook: invoke fdpio_cleanup when the library
// unloads. Safe because we don't depend on any other library's
// destructor having run first.
__attribute__((destructor))
static void fdpio_module_dtor() {
    fdpio_cleanup();
}
