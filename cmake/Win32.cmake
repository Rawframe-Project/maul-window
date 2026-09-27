# The Win32 backend. It links the system libraries every Windows has:
# user32 for windows and input, shcore for monitor DPI.

set(MWIN_WIN32_SOURCES
    src/backend_win32.c
    src/win32_input.c
    src/win32_output.c
    src/win32_window.c)
target_sources(maul-window PRIVATE ${MWIN_WIN32_SOURCES})
target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_WIN32)
# Windows 10 1703 or later, for per-monitor DPI awareness version 2.
set_source_files_properties(${MWIN_WIN32_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "_WIN32_WINNT=0x0A00;WINVER=0x0A00;UNICODE;_UNICODE")
target_link_libraries(maul-window PRIVATE user32 shcore)
string(APPEND MAUL_PKG_LIBS_PRIVATE " -luser32 -lshcore")
