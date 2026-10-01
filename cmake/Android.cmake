# The Android backend (mwin-0026): NativeActivity's native surface, input
# queue and callbacks, and the choreographer's frames, all from libandroid.
# The program's application also carries the library's Java activity
# (java/maul/window).

target_sources(maul-window PRIVATE
    src/android_window.c
    src/backend_android.c)
target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_ANDROID)
target_link_libraries(maul-window PRIVATE android)
