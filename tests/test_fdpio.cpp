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

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <fdpio.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// Path on disk for local-file tests. Each test creates its own temp
// file under the system temp dir so parallel runs are safe.
std::string make_tmp_path(const char *suffix) {
    char tmpl[] = "/tmp/libfdpio_test_XXXXXX";
    int fd = mkstemp(tmpl);
    REQUIRE(fd >= 0);
    ::close(fd);
    std::string p = tmpl;
    p += suffix;
    ::rename(tmpl, p.c_str());
    return p;
}

// URL of the canonical 165920.NBI shot used by the URL round-trip
// test. Must be reachable from this host.
constexpr const char *kTestUrl =
    "xroots://fdp-d3d-origin.nationalresearchplatform.org:8443//"
    "fdp-d3d/archives/ptdata/ptdata3/16592x/165920.NBI";

}  // namespace

TEST_CASE("local open/write/read/close round-trip") {
    REQUIRE(fdpio_init() == 0);

    std::string path = make_tmp_path(".dat");
    int wfd = fdpio_open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    REQUIRE(wfd >= 0);
    REQUIRE(wfd < (1 << 20));   // local fd, not synthetic

    const char *msg = "hello-libfdpio";
    ssize_t n = fdpio_write(wfd, msg, std::strlen(msg));
    CHECK(n == (ssize_t)std::strlen(msg));
    CHECK(fdpio_close(wfd) == 0);

    int rfd = fdpio_open(path.c_str(), O_RDONLY);
    REQUIRE(rfd >= 0);
    CHECK(rfd < (1 << 20));

    char buf[64] = {0};
    n = fdpio_read(rfd, buf, sizeof(buf) - 1);
    CHECK(n == (ssize_t)std::strlen(msg));
    CHECK(std::strcmp(buf, msg) == 0);
    CHECK(fdpio_close(rfd) == 0);

    ::unlink(path.c_str());
}

TEST_CASE("local lseek SEEK_END/SEEK_SET round-trip") {
    REQUIRE(fdpio_init() == 0);

    std::string path = make_tmp_path(".dat");
    int wfd = fdpio_open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    REQUIRE(wfd >= 0);
    fdpio_write(wfd, "0123456789", 10);
    fdpio_close(wfd);

    int fd = fdpio_open(path.c_str(), O_RDONLY);
    REQUIRE(fd >= 0);
    off_t end = fdpio_lseek(fd, 0, SEEK_END);
    CHECK(end == 10);
    off_t back = fdpio_lseek(fd, 3, SEEK_SET);
    CHECK(back == 3);
    char c = 0;
    CHECK(fdpio_read(fd, &c, 1) == 1);
    CHECK(c == '3');
    fdpio_close(fd);
    ::unlink(path.c_str());
}

TEST_CASE("fdpio_is_url honors FDPIO_PROTOCOLS env") {
    // Default list should include pelican, http, https, xroot, xroots, root, roots.
    setenv("FDPIO_PROTOCOLS", "pelican,xroots", 1);
    REQUIRE(fdpio_init() == 0);
    CHECK(fdpio_is_url("pelican://host/path") == 1);
    CHECK(fdpio_is_url("xroots://host/path") == 1);
    CHECK(fdpio_is_url("http://host/path") == 0);     // not in custom list
    CHECK(fdpio_is_url("/etc/hostname") == 0);
    CHECK(fdpio_is_url("") == 0);
    CHECK(fdpio_is_url(nullptr) == 0);
    unsetenv("FDPIO_PROTOCOLS");
}

TEST_CASE("URL open/read/close round-trip via xroots://") {
    if (!std::getenv("FDPIO_TEST_URL_ENABLE")) {
        MESSAGE("Skipping URL test: set FDPIO_TEST_URL_ENABLE=1 to run");
        return;
    }
    REQUIRE(fdpio_init() == 0);

    int fd = fdpio_open(kTestUrl, O_RDONLY);
    if (fd < 0) {
        MESSAGE("URL open failed (network/auth issue?): "
                << fdpio_error_string());
        return;
    }
    CHECK(fd >= (1 << 20));   // synthetic fd

    char buf[1024];
    ssize_t n = fdpio_read(fd, buf, sizeof(buf));
    CHECK(n > 16);            // first 1 KiB should include the file header

    CHECK(fdpio_close(fd) == 0);
}

TEST_CASE("synthetic fd doesn't leak to native syscalls") {
    REQUIRE(fdpio_init() == 0);

    // A synthetic fd that doesn't exist should fail clean. We don't
    // call fdpio_open with a URL here (network not required); we just
    // confirm that pretending fd 1<<20 exists returns EBADF and never
    // touches the kernel.
    char buf[16];
    ssize_t n = fdpio_read(1 << 20, buf, sizeof(buf));
    CHECK(n == -1);
    CHECK(errno == EBADF);

    int rc = fdpio_close(1 << 20);
    CHECK(rc == -1);
    CHECK(errno == EBADF);
}
