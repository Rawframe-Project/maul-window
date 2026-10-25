// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend against Windows (a CI runner's desktop, or wine):
// the window a creation makes, the monitors as Windows' own calls
// describe them (bounds, work area, primary, scale, name, refresh), the
// native handles, size and place as Windows reports them, maximizing,
// borderless full screen and back, a change of DPI, the close button's
// message, and the process made per-monitor DPI aware.
//
// The runner has one monitor, so the window cannot move to one of
// another DPI: the test sends WM_DPICHANGED as Windows would, with
// twice the window's DPI and the rectangle that keeps its logical size,
// from a timer the pump dispatches, as Windows sends it while the pump
// runs. The window is made small and undecorated first: twice its pixels
// fit the screen, and with no frame the window's real DPI, which the
// message does not change, frames nothing differently from the new one.
// "A DPI change answered within the pump cycle" reads here as: the scale
// record carries the logical size as the one suggested, the backend has
// taken Windows' suggested rectangle by the time the program sees the
// record, and a size the program requests on the record, before the next
// pump, is the one the window keeps.

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
// How long the program's answer to a DPI change must stand: long enough
// for a rectangle the backend applied a pump or a timer later to land.
#define SETTLE_MS 250u
// The size the program asks for on the scale record, larger than the
// window's in both directions.
#define ANSWER_WIDTH  450.0f
#define ANSWER_HEIGHT 320.0f

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
    phaseShrink,
    phaseDpi,
    phaseAnswer,
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
    mwinScaleChange scale;
    mwinPixelSize pixels;
    // The rectangle sent with WM_DPICHANGED, and the window sent it.
    RECT suggested;
    HWND hwnd;
    UINT dpi;
    // The client area's pixels before it.
    RECT client;
    mwinCompletion completion;
    bool completed;
    // Maximize requests made again after the platform undid one.
    int retries;
    bool timedOut;
    // The DPI change would make the window larger than Windows allows.
    bool skipped;
} Program;

// The program whose window the DPI timer sends WM_DPICHANGED to.
static Program* s_program;

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->seen |= 1ull << event.type;
        program->size = event.type == mwin_eventResized ? event.data.size : program->size;
        program->position = event.type == mwin_eventMoved ? event.data.position : program->position;
        program->mode = event.type == mwin_eventModeChanged ? event.data.mode : program->mode;
        program->scale = event.type == mwin_eventScaleChanged ? event.data.scale : program->scale;
        program->pixels =
            event.type == mwin_eventPixelSizeChanged ? event.data.pixelSize : program->pixels;
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
    case phaseShrink:
        return program->size.width == 400.0f && program->size.height == 300.0f;
    case phaseDpi:
        return Seen(program, mwin_eventScaleChanged);
    case phaseAnswer:
        return GetTickCount64() - program->startMs >= SETTLE_MS;
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
    CHECK(GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext()) ==
              DPI_AWARENESS_PER_MONITOR_AWARE,
          "the process per-monitor DPI aware");
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

// The window's size around a client area of a size, at a DPI.
static RECT Framed(HWND hwnd, LONG width, LONG height, UINT dpi)
{
    RECT rect = {0, 0, width, height};
    CHECK(AdjustWindowRectExForDpi(&rect, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE,
                                   (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE), dpi),
          "the frame around a client area");
    return rect;
}

// Whether Windows lets the window be as large as the DPI change makes it:
// the program's answer at twice the DPI, larger than the suggested size.
// At the runner's 96 DPI it is; from about 120 it is not.
static bool Fits(HWND hwnd)
{
    UINT dpi = GetDpiForWindow(hwnd);
    float scale = (float)dpi * 2.0f / 96.0f;
    RECT rect = Framed(hwnd, (LONG)(ANSWER_WIDTH * scale + 0.5f),
                       (LONG)(ANSWER_HEIGHT * scale + 0.5f), dpi);
    int width = GetSystemMetricsForDpi(SM_CXMAXTRACK, dpi);
    int height = GetSystemMetricsForDpi(SM_CYMAXTRACK, dpi);
    if (rect.right - rect.left > width || rect.bottom - rect.top > height)
    {
        (void)printf("skipped: at %u DPI the window would be %ld x %ld, more than the %d x %d "
                     "Windows allows\n",
                     dpi, rect.right - rect.left, rect.bottom - rect.top, width, height);
        return false;
    }
    return true;
}

static void CALLBACK SendDpi(HWND unused, UINT message, UINT_PTR timer, DWORD time)
{
    (void)unused;
    (void)message;
    (void)time;
    KillTimer(nullptr, timer);
    Program* program = s_program;
    (void)SendMessageW(program->hwnd, WM_DPICHANGED, MAKEWPARAM(program->dpi, program->dpi),
                       (LPARAM)&program->suggested);
}

// Has the pump send the window WM_DPICHANGED for twice its DPI, with the
// rectangle Windows would suggest: the same place, the client area twice
// as many pixels, framed as Windows frames the window.
static void ChangeDpi(Program* program, HWND hwnd)
{
    UINT dpi = GetDpiForWindow(hwnd);
    RECT window;
    CHECK(GetWindowRect(hwnd, &window) && GetClientRect(hwnd, &program->client),
          "the window's rectangles");
    program->dpi = dpi * 2;
    // Windows frames it at the new DPI; at the current one here, the
    // window's real DPI, which this message leaves as it is.
    RECT suggested = Framed(hwnd, program->client.right * 2, program->client.bottom * 2, dpi);
    OffsetRect(&suggested, window.left - suggested.left, window.top - suggested.top);
    program->suggested = suggested;
    program->hwnd = hwnd;
    s_program = program;
    CHECK(SetTimer(nullptr, 0, USER_TIMER_MINIMUM, SendDpi) != 0, "a timer for the DPI change");
}

// What the program reads on the scale record, before the next pump.
static void CheckDpi(const Program* program, mwinContext* context)
{
    HWND hwnd = WindowOf(context, program->window);
    float scale = (float)program->dpi / 96.0f;
    LONG width = program->client.right * 2;
    LONG height = program->client.bottom * 2;
    CHECK(program->scale.scale == scale && program->scale.suggestedSize.width == 400.0f &&
              program->scale.suggestedSize.height == 300.0f,
          "the scale record: the new scale, the logical size suggested");
    CHECK(Seen(program, mwin_eventPixelSizeChanged), "the pixel size record with the scale record");
    RECT window;
    RECT client;
    CHECK(GetWindowRect(hwnd, &window) && EqualRect(&window, &program->suggested),
          "the window takes the rectangle Windows suggested");
    CHECK(GetClientRect(hwnd, &client) && client.right == width && client.bottom == height &&
              program->pixels.width == (uint32_t)width &&
              program->pixels.height == (uint32_t)height,
          "the client area twice as many pixels, as Windows and the record say");
    mwinWindowState state;
    CHECK(mwinGetWindowState(context, program->window, &state) == mwin_success &&
              state.scale == scale && state.size.width == 400.0f && state.size.height == 300.0f,
          "the logical size kept at the new scale");
}

// The size the program asked for on the scale record is the one the
// window keeps, as Windows and the records say.
static void CheckAnswer(const Program* program, mwinContext* context)
{
    float scale = (float)program->dpi / 96.0f;
    LONG width = (LONG)(ANSWER_WIDTH * scale + 0.5f);
    LONG height = (LONG)(ANSWER_HEIGHT * scale + 0.5f);
    RECT client;
    CHECK(GetClientRect(WindowOf(context, program->window), &client) && client.right == width &&
              client.bottom == height && program->pixels.width == (uint32_t)width &&
              program->pixels.height == (uint32_t)height,
          "the client area the answer's pixels, as Windows and the record say");
    mwinWindowState state;
    CHECK(mwinGetWindowState(context, program->window, &state) == mwin_success &&
              state.scale == scale && state.size.width == ANSWER_WIDTH &&
              state.size.height == ANSWER_HEIGHT && program->size.width == ANSWER_WIDTH &&
              program->size.height == ANSWER_HEIGHT,
          "the answer's logical size, as the state and the record say");
}

static void Advance(Program* program, mwinContext* context)
{
    mwinWindowId window = program->window;
    Phase next = program->phase + 1;
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
        CHECK(mwinRequestStyle(context, window, 0, nullptr) == mwin_success &&
                  mwinRequestSize(context, window, (mwinSize){400.0f, 300.0f}, nullptr) ==
                      mwin_success,
              "undecorated, and a smaller size");
        break;
    case phaseShrink:
        if (Fits(WindowOf(context, window)))
        {
            ChangeDpi(program, WindowOf(context, window));
            break;
        }
        program->skipped = true;
        PostMessageW(WindowOf(context, window), WM_CLOSE, 0, 0);
        next = phaseClose;
        break;
    case phaseDpi:
        CheckDpi(program, context);
        CHECK(mwinRequestSize(context, window, (mwinSize){ANSWER_WIDTH, ANSWER_HEIGHT}, nullptr) ==
                  mwin_success,
              "another size, on the scale record");
        break;
    case phaseAnswer:
        CheckAnswer(program, context);
        PostMessageW(WindowOf(context, window), WM_CLOSE, 0, 0);
        break;
    default:
        CHECK(mwinDestroyWindow(context, window) == mwin_success, "destroy");
        break;
    }
    program->phase = next;
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
    return s_failures != 0 ? 1 : program.skipped ? 77 : 0;
}
