// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test backend, a headless platform for contract tests. It exists
// only in builds with MAUL_WINDOW_TEST_BACKEND and only in a context
// created with mwin_backendTest; it is never a fallback. It answers each
// request at the next pump, as the answer set for its kind says, and
// takes what a platform would report from mwinTestPost, which sends it
// through the same path a real backend's reports take.

#ifndef MAUL_WINDOW_TEST_H
#define MAUL_WINDOW_TEST_H

#include "maul-window/event.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Sets how the test platform answers requests of a kind from now on.
    /// mwin_outcomeDone, the default, carries a request out at the next
    /// pump.
    ///
    /// @param context  A context of the test backend.
    /// @param kind     The kind of request.
    /// @param outcome  The answer: any mwin_outcome value but superseded
    ///                 and cancelled, which only the core gives.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL context, an
    ///         unknown kind or an answer the platform cannot give.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestSetAnswer(mwinContext* context, mwinRequestKind kind,
                                                         mwinOutcome outcome);

    /// Holds requests: while held, none is answered.
    ///
    /// @param context  A context of the test backend.
    /// @param hold     true to hold, false to answer again from the next
    ///                 pump.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestHold(mwinContext* context, bool hold);

    /// Reports what the platform would: the user resized or closed a
    /// window, focus moved, the scale changed, a key went down, text was
    /// typed. The report reaches the stream at the next pump, stamped with
    /// the time of this call; text is copied from the record's pointer.
    /// Completions, input state resets and the created and destroyed
    /// records are the core's and refused.
    ///
    /// @param context  A context of the test backend.
    /// @param event    The notification; its window must be live.
    /// @return `mwin_success`; `mwin_errorStale` for a window that no
    ///         longer exists; `mwin_errorCapacity` when 1,024 reports or
    ///         64 KiB of their text already wait; `mwin_errorUnsupported`
    ///         for a context of another backend; `mwin_errorInvalid` for a
    ///         NULL argument, a
    ///         record type only the core makes, or text that is not UTF-8.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestPost(mwinContext* context, const mwinEvent* event);

    /// Sets the test platform's clock, which stamps every record.
    ///
    /// @param context  A context of the test backend.
    /// @param timeNs   Nanoseconds; it may not go back.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL context or
    ///         a time before the current one.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestSetTime(mwinContext* context, uint64_t timeNs);

    /// Sets the scale the test platform gives windows it creates or
    /// resizes from now on; 1 at first.
    ///
    /// @param context  A context of the test backend.
    /// @param scale    A positive, finite scale.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL context or a
    ///         scale that is not positive and finite.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestSetScale(mwinContext* context, float scale);

    /// Reads the title the test platform shows for a window.
    ///
    /// @param context    A context of the test backend.
    /// @param window     The window.
    /// @param buffer     Receives the title, not NUL-terminated. May be
    ///                   NULL when capacity is 0.
    /// @param capacity   The bytes buffer holds.
    /// @param lengthOut  Receives the title's length in bytes.
    /// @return `mwin_success`; `mwin_errorCapacity` when the title does not
    ///         fit (the bytes that fit are written); `mwin_errorStale` for
    ///         a window that no longer exists; `mwin_errorUnsupported` for
    ///         a context of another backend; `mwin_errorInvalid` for a NULL
    ///         argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestGetTitle(const mwinContext* context,
                                                        mwinWindowId window, char* buffer,
                                                        size_t capacity, size_t* lengthOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_TEST_H
