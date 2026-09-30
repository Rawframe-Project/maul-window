# The iOS backend (mwin-0025), in Objective-C with manual retain and
# release, as the macOS backend: UIKit for scenes, windows and input,
# CoreGraphics for its geometry, QuartzCore for the views' CAMetalLayers
# and the display link.

enable_language(OBJC)
target_sources(maul-window PRIVATE
    src/apple_text.m
    src/backend_ios.m
    src/ios_keys.m
    src/ios_output.m
    src/ios_pointer.m
    src/ios_text.m
    src/ios_view.m
    src/ios_window.m)
target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_IOS)
set_target_properties(maul-window PROPERTIES
    OBJC_STANDARD 23
    OBJC_STANDARD_REQUIRED ON
    OBJC_EXTENSIONS OFF
    OBJC_VISIBILITY_PRESET hidden)
target_compile_options(maul-window PRIVATE
    $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc -Wmissing-prototypes>)
target_link_libraries(maul-window PRIVATE "-framework CoreGraphics" "-framework Foundation"
    "-framework QuartzCore" "-framework UIKit")
