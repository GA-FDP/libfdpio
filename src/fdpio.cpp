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

#include <fdpio.h>

#include <XrdPosix/XrdPosixXrootdPath.hh>

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <dlfcn.h>
#include <fstream>
#include <string>

static thread_local char fdpio_errbuf[256];

static void set_error(const char *msg) {
    strncpy(fdpio_errbuf, msg, sizeof(fdpio_errbuf) - 1);
    fdpio_errbuf[sizeof(fdpio_errbuf) - 1] = '\0';
}

static void set_error_errno() {
    set_error(strerror(errno));
}

/* Function pointer types for XrdPosix_* API */
typedef int        (*XrdPosix_Open_t)(const char *, int, ...);
typedef int        (*XrdPosix_Close_t)(int);
typedef ssize_t    (*XrdPosix_Read_t)(int, void *, size_t);
typedef ssize_t    (*XrdPosix_Write_t)(int, const void *, size_t);
typedef ssize_t    (*XrdPosix_Pread_t)(int, void *, size_t, off_t);
typedef ssize_t    (*XrdPosix_Pwrite_t)(int, const void *, size_t, off_t);
typedef off_t      (*XrdPosix_Lseek_t)(int, off_t, int);
typedef int        (*XrdPosix_Fstat_t)(int, struct stat *);
typedef int        (*XrdPosix_Stat_t)(const char *, struct stat *);
typedef int        (*XrdPosix_Unlink_t)(const char *);
typedef int        (*XrdPosix_Rename_t)(const char *, const char *);
typedef int        (*XrdPosix_Mkdir_t)(const char *, mode_t);
typedef int        (*XrdPosix_Rmdir_t)(const char *);
typedef DIR *      (*XrdPosix_Opendir_t)(const char *);
typedef struct dirent * (*XrdPosix_Readdir_t)(DIR *);
typedef int        (*XrdPosix_Closedir_t)(DIR *);
typedef int        (*XrdPosix_isMyPath_t)(const char *);

static struct {
    void *handle;
    XrdPosix_Open_t     Open;
    XrdPosix_Close_t    Close;
    XrdPosix_Read_t     Read;
    XrdPosix_Write_t    Write;
    XrdPosix_Pread_t    Pread;
    XrdPosix_Pwrite_t   Pwrite;
    XrdPosix_Lseek_t    Lseek;
    XrdPosix_Fstat_t    Fstat;
    XrdPosix_Stat_t     Stat;
    XrdPosix_Unlink_t   Unlink;
    XrdPosix_Rename_t   Rename;
    XrdPosix_Mkdir_t    Mkdir;
    XrdPosix_Rmdir_t    Rmdir;
    XrdPosix_Opendir_t  Opendir;
    XrdPosix_Readdir_t  Readdir;
    XrdPosix_Closedir_t Closedir;
    XrdPosix_isMyPath_t isMyPath;
} xrd = {};

#define LOAD_SYM(name) \
    xrd.name = (XrdPosix_##name##_t)dlsym(xrd.handle, "XrdPosix_" #name); \
    if (!xrd.name) { \
        fprintf(stderr, "fdpio: dlsym XrdPosix_%s: %s\n", #name, dlerror()); \
        ok = false; \
    }

__attribute__((constructor))
static void fdpio_load_xrdposix(void) {
    xrd.handle = dlopen("libXrdPosixPreload.so", RTLD_LAZY | RTLD_LOCAL);
    if (!xrd.handle)
        xrd.handle = dlopen("libXrdPosixPreload.so.2", RTLD_LAZY | RTLD_LOCAL);
    if (!xrd.handle) {
        fprintf(stderr, "fdpio: cannot load libXrdPosixPreload.so: %s\n", dlerror());
        return;
    }

    bool ok = true;
    LOAD_SYM(Open)
    LOAD_SYM(Close)
    LOAD_SYM(Read)
    LOAD_SYM(Write)
    LOAD_SYM(Pread)
    LOAD_SYM(Pwrite)
    LOAD_SYM(Lseek)
    LOAD_SYM(Fstat)
    LOAD_SYM(Stat)
    LOAD_SYM(Unlink)
    LOAD_SYM(Rename)
    LOAD_SYM(Mkdir)
    LOAD_SYM(Rmdir)
    LOAD_SYM(Opendir)
    LOAD_SYM(Readdir)
    LOAD_SYM(Closedir)

    /* XrdPosix_isMyPath has C++ linkage (not inside extern "C") */
    xrd.isMyPath = (XrdPosix_isMyPath_t)dlsym(xrd.handle,
                                               "_Z17XrdPosix_isMyPathPKc");
    if (!xrd.isMyPath) {
        fprintf(stderr, "fdpio: dlsym XrdPosix_isMyPath: %s\n", dlerror());
        ok = false;
    }

    if (!ok)
        fprintf(stderr, "fdpio: some XrdPosix symbols could not be resolved\n");
}

#undef LOAD_SYM

extern "C" {

int fdpio_init(void) {
    /* Register protocols from FDPIO_PROTOCOLS (default: pelican,http,https) */
    const char *protos_env = getenv("FDPIO_PROTOCOLS");
    std::string protos = protos_env ? protos_env : "pelican,http,https";

    size_t start = 0;
    while (start < protos.size()) {
        size_t end = protos.find(',', start);
        if (end == std::string::npos)
            end = protos.size();
        std::string proto = protos.substr(start, end - start);
        if (!proto.empty()) {
            std::string proto_url = proto + "://";
            XrdPosixXrootPath::AddProto(proto_url.c_str());
        }
        start = end + 1;
    }

    /* Set XRDCP_ALLOW_HTTP if not already set */
    setenv("XRDCP_ALLOW_HTTP", "true", 0);

    /* Resolve auth token: BEARER_TOKEN -> BEARER_TOKEN_FILE -> ~/.fdp/token */
    if (!getenv("BEARER_TOKEN")) {
        const char *token_file = getenv("BEARER_TOKEN_FILE");
        std::string token_path;
        if (token_file) {
            token_path = token_file;
        } else {
            const char *home = getenv("HOME");
            if (home) {
                token_path = std::string(home) + "/.fdp/token";
            }
        }
        if (!token_path.empty()) {
            std::ifstream ifs(token_path);
            if (ifs.is_open()) {
                std::string token;
                std::getline(ifs, token);
                if (!token.empty()) {
                    setenv("BEARER_TOKEN", token.c_str(), 1);
                }
            }
        }
    }

    fdpio_errbuf[0] = '\0';
    return 0;
}

void fdpio_cleanup(void) {
}

int fdpio_open(const char *path, int oflag, ...) {
    if (!xrd.Open) { errno = ENOSYS; return -1; }
    mode_t mode = 0;
    if (oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    int fd = xrd.Open(path, oflag, mode);
    if (fd < 0)
        set_error_errno();
    return fd;
}

int fdpio_close(int fildes) {
    if (!xrd.Close) { errno = ENOSYS; return -1; }
    int rc = xrd.Close(fildes);
    if (rc < 0)
        set_error_errno();
    return rc;
}

ssize_t fdpio_read(int fildes, void *buf, size_t nbyte) {
    if (!xrd.Read) { errno = ENOSYS; return -1; }
    ssize_t rc = xrd.Read(fildes, buf, nbyte);
    if (rc < 0)
        set_error_errno();
    return rc;
}

ssize_t fdpio_write(int fildes, const void *buf, size_t nbyte) {
    if (!xrd.Write) { errno = ENOSYS; return -1; }
    ssize_t rc = xrd.Write(fildes, buf, nbyte);
    if (rc < 0)
        set_error_errno();
    return rc;
}

ssize_t fdpio_pread(int fildes, void *buf, size_t nbyte, off_t offset) {
    if (!xrd.Pread) { errno = ENOSYS; return -1; }
    ssize_t rc = xrd.Pread(fildes, buf, nbyte, offset);
    if (rc < 0)
        set_error_errno();
    return rc;
}

ssize_t fdpio_pwrite(int fildes, const void *buf, size_t nbyte, off_t offset) {
    if (!xrd.Pwrite) { errno = ENOSYS; return -1; }
    ssize_t rc = xrd.Pwrite(fildes, buf, nbyte, offset);
    if (rc < 0)
        set_error_errno();
    return rc;
}

off_t fdpio_lseek(int fildes, off_t offset, int whence) {
    if (!xrd.Lseek) { errno = ENOSYS; return -1; }
    off_t rc = xrd.Lseek(fildes, offset, whence);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_fstat(int fildes, struct stat *buf) {
    if (!xrd.Fstat) { errno = ENOSYS; return -1; }
    int rc = xrd.Fstat(fildes, buf);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_stat(const char *path, struct stat *buf) {
    if (!xrd.Stat) { errno = ENOSYS; return -1; }
    int rc = xrd.Stat(path, buf);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_unlink(const char *path) {
    if (!xrd.Unlink) { errno = ENOSYS; return -1; }
    int rc = xrd.Unlink(path);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_rename(const char *oname, const char *nname) {
    if (!xrd.Rename) { errno = ENOSYS; return -1; }
    int rc = xrd.Rename(oname, nname);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_mkdir(const char *path, mode_t mode) {
    if (!xrd.Mkdir) { errno = ENOSYS; return -1; }
    int rc = xrd.Mkdir(path, mode);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_rmdir(const char *path) {
    if (!xrd.Rmdir) { errno = ENOSYS; return -1; }
    int rc = xrd.Rmdir(path);
    if (rc < 0)
        set_error_errno();
    return rc;
}

DIR *fdpio_opendir(const char *path) {
    if (!xrd.Opendir) { errno = ENOSYS; return NULL; }
    DIR *d = xrd.Opendir(path);
    if (!d)
        set_error_errno();
    return d;
}

struct dirent *fdpio_readdir(DIR *dirp) {
    if (!xrd.Readdir) { errno = ENOSYS; return NULL; }
    return xrd.Readdir(dirp);
}

int fdpio_closedir(DIR *dirp) {
    if (!xrd.Closedir) { errno = ENOSYS; return -1; }
    int rc = xrd.Closedir(dirp);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_is_url(const char *path) {
    if (!xrd.isMyPath) return 0;
    return xrd.isMyPath(path);
}

void fdpio_set_token(const char *token) {
    setenv("BEARER_TOKEN", token, 1);
}

const char *fdpio_error_string(void) {
    return fdpio_errbuf;
}

} /* extern "C" */
