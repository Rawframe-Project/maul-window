// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend's keeping awake against the test compositor of
// wayland_server.h with an idle inhibit manager: an inhibitor on the
// window's surface while it asks, one only when asked twice, none once
// it no longer asks, and none after the window goes. Skipped (exit
// status 77) without XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_server.h"

#include "maul-window/event.h"
#include "maul-window/services.h"

#include <idle-inhibit-unstable-v1-server-protocol.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define SETTLE_NS   50000000ull

typedef enum Phase
{
    phaseCreate,
    phaseAwake,
    phaseTwice,
    phaseAsleep,
    phaseAgain,
    phaseGone,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinRequestId request;
    int outcome;
    bool timedOut;
} Program;

// The inhibitors the client holds, and whether the last was made on a
// surface.
static atomic_int s_inhibitors;
static atomic_bool s_onSurface;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void InhibitorGone(struct wl_resource* resource)
{
    (void)resource;
    atomic_fetch_sub(&s_inhibitors, 1);
}

static const struct zwp_idle_inhibitor_v1_interface s_inhibitor = {ServerDestroyResource};

static void CreateInhibitor(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                            struct wl_resource* surface)
{
    struct wl_resource* inhibitor = wl_resource_create(client, &zwp_idle_inhibitor_v1_interface,
                                                       wl_resource_get_version(resource), id);
    wl_resource_set_implementation(inhibitor, &s_inhibitor, nullptr, InhibitorGone);
    atomic_store(&s_onSurface, surface != nullptr);
    atomic_fetch_add(&s_inhibitors, 1);
}

static const struct zwp_idle_inhibit_manager_v1_interface s_manager = {ServerDestroyResource,
                                                                       CreateInhibitor};

static void BindManager(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &zwp_idle_inhibit_manager_v1_interface, (int)version, id);
    wl_resource_set_implementation(resource, &s_manager, data, nullptr);
}

static int Held(void)
{
    return atomic_load(&s_inhibitors);
}

static void Ask(mwinContext* context, Program* program, bool awake)
{
    program->outcome = -1;
    CHECK(mwinRequestKeepAwake(context, program->window, awake, &program->request) == mwin_success,
          "ask to keep awake");
}

static bool Ready(const Program* program)
{
    bool done = program->outcome == mwin_outcomeDone;
    bool settled = NowNs() - program->startNs >= SETTLE_NS;
    switch (program->phase)
    {
    case phaseCreate:
        return done;
    case phaseAwake:
    case phaseAgain:
        return done && Held() == 1;
    case phaseTwice:
        return done && settled;
    case phaseAsleep:
    case phaseGone:
        return done && Held() == 0;
    default:
        return false;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseCreate:
        Ask(context, program, true);
        break;
    case phaseAwake:
        CHECK(atomic_load(&s_onSurface), "an inhibitor on the window's surface while it asks");
        Ask(context, program, true);
        break;
    case phaseTwice:
        CHECK(Held() == 1, "one inhibitor only when asked twice");
        Ask(context, program, false);
        break;
    case phaseAsleep:
        CHECK(Held() == 0, "none once the window no longer asks");
        Ask(context, program, true);
        break;
    case phaseAgain:
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success, "destroy");
        // The destroyed window's record comes as the request's would.
        program->outcome = mwin_outcomeDone;
        break;
    default:
        CHECK(Held() == 0, "none after the window goes");
        break;
    }
    program->phase += 1;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startNs = NowNs();
    program->outcome = -1;
    return mwinCreateWindow(context, &def, &program->window, &program->request);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == program->request.index1 &&
            completion->request.generation == program->request.generation)
        {
            program->outcome = completion->outcome;
        }
    }
    if (Ready(program))
    {
        Advance(program, context);
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
    pthread_mutex_lock(&server.lock);
    wl_global_create(server.display, &zwp_idle_inhibit_manager_v1_interface, 1, nullptr,
                     BindManager);
    pthread_mutex_unlock(&server.lock);
    Program program = {0};
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
