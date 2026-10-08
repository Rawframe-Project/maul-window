// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A monitor's HDR facts on the Wayland backend, against the test
// compositor of wayland_server.h with an output of
// wayland_output_server.h and the color manager of
// wayland_color_server.h: an output in HDR (PQ) has its facts from the
// first frame; switched to SDR it changes, its headroom 1; in HDR
// without a peak its headroom is unknown; a description that fails
// leaves the facts unknown. Skipped (exit status 77) without
// XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_color_server.h"
#include "wayland_output_server.h"

#include "maul-window/event.h"
#include "maul-window/monitor.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull

typedef enum Phase
{
    phaseStart,
    phaseSdr,
    phaseNoPeak,
    phaseFailed,
    phaseDone,
} Phase;

typedef struct Program
{
    Server* server;
    OutputServer output;
    ColorServer color;
    Phase phase;
    uint64_t startNs;
    bool changed;
    bool timedOut;
} Program;

static const ColorDescription s_hdr = {
    .transfer = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ,
    .referenceNits = 203,
    .targetMaxNits = 1000,
    .maxCll = 1000,
    .maxFall = 400,
};

static const ColorDescription s_sdr = {
    .transfer = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB,
    .referenceNits = 80,
    .targetMaxNits = 80,
};

// HDR output whose peak the compositor does not tell.
static const ColorDescription s_noPeak = {
    .transfer = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ,
    .referenceNits = 203,
};

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static bool First(const mwinContext* context, mwinMonitorInfo* infoOut)
{
    mwinMonitorId monitors[4];
    size_t count = 0;
    return mwinGetMonitors(context, monitors, 4, &count) == mwin_success && count == 1 &&
           mwinGetMonitorInfo(context, monitors[0], infoOut) == mwin_success;
}

static void Next(Program* program, Phase phase)
{
    program->phase = phase;
    program->changed = false;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)context;
    Program* program = user;
    program->startNs = NowNs();
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->changed = program->changed || event.type == mwin_eventMonitorChanged;
    }
    mwinMonitorInfo info;
    const mwinHdrFacts* hdr = &info.hdr;
    if (program->phase == phaseStart)
    {
        CHECK(First(context, &info) && hdr->known && hdr->active && hdr->peakNits == 1000.0f &&
                  hdr->fullFrameNits == 400.0f && hdr->sdrWhiteNits == 203.0f &&
                  fabsf(hdr->headroom - 1000.0f / 203.0f) < 0.001f && !info.variableRefresh,
              "an output in HDR has its facts from the first frame");
        ColorChange(&program->color, s_sdr);
        Next(program, phaseSdr);
    }
    else if (program->phase == phaseSdr && program->changed)
    {
        CHECK(First(context, &info) && hdr->known && !hdr->active && hdr->peakNits == 80.0f &&
                  hdr->fullFrameNits == 0.0f && hdr->sdrWhiteNits == 80.0f && hdr->headroom == 1.0f,
              "switched to SDR, the monitor changes, its headroom 1");
        ColorChange(&program->color, s_noPeak);
        Next(program, phaseNoPeak);
    }
    else if (program->phase == phaseNoPeak && program->changed)
    {
        CHECK(First(context, &info) && hdr->known && hdr->active && hdr->peakNits == 0.0f &&
                  hdr->sdrWhiteNits == 203.0f && hdr->headroom == 0.0f,
              "HDR without a peak: its headroom unknown");
        ColorChange(&program->color, (ColorDescription){.fails = true});
        Next(program, phaseFailed);
    }
    else if (program->phase == phaseFailed && program->changed)
    {
        CHECK(First(context, &info) && !hdr->known && !hdr->active && hdr->peakNits == 0.0f &&
                  hdr->sdrWhiteNits == 0.0f && hdr->headroom == 0.0f,
              "a description that fails leaves the facts unknown");
        Next(program, phaseDone);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 500000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    static Server server;
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&server, "us", ""))
    {
        return 77;
    }
    static Program program;
    program = (Program){.server = &server};
    OutputAdd(&program.output, &server, "TEST-1", 0, 0, 1920, 1080, 1);
    ColorAdd(&program.color, &server, s_hdr);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the test compositor");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    ServerStop(&server);
    return s_failures == 0 ? 0 : 1;
}
