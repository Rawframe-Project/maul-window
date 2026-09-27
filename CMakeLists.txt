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
option(MAUL_WINDOW_TEST_BACKEND "Build the headless test backend (mwin_backendTest)" ${MAUL_WINDOW_BUILD_TESTS})

# Maul Unicode validates every string that crosses the library: the
# installed package when there is one, otherwise the pinned release,
# built and installed along. It comes before the family modules below,
# whose copies then serve this project.
set(MAUL_WINDOW_UNICODE_VERSION 0.2.0)
find_package(maul-unicode ${MAUL_WINDOW_UNICODE_VERSION} CONFIG QUIET)
if(NOT maul-unicode_FOUND)
    include(FetchContent)
    set(MAUL_UNICODE_INSTALL ${MAUL_WINDOW_INSTALL})
    if(MAUL_WINDOW_BUILD_SHARED)
        # Its static library goes into ours.
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    endif()
    FetchContent_Declare(maul-unicode
        GIT_REPOSITORY https://github.com/Rawframe-Project/maul-unicode.git
        GIT_TAG v${MAUL_WINDOW_UNICODE_VERSION}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(maul-unicode)
endif()

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
    src/allocator.c
    src/base.c
    src/context.c
    src/input.c
    src/monitor.c
    src/native.c
    src/queue.c
    src/system.c
    src/text.c
    src/window.c)
if(MAUL_WINDOW_TEST_BACKEND)
    target_sources(maul-window PRIVATE src/backend_test.c)
    target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_TEST_BACKEND)
endif()
maul_configure_library(maul-window)
target_link_libraries(maul-window PRIVATE maul-unicode::maul-unicode)
if(NOT WIN32)
    target_link_libraries(maul-window PRIVATE m)
    set(MAUL_PKG_LIBS_PRIVATE "-lm")
endif()
set(MAUL_PACKAGE_DEPENDENCIES "find_dependency(maul-unicode ${MAUL_WINDOW_UNICODE_VERSION})")
set(MAUL_PKG_REQUIRES_PRIVATE "maul-unicode >= ${MAUL_WINDOW_UNICODE_VERSION}")

if(MAUL_WINDOW_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    if(MAUL_WINDOW_TEST_BACKEND)
        maul_add_test(window)
        maul_add_test(input)
        maul_add_test(lifecycle)
        maul_add_test(monitor)
        maul_add_test(system)
        maul_add_test(ime)
        maul_add_test(properties)
    endif()
    maul_add_header_tests()
endif()

if(MAUL_WINDOW_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_WINDOW_INSTALL)
    include(cmake/Install.cmake)
endif()
