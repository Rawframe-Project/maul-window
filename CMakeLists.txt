cmake_minimum_required(VERSION 3.25)

# The library's identity. The family's CMake modules and tools read these.
set(MAUL_LIBRARY maul-window)
set(MAUL_API_PREFIX mwin)
set(MAUL_MACRO_PREFIX MWIN)
set(MAUL_TITLE "Maul Window")
set(MAUL_PREFIX MAUL_WINDOW)

include(cmake/ReadVersion.cmake)
maul_read_version(${CMAKE_CURRENT_SOURCE_DIR}/include/maul-window/base.h MWIN MAUL_VERSION)
project(maul-window
    VERSION ${MAUL_VERSION}
    DESCRIPTION "Windows, input, gamepads and platform services"
    LANGUAGES C)

option(MAUL_WINDOW_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_WINDOW_BUILD_BENCH "Build the benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_WINDOW_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_WINDOW_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_WINDOW_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_WINDOW_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_WINDOW_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_WINDOW_COVERAGE "Instrument for gcov coverage" OFF)

# Every executable (tests, benchmarks, tools) lands in the top of the
# build tree, so scripts and CI find them in one place.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()

include(GNUInstallDirs)
include(cmake/CompilerFlags.cmake)
include(cmake/Library.cmake)
include(cmake/Testing.cmake)

if(MAUL_WINDOW_BUILD_SHARED)
    set(MAUL_LIBRARY_KIND SHARED)
else()
    set(MAUL_LIBRARY_KIND STATIC)
endif()
add_library(maul-window ${MAUL_LIBRARY_KIND}
    src/base.c)
maul_configure_library(maul-window)

if(MAUL_WINDOW_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_header_tests()
endif()

if(MAUL_WINDOW_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_WINDOW_INSTALL)
    include(cmake/Install.cmake)
endif()
