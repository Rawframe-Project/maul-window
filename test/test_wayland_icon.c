// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend's window icons against the test compositor of
// wayland_server.h with a toplevel icon manager: every image in a
// square shared-memory buffer of scale 1, one not square centred on a
// clear square, its alpha premultiplied; no buffer destroyed before its
// icon; none sets no icon. Skipped (exit status 77) without
// XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_server.h"

#include "maul-window/event.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xdg-toplevel-icon-v1-server-protocol.h>

#define DEADLINE_NS 5000000000ull

// An icon the client made: its buffers, each watched so one destroyed
// before the icon is seen.
typedef struct Entry
{
    struct wl_listener gone;
    struct FakeIcon* icon;
    struct wl_resource* buffer;
    int32_t scale;
} Entry;

typedef struct FakeIcon
{
    Entry entries[4];
    int count;
} FakeIcon;

// What the compositor was given: set_icon's calls, the buffers of the
// last icon set (0 for none), their sides, scales and first pixels, and
// whether a buffer went before its icon. Under the server's lock.
typedef struct Icons
{
    int sets;
    int count;
    int32_t sides[4];
    int32_t scales[4];
    uint32_t pixels[4][9];
    bool early;
} Icons;

static Server s_server;
static Icons s_icons;

typedef struct Program
{
    int step;
    uint64_t startNs;
    uint64_t stepNs;
    mwinWindowId window;
    mwinRequestId request;
    bool answered;
    bool done;
} Program;

// 3 by 1 and 1 by 2, RGBA with straight alpha.
static const uint8_t s_wide[] = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0};
static const uint8_t s_dot[] = {1, 2, 3, 4, 1, 2, 3, 4};

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void BufferGone(struct wl_listener* listener, void* data)
{
    (void)data;
    Entry* entry = wl_container_of(listener, entry, gone);
    s_icons.early = true;
    wl_list_remove(&entry->gone.link);
    entry->buffer = nullptr;
}

static void AddBuffer(struct wl_client* client, struct wl_resource* resource,
                      struct wl_resource* buffer, int32_t scale)
{
    (void)client;
    FakeIcon* icon = wl_resource_get_user_data(resource);
    if (icon->count < 4)
    {
        Entry* entry = &icon->entries[icon->count++];
        *entry = (Entry){.icon = icon, .buffer = buffer, .scale = scale};
        entry->gone.notify = BufferGone;
        wl_resource_add_destroy_listener(buffer, &entry->gone);
    }
}

static void NoName(struct wl_client* client, struct wl_resource* resource, const char* name)
{
    (void)client;
    (void)resource;
    (void)name;
}

static const struct xdg_toplevel_icon_v1_interface s_icon = {ServerDestroyResource, NoName,
                                                             AddBuffer};

// The icon goes: its buffers are no longer watched.
static void IconGone(struct wl_resource* resource)
{
    FakeIcon* icon = wl_resource_get_user_data(resource);
    for (int i = 0; i < icon->count; i++)
    {
        if (icon->entries[i].buffer != nullptr)
        {
            wl_list_remove(&icon->entries[i].gone.link);
        }
    }
    free(icon);
}

static void CreateIcon(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    struct wl_resource* icon = wl_resource_create(client, &xdg_toplevel_icon_v1_interface,
                                                  wl_resource_get_version(resource), id);
    wl_resource_set_implementation(icon, &s_icon, calloc(1, sizeof(FakeIcon)), IconGone);
}

// Notes an icon's buffers as they are when set.
static void SetIcon(struct wl_client* client, struct wl_resource* resource,
                    struct wl_resource* toplevel, struct wl_resource* resourceIcon)
{
    (void)client;
    (void)resource;
    (void)toplevel;
    const FakeIcon* icon =
        resourceIcon != nullptr ? wl_resource_get_user_data(resourceIcon) : nullptr;
    s_icons.sets += 1;
    s_icons.count = icon != nullptr ? icon->count : 0;
    for (int i = 0; i < s_icons.count; i++)
    {
        struct wl_shm_buffer* buffer = wl_shm_buffer_get(icon->entries[i].buffer);
        int32_t width = wl_shm_buffer_get_width(buffer);
        s_icons.sides[i] = width == wl_shm_buffer_get_height(buffer) ? width : -1;
        s_icons.scales[i] = icon->entries[i].scale;
        wl_shm_buffer_begin_access(buffer);
        const uint32_t* data = wl_shm_buffer_get_data(buffer);
        int32_t stride = wl_shm_buffer_get_stride(buffer) / 4;
        for (int p = 0; p < 9 && p < width * width; p++)
        {
            s_icons.pixels[i][p] = data[(p / width) * stride + p % width];
        }
        wl_shm_buffer_end_access(buffer);
    }
}

static const struct xdg_toplevel_icon_manager_v1_interface s_manager = {ServerDestroyResource,
                                                                        CreateIcon, SetIcon};

static void BindManager(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &xdg_toplevel_icon_manager_v1_interface, (int)version, id);
    wl_resource_set_implementation(resource, &s_manager, data, nullptr);
    xdg_toplevel_icon_manager_v1_send_done(resource);
}

static Icons Given(void)
{
    pthread_mutex_lock(&s_server.lock);
    Icons icons = s_icons;
    pthread_mutex_unlock(&s_server.lock);
    return icons;
}

// Whether the step's icon reached the compositor yet.
static bool Settled(const Program* program)
{
    Icons icons = Given();
    if (program->step == 1)
    {
        return icons.sets == 1;
    }
    return icons.sets == 2;
}

static void Check(const Program* program)
{
    Icons icons = Given();
    if (program->step == 1)
    {
        // The row in the middle of a clear 3 by 3 square, premultiplied:
        // half green is 0x80 of green at alpha 0x80, clear blue is 0; the
        // dots (1, 2, 3, 4) round to black at alpha 4, down the left of a 2 by 2
        // square.
        static const uint32_t square[9] = {0, 0, 0, 0xFFFF0000u, 0x80008000u, 0, 0, 0, 0};
        CHECK(icons.count == 2 && icons.sides[0] == 3 && icons.sides[1] == 2 &&
                  icons.scales[0] == 1 && icons.scales[1] == 1 &&
                  memcmp(icons.pixels[0], square, sizeof(square)) == 0 &&
                  icons.pixels[1][0] == 0x04000000u && icons.pixels[1][1] == 0 &&
                  icons.pixels[1][2] == 0x04000000u && icons.pixels[1][3] == 0,
              "each image square, of scale 1, centred and premultiplied");
    }
    else
    {
        CHECK(icons.count == 0 && !icons.early,
              "none sets no icon; no buffer destroyed before its icon");
    }
}

static void Next(mwinContext* context, Program* program)
{
    if (program->step == 0)
    {
        mwinIconImage images[2] = {{3, 1, 12, s_wide}, {1, 2, 4, s_dot}};
        CHECK(mwinRequestIcon(context, program->window, images, 2, &program->request) ==
                  mwin_success,
              "two images");
    }
    else if (program->step == 1)
    {
        CHECK(mwinRequestIcon(context, program->window, nullptr, 0, &program->request) ==
                  mwin_success,
              "none");
    }
    program->step += 1;
    program->answered = false;
    program->stepNs = NowNs();
}

static int Outcome(mwinContext* context, mwinRequestId request)
{
    mwinEvent event;
    int outcome = -1;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == request.index1 &&
            completion->request.generation == request.generation)
        {
            outcome = completion->outcome;
        }
    }
    return outcome;
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startNs = NowNs();
    mwinWindowDef def = mwinDefaultWindowDef();
    return mwinCreateWindow(context, &def, &program->window, &program->request);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    program->answered = program->answered || Outcome(context, program->request) == mwin_outcomeDone;
    if (program->answered && program->step == 0)
    {
        Next(context, program);
    }
    else if (program->answered && program->step < 3)
    {
        bool settled = Settled(program);
        if (settled || NowNs() - program->stepNs > 2000000000u)
        {
            CHECK(settled, "the icon reached the compositor");
            Check(program);
            Next(context, program);
        }
    }
    program->done = program->step == 3;
    struct timespec pause = {0, 500000};
    (void)nanosleep(&pause, nullptr);
    return program->done || NowNs() - program->startNs > DEADLINE_NS ? mwin_frameStop
                                                                     : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&s_server, "us", ""))
    {
        return 77;
    }
    pthread_mutex_lock(&s_server.lock);
    wl_global_create(s_server.display, &xdg_toplevel_icon_manager_v1_interface, 1, nullptr,
                     BindManager);
    pthread_mutex_unlock(&s_server.lock);
    Program program = {0};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, "the program runs on the test compositor");
    ServerStop(&s_server);
    return s_failures == 0 ? 0 : 1;
}
