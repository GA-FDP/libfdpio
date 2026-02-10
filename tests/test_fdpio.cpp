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
#include <cstring>
#include <cstdlib>
#include <signal.h>
#include <sys/wait.h>

#include <fdpio.h>

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
//const char* TEST_URL = "xroot://fdp-d3d-origin.nationalresearchplatform.org:8443/fdp-d3d/helloFDP.txt";
const char* EXPECTED_CONTENT = "World!\n";


TEST_CASE("file operations") {
    // Ensure initialization
    setenv("XRDCP_ALLOW_HTTP", "true", 1);
    setenv("XRD_CURLNUMTHREADS", "1", 1);
    REQUIRE(fdpio_init() == 0);

    int fd = fdpio_open(TEST_URL, O_RDONLY);

    SUBCASE("open and read test file") {

        REQUIRE(fd > 0);

        char buffer[1024];
        ssize_t bytes_read;

        bytes_read = fdpio_read(fd, buffer, sizeof(buffer));

        CHECK(bytes_read == 7);

        CHECK(strcmp(buffer, EXPECTED_CONTENT) == 0);

    }

    SUBCASE("seek operations") {
        REQUIRE(fd > 0);

        // Test SEEK_SET
        off_t pos = fdpio_lseek(fd, 2, SEEK_SET);
        CHECK(pos == 2);

        // Read from position 2
        char buffer[10] = {0};
        ssize_t bytes_read = fdpio_read(fd, buffer, 4);
        CHECK(bytes_read > 0);
        CHECK(strncmp(buffer, "rld!", 4) == 0);

        // Test SEEK_CUR (relative positioning)
        pos = fdpio_lseek(fd, -2, SEEK_CUR);
        CHECK(pos >= 0);
    }

    SUBCASE("file stat") {

        struct stat stat_result;
        int status = fdpio_stat(TEST_URL, &stat_result);
        CHECK(status == 0);  // File should exist

        // Test non-existent file
        struct stat bad_stat_result;
        int stat_nonexistent = fdpio_stat("pelican://osg-htc.org:443/fdp-d3d/nonexistent.txt", &bad_stat_result);
        CHECK(stat_nonexistent != 0);  // Should fail
    }

    if (fd > 0)
        fdpio_close(fd);
}



TEST_CASE("directory operations") {
    // Ensure initialization
    setenv("XRDCP_ALLOW_HTTP", "true", 1);
    setenv("XRD_CURLNUMTHREADS", "1", 1);
    REQUIRE(fdpio_init() == 0);

    // Test directory URL - contains multiple files including helloFDP.txt
    const char* TEST_DIR_URL = "pelican://osg-htc.org:443/fdp-d3d";


    SUBCASE("read directory entries") {

        DIR *d = fdpio_opendir(TEST_DIR_URL);
        REQUIRE(d != nullptr);

        // Should contain the helloFDP.txt file
        struct dirent *entry;

        bool found_helloFDP = false;
        bool found_archives = false;
        while ((entry = fdpio_readdir(d)) != NULL) {
            if (strcmp(entry->d_name, "helloFDP.txt") == 0) {
                found_helloFDP = true;
            }

            if (strcmp(entry->d_name, "archives") == 0) {
                found_archives = true;
            }
        }

        CHECK(found_helloFDP);
        CHECK(found_archives);

        CHECK(fdpio_closedir(d) != -1);
    }


}
