// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window;

import android.view.InputDevice;
import java.util.ArrayList;
import java.util.List;

/**
 * A stand-in for the copies Gamepads reads (in its package, which the
 * interface is private to): the devices whose names start with "Maul
 * Stale" read as Android 11 can leave an app's copy of a pad it read while
 * the pad's kernel devices were being merged, a touchpad's pointer with no
 * joystick ranges. Other devices read as they are.
 */
public final class StaleCopies implements Gamepads.Copies {
    private final Gamepads.Copies real;

    private StaleCopies(Gamepads.Copies real) {
        this.real = real;
    }

    /** Puts the stand-in in place, for the rest of the process. */
    public static void install() {
        Gamepads.copies = new StaleCopies(Gamepads.copies);
    }

    private static boolean stale(InputDevice device) {
        return device.getName().startsWith("Maul Stale");
    }

    @Override
    public int sources(InputDevice device) {
        return stale(device) ? InputDevice.SOURCE_MOUSE : real.sources(device);
    }

    @Override
    public List<InputDevice.MotionRange> ranges(InputDevice device) {
        return stale(device) ? new ArrayList<>() : real.ranges(device);
    }
}
