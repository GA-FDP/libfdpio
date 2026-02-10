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

#include <XrdPosix/XrdPosixExtern.hh>
#include <XrdPosix/XrdPosixXrootdPath.hh>

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cerrno>
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
    mode_t mode = 0;
    if (oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    int fd = XrdPosix_Open(path, oflag, mode);
    if (fd < 0)
        set_error_errno();
    return fd;
}

int fdpio_close(int fildes) {
    int rc = XrdPosix_Close(fildes);
    if (rc < 0)
        set_error_errno();
    return rc;
}

ssize_t fdpio_read(int fildes, void *buf, size_t nbyte) {
    ssize_t rc = XrdPosix_Read(fildes, buf, nbyte);
    if (rc < 0)
        set_error_errno();
    return rc;
}

ssize_t fdpio_write(int fildes, const void *buf, size_t nbyte) {
    ssize_t rc = XrdPosix_Write(fildes, buf, nbyte);
    if (rc < 0)
        set_error_errno();
    return rc;
}

ssize_t fdpio_pread(int fildes, void *buf, size_t nbyte, off_t offset) {
    ssize_t rc = XrdPosix_Pread(fildes, buf, nbyte, offset);
    if (rc < 0)
        set_error_errno();
    return rc;
}

ssize_t fdpio_pwrite(int fildes, const void *buf, size_t nbyte, off_t offset) {
    ssize_t rc = XrdPosix_Pwrite(fildes, buf, nbyte, offset);
    if (rc < 0)
        set_error_errno();
    return rc;
}

off_t fdpio_lseek(int fildes, off_t offset, int whence) {
    off_t rc = XrdPosix_Lseek(fildes, offset, whence);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_fstat(int fildes, struct stat *buf) {
    int rc = XrdPosix_Fstat(fildes, buf);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_stat(const char *path, struct stat *buf) {
    int rc = XrdPosix_Stat(path, buf);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_unlink(const char *path) {
    int rc = XrdPosix_Unlink(path);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_rename(const char *oname, const char *nname) {
    int rc = XrdPosix_Rename(oname, nname);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_mkdir(const char *path, mode_t mode) {
    int rc = XrdPosix_Mkdir(path, mode);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_rmdir(const char *path) {
    int rc = XrdPosix_Rmdir(path);
    if (rc < 0)
        set_error_errno();
    return rc;
}

DIR *fdpio_opendir(const char *path) {
    DIR *d = XrdPosix_Opendir(path);
    if (!d)
        set_error_errno();
    return d;
}

struct dirent *fdpio_readdir(DIR *dirp) {
    return XrdPosix_Readdir(dirp);
}

int fdpio_closedir(DIR *dirp) {
    int rc = XrdPosix_Closedir(dirp);
    if (rc < 0)
        set_error_errno();
    return rc;
}

int fdpio_is_url(const char *path) {
    return XrdPosix_isMyPath(path);
}

void fdpio_set_token(const char *token) {
    setenv("BEARER_TOKEN", token, 1);
}

const char *fdpio_error_string(void) {
    return fdpio_errbuf;
}

} /* extern "C" */
