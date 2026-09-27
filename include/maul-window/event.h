// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The event stream. The program drains it in frame with mwinNextEvent,
// which returns every record in the order it arrived, across windows.
// Records wait in storage per window, each with its own limit, so one
// busy window cannot push out another's records. Notifications that
// must be handled before the platform goes on (suspending, a lost
// surface) come first.
//
// A request's completion comes after the notifications the change
// caused: a size request is answered after mwin_eventResized.
//
// Input comes in four classes, each with its own storage per window.
// Discrete records (keys, text, buttons, touches and pen contacts
// beginning or ending) are never merged: when their storage is full the
// window gets mwin_eventInputStateReset instead, which also follows every
// loss of focus, after which no key or button counts as held. Motion,
// raw deltas and the wheel are delivered sample by sample while there is
// room, and merged into the newest waiting record of their kind when
// there is not; samples says how many a record stands for.
//
// Text in a record stays valid until the frame that drained it returns.

#ifndef MAUL_WINDOW_EVENT_H
#define MAUL_WINDOW_EVENT_H

#include "maul-window/input.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // What a record reports.
    typedef uint16_t mwinEventType;

    enum
    {
        mwin_eventNone = 0,
        // The platform made the window.
        mwin_eventWindowCreated = 1,
        // The user asked to close the window; nothing closes by itself.
        mwin_eventCloseRequested = 2,
        // The window was destroyed; its id is stale.
        mwin_eventWindowDestroyed = 3,
        // The logical size changed (data.size).
        mwin_eventResized = 4,
        // The size in pixels changed (data.pixelSize).
        mwin_eventPixelSizeChanged = 5,
        // The scale changed (data.scale), with the size the platform
        // suggests for it.
        mwin_eventScaleChanged = 6,
        // The window moved (data.position).
        mwin_eventMoved = 7,
        mwin_eventFocusGained = 8,
        mwin_eventFocusLost = 9,
        // Nothing of the window can be seen, or it can be again.
        mwin_eventOccluded = 10,
        mwin_eventRevealed = 11,
        // The mode changed (data.mode).
        mwin_eventModeChanged = 12,
        mwin_eventShown = 13,
        mwin_eventHidden = 14,
        // A request was answered (data.completion).
        mwin_eventRequestCompleted = 15,
        // Forget every key and button held: focus was lost or input was.
        mwin_eventInputStateReset = 16,
        // A key went down or up (data.key).
        mwin_eventKeyDown = 17,
        mwin_eventKeyUp = 18,
        // Text was typed or composed (data.text), validated UTF-8.
        mwin_eventTextInput = 19,
        // The cursor moved over the window (data.pointer).
        mwin_eventCursorMoved = 20,
        mwin_eventCursorEntered = 21,
        mwin_eventCursorLeft = 22,
        // A mouse button went down or up (data.pointer).
        mwin_eventButtonDown = 23,
        mwin_eventButtonUp = 24,
        // The wheel turned (data.wheel).
        mwin_eventWheel = 25,
        // The pointing device moved, unscaled (data.delta).
        mwin_eventRawPointerDelta = 26,
        // A touch began, moved, ended or was taken by the system
        // (data.touch).
        mwin_eventTouchDown = 27,
        mwin_eventTouchMoved = 28,
        mwin_eventTouchUp = 29,
        mwin_eventTouchCancelled = 30,
        // A pen moved, touched down, lifted, or a pen button changed
        // (data.pen).
        mwin_eventPenMoved = 31,
        mwin_eventPenDown = 32,
        mwin_eventPenUp = 33,
        mwin_eventPenButtonDown = 34,
        mwin_eventPenButtonUp = 35,
    };

    // The kind of a request.
    typedef uint8_t mwinRequestKind;

    enum
    {
        mwin_requestCreate = 0,
        mwin_requestTitle = 1,
        mwin_requestSize = 2,
        mwin_requestPosition = 3,
        mwin_requestMode = 4,
        mwin_requestVisible = 5,
        mwin_requestFocus = 6,
        mwin_requestCursorMode = 7,
        mwin_requestCursorShape = 8,
    };

    // How a request ended.
    typedef uint8_t mwinOutcome;

    enum
    {
        // The platform did what was asked; the notifications before the
        // completion say what it chose.
        mwin_outcomeDone = 0,
        // The platform or this backend cannot do it.
        mwin_outcomeUnsupported = 1,
        // The platform or the user refused.
        mwin_outcomeDenied = 2,
        // A later request of the same kind on the same window replaced it.
        mwin_outcomeSuperseded = 3,
        // Its window was destroyed first.
        mwin_outcomeCancelled = 4,
        // The platform failed.
        mwin_outcomeFailed = 5,
    };

    // The answer to a request.
    typedef struct mwinCompletion
    {
        mwinRequestId request;
        mwinRequestKind kind;
        mwinOutcome outcome;
    } mwinCompletion;

    // A scale change and the logical size the platform suggests with it.
    typedef struct mwinScaleChange
    {
        float scale;
        mwinSize suggestedSize;
    } mwinScaleChange;

    // A key going down or up.
    typedef struct mwinKeyEvent
    {
        mwinKeyCode code;
        mwinModifiers modifiers;
        mwinKey key;
        // The platform repeats a held key.
        bool repeat;
    } mwinKeyEvent;

    // Typed or composed text, valid until the frame that drained it
    // returns.
    typedef struct mwinTextEvent
    {
        const char* text;
        uint32_t length;
    } mwinTextEvent;

    // The cursor: where it is, the buttons held, and for a button record
    // the button that changed and the count of quick clicks it completes.
    typedef struct mwinPointerEvent
    {
        mwinPosition position;
        mwinModifiers modifiers;
        // Bit b - 1 is set while button b is held.
        uint8_t buttons;
        mwinMouseButton button;
        uint8_t clicks;
    } mwinPointerEvent;

    // Wheel turns in detents, fractional for smooth wheels and touchpads;
    // positive y is away from the user, positive x to the right.
    typedef struct mwinWheelEvent
    {
        float x;
        float y;
    } mwinWheelEvent;

    // Relative motion in the device's own units, before any acceleration
    // the platform can leave out.
    typedef struct mwinDeltaEvent
    {
        float x;
        float y;
    } mwinDeltaEvent;

    // A touch, by an id stable from its down to its up or cancel.
    typedef struct mwinTouchEvent
    {
        uint64_t id;
        mwinPosition position;
        // From 0 to 1, or -1 where the platform does not measure it.
        float pressure;
    } mwinTouchEvent;

    // The pen's state.
    typedef uint8_t mwinPenFlags;

    enum
    {
        // The eraser end is in use.
        mwin_penEraser = 1,
        // The tip touches the surface; otherwise the pen hovers.
        mwin_penContact = 2,
        // The barrel button is held.
        mwin_penBarrel = 4,
    };

    // A pen, where the platform has one.
    typedef struct mwinPenEvent
    {
        mwinPosition position;
        // From 0 to 1.
        float pressure;
        // Degrees from upright, toward positive x and positive y.
        float tiltX;
        float tiltY;
        mwinPenFlags flags;
        // For a pen button record, the button: 1 for the barrel.
        uint8_t button;
    } mwinPenEvent;

    // One record of the stream.
    typedef struct mwinEvent
    {
        mwinEventType type;
        // How many platform samples the record stands for: 1, or more for
        // motion merged when its storage was full.
        uint16_t samples;
        // The window it is about.
        mwinWindowId window;
        // Nanoseconds on a monotonic clock, from the platform's own event
        // time where it has one.
        uint64_t timeNs;
        union
        {
            mwinSize size;
            mwinPixelSize pixelSize;
            mwinScaleChange scale;
            mwinPosition position;
            mwinWindowMode mode;
            mwinCompletion completion;
            mwinKeyEvent key;
            mwinTextEvent text;
            mwinPointerEvent pointer;
            mwinWheelEvent wheel;
            mwinDeltaEvent delta;
            mwinTouchEvent touch;
            mwinPenEvent pen;
        } data;
    } mwinEvent;

    /// Takes the next record of the stream.
    ///
    /// @param context   The context.
    /// @param eventOut  Receives the record.
    /// @return `mwin_success`; `mwin_empty` when the stream is drained;
    ///         `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinNextEvent(mwinContext* context, mwinEvent* eventOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_EVENT_H
