/*
Copyright 2026 General Atomics

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
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

// This exe deliberately never calls fdpio_init() before the first URL op.
// Pre-fix (libfdpio2 2.0.1) this fails: fdpio_open falls through the POSIX
// shim and emits literal openat("pelican://...") = ENOENT. Post-fix (2.0.2)
// fdpio_open self-initializes and the URL resolves normally.

namespace {
const char *test_url() {
    return "pelican://osg-htc.org:443/fdp-d3d/archives/index/json/"
           "json_indexes_2026-01-13_12:22:11/1659/165920.json";
}

const char *skip_reason() {
    if (!std::getenv("XRD_PLUGINCONFDIR"))
        return "XRD_PLUGINCONFDIR not set (xrdcl-pelican-fdp plugin won't load)";
    if (!std::getenv("BEARER_TOKEN") && !std::getenv("BEARER_TOKEN_FILE"))
        return "no BEARER_TOKEN / BEARER_TOKEN_FILE set; osg-htc origin will deny";
    return nullptr;
}
}  // namespace

TEST_CASE("fdpio_open(URL) succeeds without prior fdpio_init()") {
    if (const char *why = skip_reason()) {
        MESSAGE("Skipping: " << why);
        return;
    }
    int fd = fdpio_open(test_url(), O_RDONLY);
    REQUIRE(fd >= 0);
    char buf[64];
    ssize_t n = fdpio_read(fd, buf, sizeof buf);
    CHECK(n > 0);
    fdpio_close(fd);
}

TEST_CASE("fdpio_stat(URL) succeeds without prior fdpio_init()") {
    if (const char *why = skip_reason()) {
        MESSAGE("Skipping: " << why);
        return;
    }
    struct stat st;
    int rc = fdpio_stat(test_url(), &st);
    REQUIRE(rc == 0);
    CHECK(st.st_size > 0);
}
