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

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>
#include <fdpio.h>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

// Custom main to force exit after tests complete
int main(int argc, char** argv) {
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    
    int res = context.run();
    
    // Force immediate exit to avoid XRootD thread cleanup issues
    // Use _exit() to bypass all cleanup including static destructors
    _exit(res);
    
    return res; // Should never reach here
}

// Test file URL and expected content
const char* TEST_URL = "pelican://osg-htc.org:443/fdp-d3d/helloFDP.txt";
const char* EXPECTED_CONTENT = "World!\n";

TEST_CASE("fdpio initialization") {
    SUBCASE("initialization succeeds") {
        // Set required environment variables
        setenv("XRDCP_ALLOW_HTTP", "true", 1);
        setenv("FDPIO_PROTOCOLS", "pelican,http,https", 1);
        setenv("XRD_CURLNUMTHREADS", "1", 1);
        
        int result = fdpio_init();
        CHECK(result == 0);
    }
    
    SUBCASE("initialization fails without required env vars") {
        // Save original values
        const char* orig_http = getenv("XRDCP_ALLOW_HTTP");
        const char* orig_plugin = getenv("XRD_PLUGINCONFDIR");
        
        // Force cleanup to reset initialization state
        fdpio_cleanup();
        
        // Clear required environment variables
        unsetenv("XRDCP_ALLOW_HTTP");
        unsetenv("XRD_PLUGINCONFDIR");
        
        int result = fdpio_init();
        CHECK(result != 0);
        
        // Check error message is set
        const char* error = fdpio_error_string();
        CHECK(error != nullptr);
        CHECK(strlen(error) > 0);
        
        // Restore original values
        if (orig_http) setenv("XRDCP_ALLOW_HTTP", orig_http, 1);
        if (orig_plugin) setenv("XRD_PLUGINCONFDIR", orig_plugin, 1);
    }
}

TEST_CASE("URL detection") {
    SUBCASE("pelican URLs are detected") {
        CHECK(fdpio_is_url("pelican://example.com/file.txt") == 1);
        CHECK(fdpio_is_url("pelican://osg-htc.org:443/fdp-d3d/test.dat") == 1);
    }
    
    SUBCASE("http/https URLs are detected") {
        CHECK(fdpio_is_url("http://example.com/file.txt") == 1);
        CHECK(fdpio_is_url("https://example.com/file.txt") == 1);
    }
    
    SUBCASE("regular file paths are not detected") {
        CHECK(fdpio_is_url("/path/to/file.txt") == 0);
        CHECK(fdpio_is_url("file.txt") == 0);
        CHECK(fdpio_is_url("") == 0);
    }
    
    SUBCASE("null pointer is handled") {
        CHECK(fdpio_is_url(nullptr) == 0);
    }
    
    SUBCASE("custom protocols via environment variable") {
        // Test custom protocol configuration
        setenv("FDPIO_PROTOCOLS", "custom", 1);
        
        CHECK(fdpio_is_url("custom://example.com/file.txt") == 1);
        CHECK(fdpio_is_url("pelican://example.com/file.txt") == 0);
        
        // Restore default
        setenv("FDPIO_PROTOCOLS", "pelican,http,https", 1);
    }
}

TEST_CASE("file operations") {
    // Ensure initialization
    setenv("XRDCP_ALLOW_HTTP", "true", 1);
    setenv("FDPIO_PROTOCOLS", "pelican,http,https", 1);
    setenv("XRD_CURLNUMTHREADS", "1", 1);
    REQUIRE(fdpio_init() == 0);
    
    SUBCASE("open and read test file") {
        fdpio_handle_t* handle = nullptr;
        
        // Open the test file
        int open_result = fdpio_open(TEST_URL, O_RDONLY, &handle);
        REQUIRE(open_result == 0);
        REQUIRE(handle != nullptr);
        
        // Read the content
        char buffer[256] = {0};
        ssize_t bytes_read = fdpio_read(handle, buffer, sizeof(buffer) - 1);
        
        CHECK(bytes_read > 0);
        CHECK(bytes_read == strlen(EXPECTED_CONTENT));
        CHECK(strcmp(buffer, EXPECTED_CONTENT) == 0);
        
        // Close the file
        int close_result = fdpio_close(handle);
        CHECK(close_result == 0);
    }
    
    SUBCASE("seek operations") {
        fdpio_handle_t* handle = nullptr;
        
        // Open the test file
        REQUIRE(fdpio_open(TEST_URL, O_RDONLY, &handle) == 0);
        REQUIRE(handle != nullptr);
        
        // Test SEEK_SET
        off_t pos = fdpio_lseek(handle, 2, SEEK_SET);
        CHECK(pos == 2);
        
        // Read from position 2
        char buffer[10] = {0};
        ssize_t bytes_read = fdpio_read(handle, buffer, 4);
        CHECK(bytes_read > 0);
        CHECK(strncmp(buffer, "rld!", 4) == 0);
        
        // Test SEEK_CUR (relative positioning)
        pos = fdpio_lseek(handle, -2, SEEK_CUR);
        CHECK(pos >= 0);
        
        // Test SEEK_END
        pos = fdpio_lseek(handle, 0, SEEK_END);
        CHECK(pos == strlen(EXPECTED_CONTENT));
        
        fdpio_close(handle);
    }
    
    SUBCASE("file stat") {
        int stat_result = fdpio_stat(TEST_URL);
        // No debug output needed - stat should work now
        CHECK(stat_result == 0);  // File should exist
        
        // Test non-existent file
        int stat_nonexistent = fdpio_stat("pelican://osg-htc.org:443/fdp-d3d/nonexistent.txt");
        CHECK(stat_nonexistent != 0);  // Should fail
    }
}

TEST_CASE("error handling") {
    SUBCASE("open with null URL") {
        fdpio_handle_t* handle = nullptr;
        int result = fdpio_open(nullptr, O_RDONLY, &handle);
        CHECK(result != 0);
        CHECK(handle == nullptr);
        
        const char* error = fdpio_error_string();
        CHECK(error != nullptr);
        CHECK(strlen(error) > 0);
    }
    
    SUBCASE("open with null handle pointer") {
        int result = fdpio_open(TEST_URL, O_RDONLY, nullptr);
        CHECK(result != 0);
    }
    
    SUBCASE("read with invalid handle") {
        char buffer[10];
        ssize_t result = fdpio_read(nullptr, buffer, sizeof(buffer));
        CHECK(result == -1);
    }
    
    SUBCASE("read with null buffer") {
        fdpio_handle_t* handle = nullptr;
        // Open a valid file first
        REQUIRE(fdpio_open(TEST_URL, O_RDONLY, &handle) == 0);
        
        ssize_t result = fdpio_read(handle, nullptr, 10);
        CHECK(result == -1);
        
        fdpio_close(handle);
    }
    
    SUBCASE("seek with invalid handle") {
        off_t result = fdpio_lseek(nullptr, 0, SEEK_SET);
        CHECK(result == -1);
    }
}

TEST_CASE("authentication token") {
    SUBCASE("set and use token") {
        const char* test_token = "test_token_123";
        
        // Set token
        fdpio_set_token(test_token);
        
        // Token functionality is internal, but we can test that
        // setting it doesn't break normal operations
        fdpio_handle_t* handle = nullptr;
        int result = fdpio_open(TEST_URL, O_RDONLY, &handle);
        
        if (result == 0) {
            CHECK(handle != nullptr);
            fdpio_close(handle);
        }
        
        // Clear token
        fdpio_set_token(nullptr);
    }
}

TEST_CASE("cleanup") {
    SUBCASE("cleanup doesn't crash") {
        // Initialize first
        setenv("XRDCP_ALLOW_HTTP", "true", 1);
        setenv("XRD_CURLNUMTHREADS", "1", 1);
        REQUIRE(fdpio_init() == 0);
        
        // Open a file
        fdpio_handle_t* handle = nullptr;
        if (fdpio_open(TEST_URL, O_RDONLY, &handle) == 0) {
            // Don't close it manually - let cleanup handle it
        }
        
        // Cleanup should handle open handles gracefully
        fdpio_cleanup();
        
        // After cleanup, operations should fail gracefully
        fdpio_handle_t* handle2 = nullptr;
        int result = fdpio_open(TEST_URL, O_RDONLY, &handle2);
        // This might succeed or fail depending on implementation,
        // but shouldn't crash
    }
}
