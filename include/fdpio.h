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

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// XRootD handle management constants
#define FDPIO_FD_BASE 10000
#define FDPIO_MAX_HANDLES 10000
#define FDPIO_FD_MAX (FDPIO_FD_BASE + FDPIO_MAX_HANDLES)

// Generic XRootD file handle for FDP I/O
typedef struct fdpio_handle {
    void* xrd_file;     // XrdCl::File pointer
    int is_open;
    char* url;
    char* auth_token;
    off_t current_pos;
} fdpio_handle_t;

// Initialize XRootD subsystem for FDP I/O
int fdpio_init(void);

// Open an XRootD URL for FDP I/O
int fdpio_open(const char* url, int flags, fdpio_handle_t** handle);

// Read from XRootD file
ssize_t fdpio_read(fdpio_handle_t* handle, void* buffer, size_t size);

// Seek in XRootD file
off_t fdpio_lseek(fdpio_handle_t* handle, off_t offset, int whence);

// Close XRootD file
int fdpio_close(fdpio_handle_t* handle);

// Cleanup XRootD resources
void fdpio_cleanup(void);

// Check if a URL is XRootD-compatible
int fdpio_is_url(const char* url);

// Set authentication token for XRootD requests
void fdpio_set_token(const char* token);

// Get error string for last XRootD operation
const char* fdpio_error_string(void);

// Check if an XRootD file exists
int fdpio_stat(const char* url);

#ifdef __cplusplus
}
#endif

#endif // FDPIO_H