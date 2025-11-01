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
#include <XrdCl/XrdClFile.hh>
#include <XrdCl/XrdClFileSystem.hh>
#include <XrdCl/XrdClURL.hh>
#include <XrdCl/XrdClXRootDResponses.hh>
#include <memory>
#include <string>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <cstdio>
#include <unistd.h>
#include <mutex>
#include <vector>
#include <sstream>

// Global state (simplified - no complex synchronization needed)
static bool g_fdpio_initialized = false;
static std::string g_global_auth_token;
static std::string g_last_error;
static std::mutex g_fdpio_mutex;  // Only for init/cleanup, not for operations

// Helper function to split string by delimiter
std::vector<std::string> splitString(const std::string& input, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(input);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Global handle management for FD mapping
fdpio_handle_t* fdpio_handles[FDPIO_MAX_HANDLES] = {NULL};
int next_fdpio_fd = FDPIO_FD_BASE;

extern "C" {

// Handle management functions for FD mapping
int add_fdpio_handle(fdpio_handle_t* handle) {
    std::lock_guard<std::mutex> lock(g_fdpio_mutex);
    for (int i = 0; i < FDPIO_MAX_HANDLES; i++) {
        if (!fdpio_handles[i]) {
            fdpio_handles[i] = handle;
            int fd = FDPIO_FD_BASE + i;
            return fd;
        }
    }
    return -1; // No available slots
}

fdpio_handle_t* get_fdpio_handle(int fd) {
    if (fd < FDPIO_FD_BASE || fd >= FDPIO_FD_MAX) return NULL;
    std::lock_guard<std::mutex> lock(g_fdpio_mutex);
    return fdpio_handles[fd - FDPIO_FD_BASE];
}

void remove_fdpio_handle(int fd) {
    if (fd < FDPIO_FD_BASE || fd >= FDPIO_FD_MAX) return;
    std::lock_guard<std::mutex> lock(g_fdpio_mutex);
    fdpio_handles[fd - FDPIO_FD_BASE] = NULL;
}

// Internal init function - assumes mutex is already held
static int _fdpio_init() {
    if (g_fdpio_initialized) return 0; // Already initialized
    
    // Check if required environment variables are set
    const char* allow_http = getenv("XRDCP_ALLOW_HTTP");
    const char* plugin_dir = getenv("XRD_PLUGINCONFDIR");
    
    if (!allow_http || strcmp(allow_http, "true") != 0) {
        g_last_error = "XRDCP_ALLOW_HTTP must be set to 'true'";
        return -1;
    }
    
    if (!plugin_dir) {
        g_last_error = "XRD_PLUGINCONFDIR must be set to plugin directory";
        return -1;
    }
    
    try {
        // Try to load authentication token from ~/.fdp/token
        const char* home = getenv("HOME");
        if (home) {
            std::string token_path = std::string(home) + "/.fdp/token";
            FILE* token_file = fopen(token_path.c_str(), "r");
            if (token_file) {
                char token_buffer[4096];
                if (fgets(token_buffer, sizeof(token_buffer), token_file)) {
                    // Remove trailing newline
                    size_t len = strlen(token_buffer);
                    if (len > 0 && token_buffer[len-1] == '\n') {
                        token_buffer[len-1] = '\0';
                    }
                    g_global_auth_token = token_buffer;
                }
                fclose(token_file);
            }
        }
        
        g_fdpio_initialized = true;
        return 0;
    } catch (const std::exception& e) {
        g_last_error = std::string("Exception during initialization: ") + e.what();
        return -1;
    } catch (...) {
        g_last_error = "Unknown exception during initialization";
        return -1;
    }
}

int fdpio_is_url(const char* url) {
    if (!url) return 0;
    
    // Check FDPIO_PROTOCOLS environment variable for supported protocols
    const char* protocols_env = getenv("FDPIO_PROTOCOLS");
    if (!protocols_env) {
        // Default to pelican, http, https if not set
        protocols_env = "pelican,http,https";
    }
    
    std::vector<std::string> protocols = splitString(std::string(protocols_env), ',');
    std::string url_str(url);
    
    for (const auto& protocol : protocols) {
        std::string protocol_prefix = protocol + "://";
        if (url_str.find(protocol_prefix) == 0) {
            return 1;
        }
    }
    
    return 0;
}

int fdpio_init() {
    std::lock_guard<std::mutex> lock(g_fdpio_mutex);
    return _fdpio_init();
}

int fdpio_open(const char* url, int flags, fdpio_handle_t** handle) {
    
    if (!url || !handle) {
        g_last_error = "NULL URL or handle provided";
        return -1;
    }
    
    // Only lock for initialization check
    {
        std::lock_guard<std::mutex> lock(g_fdpio_mutex);
        if (!g_fdpio_initialized) {
            int init_result = _fdpio_init();
            if (init_result != 0) {
                g_last_error = "Failed to initialize Pelican subsystem";
                return -1;
            }
        }
    }
    
    
    try {
        // Allocate handle
        *handle = (fdpio_handle_t*)calloc(1, sizeof(fdpio_handle_t));
        if (!*handle) {
            g_last_error = "Memory allocation failed";
            return -1;
        }
        
        // Create XrdCl::File
        std::unique_ptr<XrdCl::File> file = std::make_unique<XrdCl::File>();
        (*handle)->xrd_file = file.release();
        (*handle)->url = strdup(url);
        (*handle)->current_pos = 0;
        
        // Copy auth token (outside of global lock)
        {
            std::lock_guard<std::mutex> lock(g_fdpio_mutex);
            (*handle)->auth_token = g_global_auth_token.empty() ? nullptr : strdup(g_global_auth_token.c_str());
        }
        
        // Convert flags
        XrdCl::OpenFlags::Flags xrd_flags = XrdCl::OpenFlags::Read;
        if (flags & O_WRONLY) {
            xrd_flags = XrdCl::OpenFlags::Write;
        } else if (flags & O_RDWR) {
            xrd_flags = static_cast<XrdCl::OpenFlags::Flags>(XrdCl::OpenFlags::Read | XrdCl::OpenFlags::Write);
        } else {
        }
        
        XrdCl::File* xrd_file = static_cast<XrdCl::File*>((*handle)->xrd_file);
        
        // Set authentication token if available
        std::string final_url = url;
        if ((*handle)->auth_token && strlen((*handle)->auth_token) > 0) {
            final_url += "?authz=";
            final_url += (*handle)->auth_token;
        } else {
        }
        
        // SYNCHRONOUS OPERATION - no callbacks, no background threads
        auto status = xrd_file->Open(final_url.c_str(), xrd_flags, XrdCl::Access::None);
        
        if (status.IsOK()) {
            (*handle)->is_open = 1;
            return 0;
        } else {
            g_last_error = std::string("Failed to open file: ") + status.GetErrorMessage();
            fdpio_close(*handle);
            *handle = nullptr;
            return -1;
        }
    } catch (const std::exception& e) {
        g_last_error = std::string("Exception while opening: ") + e.what();
        if (*handle) {
            fdpio_close(*handle);
            *handle = nullptr;
        }
        return -1;
    } catch (...) {
        g_last_error = "Unknown exception while opening";
        if (*handle) {
            fdpio_close(*handle);
            *handle = nullptr;
        }
        return -1;
    }
}

ssize_t fdpio_read(fdpio_handle_t* handle, void* buffer, size_t size) {
    if (!handle || !buffer || size == 0 || !handle->is_open) {
        g_last_error = "Invalid handle, buffer, or size";
        return -1;
    }
    
    try {
        XrdCl::File* xrd_file = static_cast<XrdCl::File*>(handle->xrd_file);
        
        // SYNCHRONOUS READ - blocks until complete, no callbacks
        uint32_t bytes_read = 0;
        auto status = xrd_file->Read(handle->current_pos, size, buffer, bytes_read);
        
        if (status.IsOK()) {
            handle->current_pos += bytes_read;
            return bytes_read;
        } else {
            g_last_error = std::string("Read failed: ") + status.GetErrorMessage();
            return -1;
        }
    } catch (const std::exception& e) {
        g_last_error = std::string("Exception during read: ") + e.what();
        return -1;
    } catch (...) {
        g_last_error = "Unknown exception during read";
        return -1;
    }
}

off_t fdpio_lseek(fdpio_handle_t* handle, off_t offset, int whence) {
    if (!handle || !handle->is_open) {
        g_last_error = "Invalid handle or file not open";
        return -1;
    }
    
    off_t new_pos;
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = handle->current_pos + offset;
            break;
        case SEEK_END:
            // Get file size using synchronous stat
            try {
                XrdCl::File* xrd_file = static_cast<XrdCl::File*>(handle->xrd_file);
                
                // SYNCHRONOUS STAT - no callbacks
                XrdCl::StatInfo* stat_info = nullptr;
                auto status = xrd_file->Stat(false, stat_info);
                
                if (status.IsOK() && stat_info) {
                    off_t file_size = stat_info->GetSize();
                    new_pos = file_size + offset;
                    delete stat_info;
                } else {
                    g_last_error = std::string("Failed to stat file for SEEK_END: ") + status.GetErrorMessage();
                    return -1;
                }
            } catch (const std::exception& e) {
                g_last_error = std::string("Exception during SEEK_END stat: ") + e.what();
                return -1;
            } catch (...) {
                g_last_error = "Unknown exception during SEEK_END stat";
                return -1;
            }
            break;
        default:
            g_last_error = "Invalid whence value";
            return -1;
    }
    
    if (new_pos < 0) {
        g_last_error = "Negative seek position";
        return -1;
    }
    
    handle->current_pos = new_pos;
    return new_pos;
}

int fdpio_close(fdpio_handle_t* handle) {
    if (!handle) return 0;
    
    int result = 0;
    
    if (handle->is_open && handle->xrd_file) {
        try {
            XrdCl::File* xrd_file = static_cast<XrdCl::File*>(handle->xrd_file);
            
            // SYNCHRONOUS CLOSE - no callbacks
            auto status = xrd_file->Close();
            if (!status.IsOK()) {
                result = -1;
            }
        } catch (...) {
            result = -1;
        }
    }
    
    // Clean up resources
    if (handle->xrd_file) {
        delete static_cast<XrdCl::File*>(handle->xrd_file);
        handle->xrd_file = nullptr;
    }
    if (handle->url) {
        free(handle->url);
        handle->url = nullptr;
    }
    if (handle->auth_token) {
        free(handle->auth_token);
        handle->auth_token = nullptr;
    }
    
    free(handle);
    return result;
}

void fdpio_cleanup() {
    std::lock_guard<std::mutex> lock(g_fdpio_mutex);
    
    // Close all open xrdcl handles
    for (int i = 0; i < FDPIO_MAX_HANDLES; i++) {
        if (fdpio_handles[i]) {
            fdpio_close(fdpio_handles[i]);
            fdpio_handles[i] = NULL;
        }
    }
    
    g_fdpio_initialized = false;
    g_global_auth_token.clear();
    g_last_error.clear();
}

void fdpio_set_token(const char* token) {
    std::lock_guard<std::mutex> lock(g_fdpio_mutex);
    g_global_auth_token = token ? token : "";
}

const char* fdpio_error_string() {
    return g_last_error.c_str();
}

int fdpio_stat(const char* url) {
    if (!url) {
        g_last_error = "NULL URL provided to stat";
        return -1;
    }
    
    // Only lock for initialization check
    {
        std::lock_guard<std::mutex> lock(g_fdpio_mutex);
        if (!g_fdpio_initialized && _fdpio_init() != 0) {
            g_last_error = "Failed to initialize Pelican subsystem for stat";
            return -1;
        }
    }
    
    try {
        // Parse URL to get components
        XrdCl::URL orig_url(url);
        
        // Create FileSystem with base URL (server only, no path, no auth)
        std::string base_url = std::string(orig_url.GetProtocol()) + "://" + 
                              orig_url.GetHostName() + ":" + 
                              std::to_string(orig_url.GetPort());
        
        XrdCl::URL fs_url(base_url);
        XrdCl::FileSystem fs(fs_url);
        
        // Build path with auth token (like xrdfs command)
        std::string stat_path = orig_url.GetPath();
        {
            std::lock_guard<std::mutex> lock(g_fdpio_mutex);
            if (!g_global_auth_token.empty()) {
                stat_path += "?authz=";
                stat_path += g_global_auth_token;
            }
        }
        
        // SYNCHRONOUS STAT - path includes auth token
        XrdCl::StatInfo* stat_info = nullptr;
        auto status = fs.Stat(stat_path.c_str(), stat_info);
        
        if (status.IsOK()) {
            if (stat_info) delete stat_info;
            return 0;
        } else {
            g_last_error = std::string("Stat failed: ") + status.ToString();
            return -1;
        }
    } catch (const std::exception& e) {
        g_last_error = std::string("Exception during stat: ") + e.what();
        return -1;
    } catch (...) {
        g_last_error = "Unknown exception during stat";
        return -1;
    }
}

} // extern "C"
