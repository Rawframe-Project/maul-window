// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window;

/**
 * The activity of a Maul Window program on Android (mwin-0026). The
 * platform's NativeActivity hands the library its window's surface and
 * input queue; this class adds what the library's C cannot do itself.
 * Name it, or a subclass of it, in the manifest, with the program's
 * native library as {@code android.app.lib_name}.
 */
public class Activity extends android.app.NativeActivity {
    /**
     * The running program, which outlives activities: an activity created
     * while it runs joins it. Written and read by the library only; zero
     * while no program runs.
     */
    static long program;
}
