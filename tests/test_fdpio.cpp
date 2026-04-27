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
// tests. Federation form (pelican://...) — origin URLs (xroots://)
// bypass the xrdcl-pelican-fdp plugin and miss bugs specific to that
// path (e.g. read-past-EOF returning a non-OK status). Override via
// FDPIO_TEST_URL if your environment routes pelican differently.
constexpr const char *kDefaultTestUrl =
    "pelican://osg-htc.org:443/fdp-d3d/archives/ptdata/"
    "ptdata3/16592x/165920.NBI";

const char *test_url() {
    const char *override_url = std::getenv("FDPIO_TEST_URL");
    return (override_url && *override_url) ? override_url : kDefaultTestUrl;
}

// URL tests run by default. They auto-skip when the environment can't
// support them:
//   1. FDPIO_SKIP_URL_TESTS is set — explicit opt-out (offline dev).
//   2. XRD_PLUGINCONFDIR is unset — without it, XrdCl can't load the
//      xrdcl-pelican-fdp plugin and pelican:// URLs fail at open.
//      The conda recipe sets it to $PREFIX/etc/xrootd/client.plugins.d,
//      and fdp_env.sh sets it for local dev.
//   3. Federation auth not available — neither BEARER_TOKEN env nor a
//      readable ~/.fdp/token file (fdpio_init reads the file as a
//      fallback). Forked-PR CI runs hit this path because GitHub doesn't
//      share secrets with forks.
// Returns nullptr if URL tests should run, or a non-null reason string.
const char *url_skip_reason() {
    if (std::getenv("FDPIO_SKIP_URL_TESTS")) {
        return "FDPIO_SKIP_URL_TESTS is set";
    }
    const char *plugin = std::getenv("XRD_PLUGINCONFDIR");
    if (!plugin || !*plugin) {
        return "XRD_PLUGINCONFDIR not set (xrdcl-pelican-fdp plugin won't load)";
    }
    const char *tok = std::getenv("BEARER_TOKEN");
    if (tok && *tok) return nullptr;
    if (const char *home = std::getenv("HOME")) {
        std::string token_path = std::string(home) + "/.fdp/token";
        if (::access(token_path.c_str(), R_OK) == 0) return nullptr;
    }
    return "no federation auth available (BEARER_TOKEN env unset and ~/.fdp/token missing)";
}

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

TEST_CASE("URL open/read/close round-trip") {
    if (auto reason = url_skip_reason()) {
        MESSAGE("Skipping URL test: " << reason);
        return;
    }
    REQUIRE(fdpio_init() == 0);

    int fd = fdpio_open(test_url(), O_RDONLY);
    REQUIRE_MESSAGE(fd >= 0,
        "URL open failed (set FDPIO_SKIP_URL_TESTS=1 to skip, or override "
        "the test URL via FDPIO_TEST_URL): "
        << fdpio_error_string());
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

// ---------------------------------------------------------------------------
// EOF semantics
//
// POSIX read() returns 0 at end-of-file and -1 only on error. Local fds get
// this for free from the kernel. URL-backed fds go through XrdCl::File::Read
// which returns a non-OK status when the requested offset is past the end
// of file ("Error response: bad address" from the origin); without a size
// cache, libfdpio2 would propagate that as -1 and break every read-to-EOF
// loop. These tests pin the contract.
// ---------------------------------------------------------------------------

TEST_CASE("local fd: read past EOF returns 0, not -1") {
    REQUIRE(fdpio_init() == 0);
    std::string path = make_tmp_path(".dat");

    int wfd = fdpio_open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    REQUIRE(wfd >= 0);
    const char *msg = "hello-libfdpio";
    REQUIRE(fdpio_write(wfd, msg, std::strlen(msg)) ==
            (ssize_t)std::strlen(msg));
    REQUIRE(fdpio_close(wfd) == 0);

    int fd = fdpio_open(path.c_str(), O_RDONLY);
    REQUIRE(fd >= 0);

    char buf[64];
    ssize_t n1 = fdpio_read(fd, buf, sizeof(buf));
    CHECK(n1 == (ssize_t)std::strlen(msg));

    // Subsequent read must return 0 (EOF), never -1.
    ssize_t n2 = fdpio_read(fd, buf, sizeof(buf));
    CHECK(n2 == 0);

    // And again — repeated EOF reads stay at 0.
    ssize_t n3 = fdpio_read(fd, buf, sizeof(buf));
    CHECK(n3 == 0);

    CHECK(fdpio_close(fd) == 0);
    ::unlink(path.c_str());
}

TEST_CASE("local fd: chunked read to EOF terminates with 0") {
    REQUIRE(fdpio_init() == 0);
    std::string path = make_tmp_path(".dat");

    // Write a payload that's not a multiple of the chunk size, so the
    // last successful read is partial — the case that surprises naive
    // implementations.
    int wfd = fdpio_open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    REQUIRE(wfd >= 0);
    constexpr size_t TOTAL = 9 * 4096 + 137;   // 36k + change
    std::string payload(TOTAL, '\0');
    for (size_t i = 0; i < TOTAL; ++i) payload[i] = (char)(i & 0xff);
    REQUIRE(fdpio_write(wfd, payload.data(), TOTAL) == (ssize_t)TOTAL);
    REQUIRE(fdpio_close(wfd) == 0);

    int fd = fdpio_open(path.c_str(), O_RDONLY);
    REQUIRE(fd >= 0);

    constexpr size_t CHUNK = 4096;
    std::string out;
    char buf[CHUNK];
    while (true) {
        ssize_t n = fdpio_read(fd, buf, CHUNK);
        REQUIRE(n >= 0);   // never -1, even at end-of-file
        if (n == 0) break;
        out.append(buf, (size_t)n);
    }
    CHECK(out == payload);

    CHECK(fdpio_close(fd) == 0);
    ::unlink(path.c_str());
}

TEST_CASE("URL fd: chunked read to EOF terminates with 0, not -1") {
    if (auto reason = url_skip_reason()) {
        MESSAGE("Skipping URL EOF test: " << reason);
        return;
    }
    REQUIRE(fdpio_init() == 0);

    int fd = fdpio_open(test_url(), O_RDONLY);
    REQUIRE_MESSAGE(fd >= 0,
        "URL open failed (URL test was opted in but URL is unreachable): "
        << fdpio_error_string());
    CHECK(fd >= (1 << 20));   // synthetic fd

    // Probe size via lseek(SEEK_END) and rewind.
    off_t end = fdpio_lseek(fd, 0, SEEK_END);
    REQUIRE(end > 0);
    REQUIRE(fdpio_lseek(fd, 0, SEEK_SET) == 0);

    // Read until exhausted in chunks. Critically: after the last byte
    // is delivered, the next read MUST return 0, NOT -1. That regression
    // is what shipped in v2.0.0 and broke ptdata's JSON index loader.
    constexpr size_t CHUNK = 64 * 1024;
    std::vector<char> buf(CHUNK);
    ssize_t total = 0;
    int reads = 0;
    while (true) {
        ssize_t n = fdpio_read(fd, buf.data(), buf.size());
        REQUIRE_MESSAGE(n >= 0, "fdpio_read returned -1 mid-stream or at EOF: "
                                << fdpio_error_string());
        if (n == 0) break;
        total += n;
        reads++;
        REQUIRE(reads < 10000);   // sanity bound — file is ~hundreds of KB
    }
    CHECK(total == end);

    // A second read at EOF still returns 0.
    ssize_t after_eof = fdpio_read(fd, buf.data(), buf.size());
    CHECK(after_eof == 0);

    CHECK(fdpio_close(fd) == 0);
}

TEST_CASE("URL fd: oversized read clamps at EOF") {
    if (auto reason = url_skip_reason()) {
        MESSAGE("Skipping URL EOF clamp test: " << reason);
        return;
    }
    REQUIRE(fdpio_init() == 0);

    int fd = fdpio_open(test_url(), O_RDONLY);
    REQUIRE_MESSAGE(fd >= 0,
        "URL open failed (URL test was opted in but URL is unreachable): "
        << fdpio_error_string());

    off_t end = fdpio_lseek(fd, 0, SEEK_END);
    REQUIRE(end > 0);

    // Position 100 bytes from the end, request a buffer much larger
    // than what's left. fdpio_read must return exactly the remaining
    // 100 bytes (clamped, not failed).
    REQUIRE(fdpio_lseek(fd, end - 100, SEEK_SET) == end - 100);

    constexpr size_t BIG = 4 * 1024 * 1024;
    std::vector<char> buf(BIG);
    ssize_t n = fdpio_read(fd, buf.data(), buf.size());
    CHECK(n == 100);

    // Next read returns 0.
    ssize_t after = fdpio_read(fd, buf.data(), buf.size());
    CHECK(after == 0);

    CHECK(fdpio_close(fd) == 0);
}

TEST_CASE("URL fd: read at EOF after seek returns 0") {
    if (auto reason = url_skip_reason()) {
        MESSAGE("Skipping URL seek-to-EOF test: " << reason);
        return;
    }
    REQUIRE(fdpio_init() == 0);

    int fd = fdpio_open(test_url(), O_RDONLY);
    REQUIRE_MESSAGE(fd >= 0,
        "URL open failed (URL test was opted in but URL is unreachable): "
        << fdpio_error_string());

    // Seek directly to end, then read. Must return 0.
    off_t end = fdpio_lseek(fd, 0, SEEK_END);
    REQUIRE(end > 0);

    char buf[64];
    ssize_t n = fdpio_read(fd, buf, sizeof(buf));
    CHECK(n == 0);

    CHECK(fdpio_close(fd) == 0);
}
