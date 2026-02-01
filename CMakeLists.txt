cmake_minimum_required(VERSION 3.10)

project(fdpio VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(_LARGEFILE_SOURCE _LARGEFILE64_SOURCE _FILE_OFFSET_BITS=64)

# Prefer shared libraries over static
set(CMAKE_FIND_LIBRARY_SUFFIXES .so .so.1 .so.0)

# Find XRootD libraries manually (conda doesn't provide .pc files)
# Look for the library directory
find_path(XROOTD_LIB_DIR
    NAMES libXrdPosixPreload.so
    HINTS ${CMAKE_PREFIX_PATH}/lib $ENV{PREFIX}/lib $ENV{CONDA_PREFIX}/lib
)

find_path(XROOTD_INCLUDE_DIR
    NAMES XrdCl/XrdClFile.hh
    HINTS ${CMAKE_PREFIX_PATH}/include/xrootd $ENV{PREFIX}/include/xrootd $ENV{CONDA_PREFIX}/include/xrootd
)

# Check if we found everything
if(NOT XROOTD_LIB_DIR)
    message(FATAL_ERROR "XrdPosixPreload library not found. Make sure XRootD is installed.")
endif()

if(NOT XROOTD_INCLUDE_DIR)
    message(FATAL_ERROR "XRootD headers not found. Make sure XRootD development files are installed.")
endif()

message(STATUS "Found XRootD:")
message(STATUS "  Library directory: ${XROOTD_LIB_DIR}")
message(STATUS "  Include directory: ${XROOTD_INCLUDE_DIR}")

# Include directories
include_directories(include)
include_directories(${XROOTD_INCLUDE_DIR})

# Library source files
set(FDPIO_SOURCES
    src/fdpio.cpp
)

# Create shared library
add_library(fdpio SHARED ${FDPIO_SOURCES})

# Force dynamic linking
target_link_options(fdpio PRIVATE "LINKER:-Bdynamic")

# Link against XRootD and dependencies dynamically
target_link_directories(fdpio PRIVATE ${XROOTD_LIB_DIR})
target_link_libraries(fdpio XrdCl XrdPosixPreload XrdPosix curl ssl crypto)

# Set library properties
set_target_properties(fdpio PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/fdpio.h
)

# Installation
include(GNUInstallDirs)

install(TARGETS fdpio
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Optional: Create pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/fdpio.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/fdpio.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/fdpio.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# Enable testing
option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()
    
    # Find doctest (header-only library)
    find_path(DOCTEST_INCLUDE_DIR
        NAMES doctest/doctest.h
        HINTS ${CMAKE_PREFIX_PATH}/include $ENV{PREFIX}/include
    )
    
    if(NOT DOCTEST_INCLUDE_DIR)
        message(WARNING "doctest not found. Downloading header...")
        file(DOWNLOAD 
            "https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h"
            "${CMAKE_CURRENT_BINARY_DIR}/doctest.h"
        )
        set(DOCTEST_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR})
    endif()
    
    message(STATUS "Found/Downloaded doctest: ${DOCTEST_INCLUDE_DIR}")
    
    # Test executable
    add_executable(test_fdpio tests/test_fdpio.cpp)
    target_include_directories(test_fdpio PRIVATE ${DOCTEST_INCLUDE_DIR})
    target_link_libraries(test_fdpio fdpio)
    
    # Add test
    add_test(NAME fdpio_tests COMMAND test_fdpio)
    
    # Set test environment variables
    set_tests_properties(fdpio_tests PROPERTIES
        ENVIRONMENT "XRDCP_ALLOW_HTTP=true;FDPIO_PROTOCOLS=pelican,http,https"
    )
    
    # Install test executable
    install(TARGETS test_fdpio
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
