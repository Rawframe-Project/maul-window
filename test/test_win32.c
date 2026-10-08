// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend against Windows (a CI runner's desktop, or wine):
// the window a creation makes, the monitors as Windows' own calls
// describe them (bounds, work area, primary, scale, name, refresh), the
// native handles, size and place as Windows reports them, maximizing,
// borderless full screen and back, and the close button's message.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/monitor.h"
#include "maul-window/native.h"

#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <string.h>
#include <windows.h>
// After windows.h, whose types it takes.
#include <shellscalingapi.h>

// Generous: under wine on a loaded machine a phase has taken over 10 s.
#define DEADLINE_MS 30000u

// Windows' monitors, as its own calls list them.
typedef struct Listed
{
    MONITORINFOEXW info[4];
    HMONITOR handles[4];
    int count;
} Listed;

static BOOL CALLBACK List(HMONITOR handle, HDC context, LPRECT rect, LPARAM data)
{
    (void)context;
    (void)rect;
    Listed* listed = (Listed*)data;
    if (listed->count < 4)
    {
        MONITORINFOEXW* info = &listed->info[listed->count];
        info->cbSize = sizeof(*info);
        listed->handles[listed->count] = handle;
        listed->count += GetMonitorInfoW(handle, (MONITORINFO*)info) ? 1 : 0;
    }
    return TRUE;
}

static bool SameRect(mwinPixelRect rect, RECT expected)
{
    return rect.x == expected.left && rect.y == expected.top &&
           rect.width == (uint32_t)(expected.right - expected.left) &&
           rect.height == (uint32_t)(expected.bottom - expected.top);
}

// A monitor as Windows describes it: its bounds, work area, whether it is
// primary, its scale, its name (the display device's description, whole
// where it fits) and its refresh rate (to the hertz Windows' settings
// give; DisplayConfig's may be exact).
static bool Described(const mwinMonitorInfo* info, const MONITORINFOEXW* expected, HMONITOR handle)
{
    UINT dpiX = 96;
    UINT dpiY = 96;
    float scale = GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY) == S_OK
                      ? (float)dpiX / 96.0f
                      : 1.0f;
    DISPLAY_DEVICEW display = {.cb = sizeof(display)};
    char name[MWIN_MONITOR_NAME_BYTES] = {0};
    int bytes = EnumDisplayDevicesW(expected->szDevice, 0, &display, 0)
                    ? WideCharToMultiByte(CP_UTF8, 0, display.DeviceString, -1, name, sizeof(name),
                                          nullptr, nullptr)
                    : 1;
    bool named = bytes == 0 || (info->nameLength == (uint32_t)bytes - 1 &&
                                memcmp(info->name, name, info->nameLength) == 0);
    DEVMODEW mode = {.dmSize = sizeof(mode)};
    int64_t hertz = EnumDisplaySettingsW(expected->szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
                            mode.dmDisplayFrequency > 1
                        ? mode.dmDisplayFrequency
                        : 0;
    int64_t off = (int64_t)info->refreshMilliHz - hertz * 1000;
    bool refresh = hertz == 0 ? true : off > -1000 && off < 1000;
    if (!named || !refresh)
    {
        (void)printf("monitor %.*s %u mHz; Windows: %s %lld Hz\n", (int)info->nameLength,
                     info->name, info->refreshMilliHz, name, (long long)hertz);
    }
    return SameRect(info->bounds, expected->rcMonitor) &&
           SameRect(info->workArea, expected->rcWork) &&
           info->primary == ((expected->dwFlags & MONITORINFOF_PRIMARY) != 0) &&
           info->scale == scale && named && refresh;
}

// Each monitor as Windows lists it, the primary first.
static void CheckMonitors(mwinContext* context)
{
    Listed listed = {0};
    (void)EnumDisplayMonitors(nullptr, nullptr, List, (LPARAM)&listed);
    mwinMonitorId monitors[4];
    size_t count = 0;
    CHECK(mwinGetMonitors(context, monitors, 4, &count) == mwin_success &&
              (int)count == listed.count,
          "as many monitors as Windows lists");
    for (size_t i = 0; i < count; i++)
    {
        mwinMonitorInfo info;
        int found = -1;
        for (int j = 0;
             j < listed.count && mwinGetMonitorInfo(context, monitors[i], &info) == mwin_success;
             j++)
        {
            found = SameRect(info.bounds, listed.info[j].rcMonitor) ? j : found;
        }
        CHECK(found >= 0 && Described(&info, &listed.info[found], listed.handles[found]) &&
                  (i > 0 || info.primary),
              "each monitor as Windows describes it, the primary first");
    }
}

typedef enum Phase
{
    phaseCreate,
    phaseResize,
    phaseMove,
    phaseMaximize,
    phaseFullscreen,
    phaseWindowed,
    phaseClose,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    ULONGLONG startMs;
    mwinWindowId window;
    uint64_t seen;
    mwinSize size;
    mwinPosition position;
    mwinWindowMode mode;
    mwinCompletion completion;
    bool completed;
    // Maximize requests made again after the platform undid one.
    int retries;
    bool timedOut;
} Program;

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->seen |= 1ull << event.type;
        program->size = event.type == mwin_eventResized ? event.data.size : program->size;
        program->position = event.type == mwin_eventMoved ? event.data.position : program->position;
        program->mode = event.type == mwin_eventModeChanged ? event.data.mode : program->mode;
        if (event.type == mwin_eventRequestCompleted)
        {
            program->completion = event.data.completion;
            program->completed = true;
        }
    }
}

static bool Seen(const Program* program, mwinEventType type)
{
    return (program->seen & (1ull << type)) != 0;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return program->completed && Seen(program, mwin_eventShown);
    case phaseResize:
        return program->size.width == 800.0f && program->size.height == 600.0f;
    case phaseMove:
        return program->position.x == 40.0f && program->position.y == 30.0f;
    case phaseMaximize:
        return program->completed && program->mode == mwin_modeMaximized;
    case phaseFullscreen:
        return program->completed && program->mode == mwin_modeBorderlessFullscreen;
    case phaseWindowed:
        return program->completed && program->mode == mwin_modeWindowed;
    default:
        return Seen(program, mwin_eventCloseRequested);
    }
}

// Whether the platform maximized the window and restored it before the
// program looked: the request done, a mode change seen, and the window
// windowed again (the queue keeps only the latest mode). Under wine a
// late answer from the X server to the move before does this now and
// then; Windows itself does not.
static bool MaximizeUndone(const Program* program)
{
    return program->phase == phaseMaximize && program->completed &&
           program->completion.kind == mwin_requestMode && Seen(program, mwin_eventModeChanged) &&
           program->mode == mwin_modeWindowed;
}

static HWND WindowOf(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (HWND)handles.handles.win32.hwnd
               : nullptr;
}

static void CheckCreated(const Program* program, mwinContext* context)
{
    static const mwinEventType expected[] = {mwin_eventWindowCreated, mwin_eventScaleChanged,
                                             mwin_eventResized,       mwin_eventPixelSizeChanged,
                                             mwin_eventModeChanged,   mwin_eventShown};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
    {
        CHECK(Seen(program, expected[i]), "what a creation reports");
    }
    CHECK(program->completion.outcome == mwin_outcomeDone &&
              IsWindow(WindowOf(context, program->window)),
          "the window is made");
    mwinMonitorId monitors[4];
    size_t count = 0;
    mwinMonitorInfo info;
    CHECK(mwinGetMonitors(context, monitors, 4, &count) == mwin_success && count >= 1 &&
              mwinGetMonitorInfo(context, monitors[0], &info) == mwin_success,
          "the monitors");
    CheckMonitors(context);
    mwinWindowState state;
    CHECK(mwinGetWindowState(context, program->window, &state) == mwin_success &&
              state.monitor.index1 == monitors[0].index1 &&
              state.monitor.generation == monitors[0].generation,
          "the window on the primary monitor");
    // The runner has no HDR display: what it tells has the shape every
    // display's facts have.
    const mwinHdrFacts* hdr = &info.hdr;
    (void)printf("hdr: known %d active %d peak %.1f frame %.1f white %.1f headroom %.2f\n",
                 hdr->known, hdr->active, (double)hdr->peakNits, (double)hdr->fullFrameNits,
                 (double)hdr->sdrWhiteNits, (double)hdr->headroom);
    CHECK(hdr->peakNits >= 0.0f && hdr->fullFrameNits >= 0.0f && hdr->sdrWhiteNits >= 0.0f &&
              (hdr->active
                   ? hdr->sdrWhiteNits > 0.0f && (hdr->headroom == 0.0f || hdr->headroom >= 1.0f)
                   : hdr->sdrWhiteNits == 0.0f && hdr->headroom == (hdr->known ? 1.0f : 0.0f)) &&
              !info.variableRefresh,
          "HDR facts in their ranges, variable refresh not known");
    (void)printf("size %u x %u mm, %u mHz\n", info.widthMm, info.heightMm, info.refreshMilliHz);
    CHECK((info.widthMm == 0) == (info.heightMm == 0), "a physical size whole or none");
}

static void Advance(Program* program, mwinContext* context)
{
    mwinWindowId window = program->window;
    switch (program->phase)
    {
    case phaseCreate:
        CheckCreated(program, context);
        CHECK(mwinRequestTitle(context, window, "Retitled", 8, nullptr) == mwin_success &&
                  mwinRequestSize(context, window, (mwinSize){800.0f, 600.0f}, nullptr) ==
                      mwin_success,
              "a title and a size");
        break;
    case phaseResize:
        CHECK(mwinRequestPosition(context, window, (mwinPosition){40.0f, 30.0f}, nullptr) ==
                  mwin_success,
              "a place");
        break;
    case phaseMove:
        CHECK(mwinRequestMode(context, window, mwin_modeMaximized, nullptr) == mwin_success,
              "maximize");
        break;
    case phaseMaximize:
        CHECK(mwinRequestMode(context, window, mwin_modeBorderlessFullscreen, nullptr) ==
                  mwin_success,
              "full screen");
        break;
    case phaseFullscreen:
        CHECK(mwinRequestMode(context, window, mwin_modeWindowed, nullptr) == mwin_success,
              "windowed again");
        break;
    case phaseWindowed:
        CHECK(program->size.width == 800.0f || program->size.width == 0.0f, "the size from before");
        PostMessageW(WindowOf(context, window), WM_CLOSE, 0, 0);
        break;
    default:
        CHECK(mwinDestroyWindow(context, window) == mwin_success, "destroy");
        break;
    }
    program->phase += 1;
    program->seen = 0;
    program->completed = false;
    program->startMs = GetTickCount64();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul Win32";
    def.titleLength = 10;
    def.size = (mwinSize){640.0f, 480.0f};
    program->startMs = GetTickCount64();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (MaximizeUndone(program) && program->retries < 3)
    {
        program->retries += 1;
        (void)printf("the platform undid the maximize; asking again\n");
        program->seen = 0;
        program->completed = false;
        CHECK(mwinRequestMode(context, program->window, mwin_modeMaximized, nullptr) ==
                  mwin_success,
              "maximize again");
    }
    else if (GetTickCount64() - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out in phase %d: mode %d, %s (kind %d, outcome %d), %gx%g at %g,%g, "
                     "zoomed %d, mode changes seen %d\n",
                     (int)program->phase, (int)program->mode,
                     program->completed ? "a request completed" : "no request completed",
                     (int)program->completion.kind, (int)program->completion.outcome,
                     (double)program->size.width, (double)program->size.height,
                     (double)program->position.x, (double)program->position.y,
                     (int)IsZoomed(WindowOf(context, program->window)),
                     (int)Seen(program, mwin_eventModeChanged));
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        Sleep(1);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    Program program = {0};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on Windows");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
