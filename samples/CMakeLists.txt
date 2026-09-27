# In-tree samples: small programs built with the library.

add_executable(sample_window window.c)
target_link_libraries(sample_window PRIVATE maul-window)
maul_apply_flags(sample_window)
