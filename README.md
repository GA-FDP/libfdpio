# libfdpio2

Fusion Data Platform I/O library for XRootD access — v2 ABI.

## Overview

libfdpio2 provides a C/C++ interface for accessing files through XRootD protocols, specifically designed for the Fusion Data Platform. It supports various XRootD-compatible protocols including Pelican, HTTP, and HTTPS.

This is the v2 generation of the library, distributed as the `libfdpio2` conda package (sonamed `libfdpio2.so.0`). It coexists with the older `libfdpio` (v1) package — install both if you have consumers spanning both ABIs. Public functions retain the `fdpio_` prefix; the public header installs to `$PREFIX/include/fdpio2/fdpio.h`, so consumers add `-I$PREFIX/include/fdpio2` and keep `#include <fdpio.h>`.

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

### Required
- `XRDCP_ALLOW_HTTP` - Must be set to "true" for HTTP-based protocols
- `XRD_PLUGINCONFDIR` - Directory containing XRootD client plugins

### Optional
- `FDPIO_PROTOCOLS` - Comma-separated list of supported protocols (default: "pelican,http,https")

### Authentication
Authentication tokens are resolved in the following order:

1. **`BEARER_TOKEN`** - If set, this token value is used directly
2. **`BEARER_TOKEN_FILE`** - If `BEARER_TOKEN` is not set, read token from this file path
3. **`~/.fdp/token`** - Default fallback if `BEARER_TOKEN_FILE` is not set

Example:
```bash
# Option 1: Direct token
export BEARER_TOKEN="your-jwt-token-here"

# Option 2: Custom token file
export BEARER_TOKEN_FILE="/path/to/your/token.txt"

# Option 3: Default location (no setup needed)
# Token will be read from ~/.fdp/token
```

The token file should contain the bearer token on a single line, with optional trailing newline.

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

## Known limitations

### Not fork-safe

libfdpio2 inherits XrdCl's fork-unsafety. Once a process has opened a URL
(directly via `fdpio_open` or transitively via libfdpio2's auto-init), it
must not `fork()` and then attempt URL operations in the child. Concretely:

- libfdpio2's own `pthread_atfork` child handler resets the lazy-init flag
  so the child re-runs `fdpio_init()` cleanly. That part is correct.
- But XrdCl's plugin manager and the xrdcl-pelican-fdp / libcurl backends
  carry per-process state (resolver thread, connection cache, plugin
  singletons) that is not fork-safe. The child will hang inside
  `XrdCl::File::Open` when the inherited state interacts with newly-spawned
  worker threads.

Use `posix_spawn(3)` or `fork()`-then-`execve(3)` instead of bare `fork()`
for child workers that need URL access. Local-path operations
(`fdpio_open` on a regular file) are unaffected.

## License

Apache 2.0 License (see LICENSE file)
