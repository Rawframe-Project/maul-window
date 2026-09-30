// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepads on macOS through GameController (macos.h), for the pad
// tracker (pad_tracker.h): the controllers with an extended gamepad
// profile, which GameController maps by place, so every one is mapped.
// A controller's name is its vendor name; GameController gives no USB
// ids. Its battery is the charge GameController reports, -1 while the
// state is unknown (a wired pad). Its reading's time is the profile's
// last event's. Connections and disconnections come as notifications on
// the main thread, which only mark that the pads should be looked for.
// Pads are read whether or not the program is in front, as on the other
// desktop platforms. It needs GameController of macOS 11; before it, no
// pad is found. The motors come in a later slice.

#include "macos.h"

#import <GameController/GameController.h>
#include <math.h>
#include <string.h>

static int32_t List(void* self, void** pads, uint32_t capacity) API_AVAILABLE(macos(11.0))
{
    (void)self;
    uint32_t count = 0;
    for (GCController* controller in [GCController controllers])
    {
        if (count < capacity && controller.extendedGamepad != nil)
        {
            pads[count++] = [controller retain];
        }
    }
    return (int32_t)count;
}

static void Release(void* self, void* pad)
{
    (void)self;
    [(GCController*)pad release];
}

static uint32_t Bit(bool held, mwinGamepadButton button)
{
    return held ? 1u << button : 0;
}

static bool Read(void* self, void* pad, mwinPadReading* reading) API_AVAILABLE(macos(11.0))
{
    (void)self;
    GCExtendedGamepad* profile = ((GCController*)pad).extendedGamepad;
    if (profile == nil)
    {
        return false;
    }
    // The time's bits: the tracker only asks whether it moved.
    double time = profile.lastEventTimestamp;
    *reading = (mwinPadReading){0};
    memcpy(&reading->timestamp, &time, sizeof(time));
    GCControllerDirectionPad* dpad = profile.dpad;
    reading->buttons =
        Bit(dpad.up.pressed, mwin_padDpadUp) | Bit(dpad.down.pressed, mwin_padDpadDown) |
        Bit(dpad.left.pressed, mwin_padDpadLeft) | Bit(dpad.right.pressed, mwin_padDpadRight) |
        Bit(profile.buttonA.pressed, mwin_padFaceSouth) |
        Bit(profile.buttonB.pressed, mwin_padFaceEast) |
        Bit(profile.buttonX.pressed, mwin_padFaceWest) |
        Bit(profile.buttonY.pressed, mwin_padFaceNorth) |
        Bit(profile.leftShoulder.pressed, mwin_padShoulderLeft) |
        Bit(profile.rightShoulder.pressed, mwin_padShoulderRight) |
        Bit(profile.leftThumbstickButton.pressed, mwin_padStickLeft) |
        Bit(profile.rightThumbstickButton.pressed, mwin_padStickRight) |
        Bit(profile.buttonMenu.pressed, mwin_padStart) |
        Bit(profile.buttonOptions.pressed, mwin_padSelect) |
        Bit(profile.buttonHome.pressed, mwin_padGuide);
    // GameController counts up as positive.
    reading->axes[mwin_padStickLeftX] = profile.leftThumbstick.xAxis.value;
    reading->axes[mwin_padStickLeftY] = -profile.leftThumbstick.yAxis.value;
    reading->axes[mwin_padStickRightX] = profile.rightThumbstick.xAxis.value;
    reading->axes[mwin_padStickRightY] = -profile.rightThumbstick.yAxis.value;
    reading->axes[mwin_padTriggerLeft] = profile.leftTrigger.value;
    reading->axes[mwin_padTriggerRight] = profile.rightTrigger.value;
    return true;
}

static bool Vibrate(void* self, void* pad, float low, float high)
{
    (void)self;
    (void)pad;
    (void)low;
    (void)high;
    return false;
}

// The vendor name as UTF-8 that fits, a character never split.
static void Describe(void* self, void* pad, mwinGamepadInfo* info)
{
    (void)self;
    NSString* name = ((GCController*)pad).vendorName;
    if (name.length == 0)
    {
        name = @"Gamepad";
    }
    NSUInteger used = 0;
    [name getBytes:info->name
             maxLength:sizeof(info->name)
            usedLength:&used
              encoding:NSUTF8StringEncoding
               options:0
                 range:NSMakeRange(0, name.length)
        remainingRange:nullptr];
    info->nameLength = (uint32_t)used;
}

static int8_t Battery(void* self, void* pad) API_AVAILABLE(macos(11.0))
{
    (void)self;
    GCDeviceBattery* battery = ((GCController*)pad).battery;
    if (battery == nil || battery.batteryState == GCDeviceBatteryStateUnknown)
    {
        return -1;
    }
    float level = battery.batteryLevel;
    return (int8_t)(level <= 0.0f ? 0 : level >= 1.0f ? 100 : lroundf(level * 100.0f));
}

static bool Changed(void* self)
{
    mwinMacPlatform* platform = self;
    bool changed = platform->padsChanged;
    platform->padsChanged = false;
    return changed;
}

static id Watch(mwinMacPlatform* platform, NSNotificationName name)
{
    return [[[NSNotificationCenter defaultCenter] addObserverForName:name
                                                              object:nil
                                                               queue:nil
                                                          usingBlock:^(NSNotification* note) {
                                                            (void)note;
                                                            platform->padsChanged = true;
                                                          }] retain];
}

void mwinMacStartPads(mwinMacPlatform* platform)
{
    if (@available(macOS 11.3, *))
    {
        // Set before the application finishes launching, as it must be.
        GCController.shouldMonitorBackgroundEvents = YES;
        platform->padObservers[0] = Watch(platform, GCControllerDidConnectNotification);
        platform->padObservers[1] = Watch(platform, GCControllerDidDisconnectNotification);
        const mwinPadRuntime runtime = {platform, List,     Release, Read,
                                        Vibrate,  Describe, Battery, Changed};
        mwinPadTrackerStart(&platform->pads, platform->context, &runtime);
        platform->padsStarted = true;
    }
}

void mwinMacStopPads(mwinMacPlatform* platform)
{
    if (!platform->padsStarted)
    {
        return;
    }
    mwinPadTrackerStop(&platform->pads);
    for (int i = 0; i < 2; i++)
    {
        [[NSNotificationCenter defaultCenter] removeObserver:platform->padObservers[i]];
        [platform->padObservers[i] release];
    }
    platform->padsStarted = false;
}

void mwinMacPumpPads(mwinMacPlatform* platform, uint64_t nowNs)
{
    if (platform->padsStarted)
    {
        mwinPadTrackerPump(&platform->pads, nowNs);
    }
}
