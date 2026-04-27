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

/* Utility functions */
int         fdpio_is_url(const char *path);
void        fdpio_set_token(const char *token);
const char *fdpio_error_string(void);

#ifdef __cplusplus
}
#endif

#endif /* FDPIO_H */
