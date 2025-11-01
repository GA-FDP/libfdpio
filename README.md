# libfdpio

Fusion Data Platform I/O library for XRootD access.

## Overview

libfdpio provides a C/C++ interface for accessing files through XRootD protocols, specifically designed for the Fusion Data Platform. It supports various XRootD-compatible protocols including Pelican, HTTP, and HTTPS.

## Features

- Generic XRootD file I/O operations (open, read, seek, close)
- Support for multiple protocols via `FDPIO_PROTOCOLS` environment variable
- Authentication token support for secure access
- Thread-safe operations
- Simple C API for easy integration

## API

### Core Functions

- `fdpio_init()` - Initialize the XRootD subsystem
- `fdpio_open()` - Open an XRootD URL for reading/writing
- `fdpio_read()` - Read data from an open file
- `fdpio_lseek()` - Seek to a position in the file
- `fdpio_close()` - Close an open file
- `fdpio_cleanup()` - Cleanup XRootD resources

### Utility Functions

- `fdpio_is_url()` - Check if a URL is XRootD-compatible
- `fdpio_stat()` - Check if a file exists
- `fdpio_set_token()` - Set authentication token
- `fdpio_error_string()` - Get last error message

## Environment Variables

- `FDPIO_PROTOCOLS` - Comma-separated list of supported protocols (default: "pelican,http,https")
- `XRDCP_ALLOW_HTTP` - Must be set to "true" for HTTP-based protocols
- `XRD_PLUGINCONFDIR` - Directory containing XRootD client plugins

## Building

### With CMake

```bash
mkdir build && cd build
cmake ..
make
make install
```

### With Conda

```bash
conda build recipe/
```

## Usage Example

```c
#include <fdpio.h>

int main() {
    // Initialize
    if (fdpio_init() != 0) {
        printf("Failed to initialize: %s\n", fdpio_error_string());
        return 1;
    }
    
    // Open file
    fdpio_handle_t* handle;
    if (fdpio_open("pelican://example.com/path/file.dat", O_RDONLY, &handle) != 0) {
        printf("Failed to open: %s\n", fdpio_error_string());
        return 1;
    }
    
    // Read data
    char buffer[1024];
    ssize_t bytes_read = fdpio_read(handle, buffer, sizeof(buffer));
    
    // Close
    fdpio_close(handle);
    fdpio_cleanup();
    
    return 0;
}
```

## License

Apache 2.0 License (see LICENSE file)
