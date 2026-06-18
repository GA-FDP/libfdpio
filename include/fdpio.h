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

#ifndef FDPIO_H
#define FDPIO_H

/*
 * libfdpio v2.x: POSIX-style I/O over native POSIX (local paths) and
 * XrdCl::File (URL paths). v2 dropped fdpio_pread/pwrite/fstat,
 * fdpio_unlink/rename/mkdir/rmdir, and fdpio_opendir/readdir/closedir
 * because no in-tree caller needed them. They can be re-added if a
 * future caller does.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialization and cleanup */
int         fdpio_init(void);
void        fdpio_cleanup(void);

/* File operations */
int         fdpio_open(const char *path, int oflag, ...);
int         fdpio_close(int fildes);
ssize_t     fdpio_read(int fildes, void *buf, size_t nbyte);
ssize_t     fdpio_write(int fildes, const void *buf, size_t nbyte);
off_t       fdpio_lseek(int fildes, off_t offset, int whence);

/* Path operations */
int         fdpio_stat(const char *path, struct stat *buf);

/* Directory listing.
 *
 * fdpio_listdir: invoke `cb` once per entry in the directory at `path`
 * (a local path or an XRootD-style URL). `is_dir` is nonzero for
 * directory entries. `ctx` is passed through to `cb` untouched.
 * Returning nonzero from `cb` stops the iteration early and that value
 * becomes the return value. Returns 0 on success, -1 on error (see
 * fdpio_error_string()).
 * Callers that need to distinguish an early stop from an error should
 * avoid using -1 as their cb stop value (use any other nonzero value).
 *
 * Design note: the callback form is deliberate. Every other function in
 * this API returns by value/out-param or hands back a *borrowed* pointer
 * (fdpio_error_string's thread_local buffer); nothing transfers ownership
 * of heap memory to the caller. A scandir-style `char ***names` +
 * `fdpio_free_listing()` would have been this library's first
 * caller-frees-this contract -- a new way to leak and a break from the
 * existing invariant. The callback keeps all allocation inside libfdpio
 * and maps cleanly onto the sole consumer (ptdata's
 * FdpioIoProvider::list_dirs accumulating into a std::vector). */
int fdpio_listdir(const char *path,
                  int (*cb)(const char *name, int is_dir, void *ctx),
                  void *ctx);

/* Utility functions */
int         fdpio_is_url(const char *path);
void        fdpio_set_token(const char *token);
const char *fdpio_error_string(void);

#ifdef __cplusplus
}
#endif

#endif /* FDPIO_H */
