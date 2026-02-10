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

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
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
ssize_t     fdpio_pread(int fildes, void *buf, size_t nbyte, off_t offset);
ssize_t     fdpio_pwrite(int fildes, const void *buf, size_t nbyte, off_t offset);
off_t       fdpio_lseek(int fildes, off_t offset, int whence);
int         fdpio_fstat(int fildes, struct stat *buf);

/* Path operations */
int         fdpio_stat(const char *path, struct stat *buf);
int         fdpio_unlink(const char *path);
int         fdpio_rename(const char *oname, const char *nname);
int         fdpio_mkdir(const char *path, mode_t mode);
int         fdpio_rmdir(const char *path);

/* Directory operations */
DIR        *fdpio_opendir(const char *path);
struct dirent *fdpio_readdir(DIR *dirp);
int         fdpio_closedir(DIR *dirp);

/* Utility functions */
int         fdpio_is_url(const char *path);
void        fdpio_set_token(const char *token);
const char *fdpio_error_string(void);

#ifdef __cplusplus
}
#endif

#endif /* FDPIO_H */
