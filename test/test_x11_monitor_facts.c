// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The facts an X11 monitor's output gives, set on the X server's own
// output by the test's connection as a driver would: an EDID with HDR
// static metadata gives the luminances, HDR staying off as X11 never
// shows it; `vrr_capable` gives variable refresh, and turned off it is
// told as a change. The properties are deleted after. Skipped (exit
// status 77) without DISPLAY or RandR 1.5.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/monitor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>

#define DEADLINE_NS 5000000000ull
// The HDR block's code for the most luminance, and what it names.
#define PEAK_CODE 115

typedef enum Phase
{
    phaseStart,
    phaseOff,
    phaseDone,
} Phase;

typedef struct Program
{
    xcb_connection_t* connection;
    xcb_randr_output_t output;
    xcb_atom_t vrr;
    Phase phase;
    uint64_t startNs;
    bool changed;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static xcb_atom_t Intern(xcb_connection_t* connection, const char* name)
{
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
        connection, xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name), nullptr);
    xcb_atom_t atom = reply != nullptr ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

// An EDID of a base block and a CTA-861 extension holding HDR static
// metadata (PQ, the most at PEAK_CODE, the frame average at 90).
static void MakeEdid(uint8_t* bytes)
{
    static const uint8_t header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    static const uint8_t blocks[] = {0xE6, 0x06, 0x05, 0x01, PEAK_CODE, 90, 0};
    memset(bytes, 0, 256);
    memcpy(bytes, header, sizeof(header));
    bytes[126] = 1;
    bytes[128] = 0x02;
    bytes[129] = 3;
    bytes[130] = (uint8_t)(4 + sizeof(blocks));
    memcpy(bytes + 132, blocks, sizeof(blocks));
    for (int b = 0; b < 2; b++)
    {
        uint8_t sum = 0;
        for (int i = 0; i < 127; i++)
        {
            sum = (uint8_t)(sum + bytes[b * 128 + i]);
        }
        bytes[b * 128 + 127] = (uint8_t)(0x100 - sum);
    }
}

static void SetVrr(const Program* program, uint32_t capable)
{
    xcb_randr_change_output_property(program->connection, program->output, program->vrr,
                                     XCB_ATOM_CARDINAL, 32, XCB_PROP_MODE_REPLACE, 1, &capable);
    xcb_flush(program->connection);
}

static bool First(mwinContext* context, mwinMonitorInfo* infoOut)
{
    mwinMonitorId ids[8];
    size_t count = 0;
    return mwinGetMonitors(context, ids, 8, &count) == mwin_success && count > 0 &&
           mwinGetMonitorInfo(context, ids[0], infoOut) == mwin_success;
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
    if (program->phase == phaseStart && First(context, &info))
    {
        float peak = 50.0f * exp2f((float)PEAK_CODE / 32.0f);
        CHECK(info.hdr.known && !info.hdr.active && fabsf(info.hdr.peakNits - peak) < 1.0f &&
                  info.hdr.fullFrameNits > 0.0f && info.hdr.headroom == 1.0f &&
                  info.variableRefresh,
              "the EDID's luminances with HDR off, and variable refresh");
        SetVrr(program, 0);
        program->phase = phaseOff;
        program->startNs = NowNs();
    }
    else if (program->phase == phaseOff && program->changed && First(context, &info) &&
             !info.variableRefresh)
    {
        program->phase = phaseDone;
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 1000000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

// The output of the X server's first monitor, or none.
static xcb_randr_output_t FirstOutput(xcb_connection_t* connection, xcb_window_t root)
{
    xcb_randr_get_monitors_reply_t* reply = xcb_randr_get_monitors_reply(
        connection, xcb_randr_get_monitors(connection, root, 1), nullptr);
    xcb_randr_output_t output = XCB_NONE;
    if (reply != nullptr)
    {
        xcb_randr_monitor_info_iterator_t it = xcb_randr_get_monitors_monitors_iterator(reply);
        if (it.rem > 0 && xcb_randr_monitor_info_outputs_length(it.data) > 0)
        {
            output = xcb_randr_monitor_info_outputs(it.data)[0];
        }
    }
    free(reply);
    return output;
}

int main(void)
{
    const char* display = getenv("DISPLAY");
    if (display == nullptr || display[0] == '\0')
    {
        return 77;
    }
    unsetenv("WAYLAND_DISPLAY");
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    xcb_randr_query_version_reply_t* version = xcb_randr_query_version_reply(
        connection, xcb_randr_query_version(connection, 1, 5), nullptr);
    bool monitors =
        version != nullptr && (version->major_version > 1 || version->minor_version >= 5);
    free(version);
    const xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    xcb_randr_output_t output = xcb_connection_has_error(connection) == 0 && monitors
                                    ? FirstOutput(connection, screen->root)
                                    : XCB_NONE;
    if (output == XCB_NONE)
    {
        xcb_disconnect(connection);
        return 77;
    }
    static Program program;
    program = (Program){
        .connection = connection, .output = output, .vrr = Intern(connection, "vrr_capable")};
    xcb_atom_t edidAtom = Intern(connection, "EDID");
    uint8_t edid[256];
    MakeEdid(edid);
    xcb_randr_change_output_property(connection, output, edidAtom, XCB_ATOM_INTEGER, 8,
                                     XCB_PROP_MODE_REPLACE, sizeof(edid), edid);
    SetVrr(&program, 1);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the X server");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "variable refresh turned off, told as a change");
    // Properties a run left behind would trouble the next.
    xcb_randr_delete_output_property(connection, output, edidAtom);
    xcb_randr_delete_output_property(connection, output, program.vrr);
    xcb_flush(connection);
    xcb_disconnect(connection);
    return s_failures == 0 ? 0 : 1;
}
