// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window;

import android.hardware.BatteryState;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.CombinedVibration;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.view.InputDevice;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * What the library asks Android about gamepads, which its C reaches only
 * through Java: the gamepads connected, what each is and has, its battery,
 * its motors and its motion sensors. Called by the library on the main
 * thread only.
 */
final class Gamepads {
    // The motion listeners of the gamepads whose sensors are on, by id.
    private static final Map<Integer, Motion> MOTIONS = new HashMap<>();
    // The devices the last list call saw.
    private static int[] sListed = new int[0];

    private Gamepads() {
    }

    /**
     * The devices that are gamepads or joysticks, not virtual: a keyboard's
     * or a remote's arrows alone do not make one. A device is read only
     * once a call before this one has seen it, or when it is the one given,
     * whose gamepad events show it whole: Android merges a pad's kernel
     * devices into one as each appears, and Android 11 keeps the copy an
     * app first read, so one read between two of them stays the first part
     * (a DualShock 4's touchpad) for good. The kernel makes them within
     * milliseconds of each other.
     */
    static int[] list(int sending) {
        int[] ids = InputDevice.getDeviceIds();
        int[] listed = sListed;
        sListed = ids.clone();
        int count = 0;
        for (int id : ids) {
            if ((id == sending || contains(listed, listed.length, id))
                    && isGamepad(InputDevice.getDevice(id))) {
                ids[count++] = id;
            }
        }
        return Arrays.copyOf(ids, count);
    }

    private static boolean isGamepad(InputDevice device) {
        if (device == null || device.isVirtual()) {
            return false;
        }
        int sources = device.getSources();
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    /**
     * A gamepad's name in UTF-8, cut to whole characters within a number of
     * bytes; null once it is gone.
     */
    static byte[] name(int id, int capacity) {
        InputDevice device = InputDevice.getDevice(id);
        if (device == null) {
            return null;
        }
        byte[] bytes = device.getName().getBytes(StandardCharsets.UTF_8);
        int length = Math.min(bytes.length, capacity);
        // Back to the start of a character: not a continuation byte.
        while (length < bytes.length && length > 0 && (bytes[length] & 0xC0) == 0x80) {
            length--;
        }
        return Arrays.copyOf(bytes, length);
    }

    /**
     * What a gamepad is and has: its vendor and product, its motors, a
     * bit for each of the keys given it has (the low and high 32), then
     * its joystick axes in increasing order; null once it is gone.
     */
    static int[] describe(int id, int[] keys) {
        InputDevice device = InputDevice.getDevice(id);
        if (device == null) {
            return null;
        }
        List<InputDevice.MotionRange> ranges = device.getMotionRanges();
        int[] axes = new int[ranges.size()];
        int count = 0;
        for (InputDevice.MotionRange range : ranges) {
            int axis = range.getAxis();
            if ((range.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) != 0
                    && !contains(axes, count, axis)) {
                axes[count++] = axis;
            }
        }
        Arrays.sort(axes, 0, count);
        boolean[] has = device.hasKeys(keys);
        long bits = 0;
        for (int i = 0; i < keys.length && i < 64; i++) {
            bits |= has[i] ? 1L << i : 0;
        }
        int[] facts = new int[5 + count];
        facts[0] = device.getVendorId();
        facts[1] = device.getProductId();
        facts[2] = motors(device).length;
        facts[3] = (int) bits;
        facts[4] = (int) (bits >>> 32);
        System.arraycopy(axes, 0, facts, 5, count);
        return facts;
    }

    private static boolean contains(int[] values, int count, int value) {
        for (int i = 0; i < count; i++) {
            if (values[i] == value) {
                return true;
            }
        }
        return false;
    }

    /** The lowest and highest value of each of a gamepad's joystick axes. */
    static float[] ranges(int id, int[] axes) {
        InputDevice device = InputDevice.getDevice(id);
        float[] limits = new float[axes.length * 2];
        for (int i = 0; device != null && i < axes.length; i++) {
            InputDevice.MotionRange range =
                    device.getMotionRange(axes[i], InputDevice.SOURCE_JOYSTICK);
            limits[2 * i] = range != null ? range.getMin() : -1.0f;
            limits[2 * i + 1] = range != null ? range.getMax() : 1.0f;
        }
        return limits;
    }

    /** The battery's charge in percent, or -1 where unknown or none. */
    static int battery(int id) {
        InputDevice device = InputDevice.getDevice(id);
        if (device == null || Build.VERSION.SDK_INT < 31) {
            return -1;
        }
        BatteryState state = device.getBatteryState();
        float capacity = state.isPresent() ? state.getCapacity() : Float.NaN;
        return Float.isNaN(capacity) ? -1 : Math.round(capacity * 100.0f);
    }

    // The motors, heavy then light where there are two.
    private static Vibrator[] motors(InputDevice device) {
        if (Build.VERSION.SDK_INT >= 31) {
            VibratorManager manager = device.getVibratorManager();
            int[] ids = manager.getVibratorIds();
            Vibrator[] motors = new Vibrator[Math.min(ids.length, 2)];
            for (int i = 0; i < motors.length; i++) {
                motors[i] = manager.getVibrator(ids[i]);
            }
            return motors;
        }
        Vibrator motor = device.getVibrator();
        return motor.hasVibrator() ? new Vibrator[] {motor} : new Vibrator[0];
    }

    /**
     * Runs a gamepad's motors, each from 0 to 1, for a time; 0 stops them.
     * One motor runs at the stronger. False when the gamepad is gone or
     * has none.
     */
    static boolean rumble(int id, float low, float high, int milliseconds) {
        InputDevice device = InputDevice.getDevice(id);
        if (device == null) {
            return false;
        }
        if (Build.VERSION.SDK_INT >= 31) {
            return rumble(device.getVibratorManager(), low, high, milliseconds);
        }
        Vibrator motor = device.getVibrator();
        if (!motor.hasVibrator()) {
            return false;
        }
        int amplitude = Math.round(Math.max(low, high) * 255.0f);
        if (amplitude == 0 || milliseconds == 0) {
            motor.cancel();
        } else {
            motor.vibrate(VibrationEffect.createOneShot(milliseconds, amplitude));
        }
        return true;
    }

    // Both motors in one effect: an input device's vibrator plays its
    // effect on every motor of the device, and a new one replaces the
    // last, so one vibrator at a time would leave both at the latest.
    private static boolean rumble(VibratorManager manager, float low, float high,
            int milliseconds) {
        int[] ids = manager.getVibratorIds();
        int count = Math.min(ids.length, 2);
        float[] strengths = count == 1 ? new float[] {Math.max(low, high)}
                                       : new float[] {low, high};
        CombinedVibration.ParallelCombination effects = CombinedVibration.startParallel();
        boolean running = false;
        for (int i = 0; i < count; i++) {
            int amplitude = Math.round(strengths[i] * 255.0f);
            if (amplitude != 0 && milliseconds != 0) {
                effects.addVibrator(ids[i], VibrationEffect.createOneShot(milliseconds, amplitude));
                running = true;
            }
        }
        if (running) {
            manager.vibrate(effects.combine());
        } else {
            manager.cancel();
        }
        return count > 0;
    }

    // A gamepad's accelerometer and gyroscope (Android 12 and later), or
    // null where it lacks either.
    private static Sensor[] sensors(int id) {
        InputDevice device = InputDevice.getDevice(id);
        if (device == null || Build.VERSION.SDK_INT < 31) {
            return null;
        }
        SensorManager manager = device.getSensorManager();
        Sensor accelerometer = manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        Sensor gyroscope = manager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        return accelerometer != null && gyroscope != null
                ? new Sensor[] {accelerometer, gyroscope}
                : null;
    }

    /** Whether a gamepad has an accelerometer and a gyroscope. */
    static boolean hasMotion(int id) {
        return sensors(id) != null;
    }

    /**
     * Turns a gamepad's motion sensors on, each sample handed to the
     * library on the main thread, or off: false when it has none.
     */
    static boolean motion(int id, boolean on) {
        Motion listening = MOTIONS.remove(id);
        if (listening != null) {
            listening.manager.unregisterListener(listening);
        }
        Sensor[] sensors = on ? sensors(id) : null;
        if (sensors == null) {
            return !on;
        }
        Motion motion = new Motion(InputDevice.getDevice(id).getSensorManager(), id);
        for (Sensor sensor : sensors) {
            motion.manager.registerListener(motion, sensor, SensorManager.SENSOR_DELAY_GAME);
        }
        MOTIONS.put(id, motion);
        return true;
    }

    /** Hands the library a sample: a gyroscope's when gyro, in m/s^2 or rad/s. */
    static native void nativeMotion(long program, int id, boolean gyro, float x, float y, float z,
            long timeNs);

    private static final class Motion implements SensorEventListener {
        final SensorManager manager;
        final int id;

        Motion(SensorManager manager, int id) {
            this.manager = manager;
            this.id = id;
        }

        @Override
        public void onSensorChanged(SensorEvent event) {
            boolean gyro = event.sensor.getType() == Sensor.TYPE_GYROSCOPE;
            nativeMotion(Activity.program, id, gyro, event.values[0], event.values[1],
                    event.values[2], event.timestamp);
        }

        @Override
        public void onAccuracyChanged(Sensor sensor, int accuracy) {
        }
    }
}
