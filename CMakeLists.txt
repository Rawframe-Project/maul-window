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
option(MAUL_WINDOW_BUILD_SAMPLES "Build the samples" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_WINDOW_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_WINDOW_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_WINDOW_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_WINDOW_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_WINDOW_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_WINDOW_COVERAGE "Instrument for gcov coverage" OFF)
option(MAUL_WINDOW_TEST_BACKEND "Build the headless test backend (mwin_backendTest)" ${MAUL_WINDOW_BUILD_TESTS})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MAUL_WINDOW_LINUX ON)
else()
    set(MAUL_WINDOW_LINUX OFF)
endif()
option(MAUL_WINDOW_WAYLAND "Build the Wayland backend" ${MAUL_WINDOW_LINUX})
option(MAUL_WINDOW_X11 "Build the X11 backend" ${MAUL_WINDOW_LINUX})
option(MAUL_WINDOW_WIN32 "Build the Win32 backend" ${WIN32})
option(MAUL_WINDOW_WEB "Build the web backend" ${EMSCRIPTEN})
option(MAUL_WINDOW_GAMEPAD "Build the gamepad component" ON)

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
    src/pad.c
    src/queue.c
    src/system.c
    src/text.c
    src/window.c)
if(MAUL_WINDOW_TEST_BACKEND)
    target_sources(maul-window PRIVATE src/backend_test.c)
    target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_TEST_BACKEND)
endif()
# The gamepad component: its functions, and the backends' gamepad code.
if(MAUL_WINDOW_GAMEPAD)
    target_sources(maul-window PRIVATE src/gamepad.c)
    target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_GAMEPAD)
endif()
maul_configure_library(maul-window)
target_link_libraries(maul-window PRIVATE maul-unicode::maul-unicode)
if(NOT WIN32)
    target_link_libraries(maul-window PRIVATE m)
    set(MAUL_PKG_LIBS_PRIVATE "-lm")
endif()
if(MAUL_WINDOW_WAYLAND OR MAUL_WINDOW_X11)
    include(cmake/Xkb.cmake)
endif()
if(MAUL_WINDOW_WAYLAND)
    include(cmake/Wayland.cmake)
endif()
if(MAUL_WINDOW_X11)
    include(cmake/X11.cmake)
endif()
if(MAUL_WINDOW_WIN32)
    include(cmake/Win32.cmake)
endif()
if(MAUL_WINDOW_WEB)
    include(cmake/Web.cmake)
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
        if(MAUL_WINDOW_GAMEPAD)
            maul_add_test(gamepad)
        endif()
    endif()
    if(MAUL_WINDOW_WAYLAND)
        # Needs a compositor: skipped without WAYLAND_DISPLAY.
        maul_add_test(wayland POSIX)
        target_compile_definitions(test_wayland PRIVATE _POSIX_C_SOURCE=200809L)
        set_tests_properties(wayland PROPERTIES SKIP_RETURN_CODE 77)
        mwin_add_wayland_server_test(wayland_input)
    endif()
    if(MAUL_WINDOW_X11)
        # Needs an X server: skipped without DISPLAY.
        maul_add_test(x11 POSIX)
        target_compile_definitions(test_x11 PRIVATE _POSIX_C_SOURCE=200809L)
        set_tests_properties(x11 PROPERTIES SKIP_RETURN_CODE 77)
        # Drives the X server through XTEST, which the test links.
        pkg_check_modules(MWIN_TEST_XCB REQUIRED IMPORTED_TARGET xcb xcb-xtest)
        maul_add_test(x11_input POSIX)
        target_compile_definitions(test_x11_input PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_input PRIVATE PkgConfig::MWIN_TEST_XCB)
        set_tests_properties(x11_input PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    if((MAUL_WINDOW_WAYLAND OR MAUL_WINDOW_X11) AND MAUL_WINDOW_GAMEPAD)
        # Makes virtual gamepads through uinput: skipped without it or a
        # display.
        maul_add_test(linux_gamepad POSIX)
        target_compile_definitions(test_linux_gamepad PRIVATE _GNU_SOURCE)
        set_tests_properties(linux_gamepad PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    if(MAUL_WINDOW_WEB)
        # Runs in headless Chrome through puppeteer, which the runner
        # finds through MWIN_NODE_MODULES: skipped without it.
        maul_add_test(web MANUAL)
        add_test(NAME web COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs
                 $<TARGET_FILE:test_web>)
        maul_add_test(web_input MANUAL)
        add_test(NAME web_input COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs
                 $<TARGET_FILE:test_web_input>)
        maul_add_test(web_ime MANUAL)
        add_test(NAME web_ime COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs
                 $<TARGET_FILE:test_web_ime>)
        maul_add_test(web_lifecycle MANUAL)
        add_test(NAME web_lifecycle COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs
                 $<TARGET_FILE:test_web_lifecycle>)
        set_tests_properties(web web_input web_ime web_lifecycle PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    if(MAUL_WINDOW_WIN32)
        maul_add_test(win32)
        target_link_libraries(test_win32 PRIVATE user32)
        # Drives the desktop through SendInput.
        maul_add_test(win32_input)
        target_link_libraries(test_win32_input PRIVATE user32)
        # Injects touch and pen: skipped where Windows cannot (wine).
        maul_add_test(win32_pointer)
        target_link_libraries(test_win32_pointer PRIVATE user32)
        set_tests_properties(win32_pointer PROPERTIES SKIP_RETURN_CODE 77)
        if(MAUL_WINDOW_GAMEPAD AND MAUL_WINDOW_TEST_BACKEND)
            # The XInput gamepads against a stand-in for XInput.
            maul_add_test(win32_gamepad WHITEBOX)
        endif()
        # Composes through the input context: skipped with no input method.
        maul_add_test(win32_ime)
        target_link_libraries(test_win32_ime PRIVATE user32 imm32)
        set_tests_properties(win32_ime PROPERTIES SKIP_RETURN_CODE 77)
        # Changes the user's settings for a moment, and puts them back.
        maul_add_test(win32_system)
        target_link_libraries(test_win32_system PRIVATE user32 advapi32 dwmapi)
        # One desktop: the focus, the pointer and the settings are not
        # shared.
        set_tests_properties(win32 win32_input win32_pointer win32_ime win32_system
                             PROPERTIES RESOURCE_LOCK desktop)
    endif()
    maul_add_header_tests()
endif()

if(MAUL_WINDOW_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(MAUL_WINDOW_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_WINDOW_INSTALL)
    include(cmake/Install.cmake)
endif()
