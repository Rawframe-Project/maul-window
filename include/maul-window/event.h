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

#ifndef MAUL_WINDOW_EVENT_H
#define MAUL_WINDOW_EVENT_H

#include "maul-window/window.h"

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

    // One record of the stream.
    typedef struct mwinEvent
    {
        mwinEventType type;
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
