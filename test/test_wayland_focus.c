// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Focus requests on Wayland through xdg-activation, against the test
// compositor of wayland_server.h and an activation global of the
// test's own:
// - the first window shown activates itself with the launcher's token,
//   which leaves the environment;
// - a focus request asks for a token with the serial of the latest
//   input, the seat and the surface with the keyboard, and activates the
//   window it was made for with it, done;
// - of two requests in a row, the first is superseded and only the
//   second's token activates.
// Skipped (exit status 77) without XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_server.h"

#include "maul-window/event.h"

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xdg-activation-v1-server-protocol.h>

#define DEADLINE_NS 10000000000ull
#define TOKENS      8

// A token the client asked for: what it was given before its commit.
typedef struct Token
{
    uint32_t serial;
    bool seat;
    struct wl_resource* surface;
} Token;

static Server s_server;
static Token s_tokens[TOKENS];
static int s_made;
// The activations: how many, and the last one's token and surface.
static int s_activations;
static char s_token[32];
static struct wl_resource* s_activated;
// The first surface committed: the first window's.
static struct wl_resource* s_first;

typedef struct Program
{
    uint64_t startNs;
    int step;
    mwinWindowId windows[2];
    mwinRequestId requests[2];
    int outcomes[2];
    bool created;
    bool pressed;
    uint32_t keySerial;
    bool done;
} Program;

static void SetSerial(struct wl_client* client, struct wl_resource* resource, uint32_t serial,
                      struct wl_resource* seat)
{
    (void)client;
    Token* token = wl_resource_get_user_data(resource);
    token->serial = serial;
    token->seat = seat != nullptr;
}

static void SetSurface(struct wl_client* client, struct wl_resource* resource,
                       struct wl_resource* surface)
{
    (void)client;
    Token* token = wl_resource_get_user_data(resource);
    token->surface = surface;
}

static void CommitToken(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    const Token* token = wl_resource_get_user_data(resource);
    char name[32];
    (void)snprintf(name, sizeof(name), "token-%d", (int)(token - s_tokens));
    xdg_activation_token_v1_send_done(resource, name);
}

static const struct xdg_activation_token_v1_interface s_tokenInterface = {
    SetSerial, ServerNoString, SetSurface, CommitToken, ServerDestroyResource};

static void GetToken(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    struct wl_resource* token = wl_resource_create(client, &xdg_activation_token_v1_interface,
                                                   wl_resource_get_version(resource), id);
    Token* data = &s_tokens[s_made < TOKENS ? s_made : TOKENS - 1];
    s_made += 1;
    *data = (Token){0};
    wl_resource_set_implementation(token, &s_tokenInterface, data, nullptr);
}

static void Activate(struct wl_client* client, struct wl_resource* resource, const char* token,
                     struct wl_resource* surface)
{
    (void)client;
    (void)resource;
    s_activations += 1;
    (void)snprintf(s_token, sizeof(s_token), "%s", token);
    s_activated = surface;
}

static const struct xdg_activation_v1_interface s_activation = {ServerDestroyResource, GetToken,
                                                                Activate};

static void BindActivation(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &xdg_activation_v1_interface, (int)version, id);
    wl_resource_set_implementation(resource, &s_activation, data, nullptr);
}

static void OnCommit(struct wl_resource* surface)
{
    s_first = s_first != nullptr ? s_first : surface;
}

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Collect(mwinContext* context, Program* program)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        for (int i = 0; i < 2 && event.type == mwin_eventRequestCompleted; i++)
        {
            bool same = completion->request.index1 == program->requests[i].index1 &&
                        completion->request.generation == program->requests[i].generation;
            program->outcomes[i] = same ? completion->outcome : program->outcomes[i];
        }
        program->created = program->created || event.type == mwin_eventWindowCreated;
        program->pressed = program->pressed || event.type == mwin_eventKeyDown;
    }
}

static bool Named(int token)
{
    char name[32];
    (void)snprintf(name, sizeof(name), "token-%d", token);
    return strcmp(s_token, name) == 0;
}

static void Next(mwinContext* context, Program* program)
{
    pthread_mutex_lock(&s_server.lock);
    struct wl_resource* focused = s_server.surface;
    const Token* last = &s_tokens[s_made > 0 ? s_made - 1 : 0];
    bool first = s_activations == 1 && strcmp(s_token, "launch-1") == 0 && s_activated == s_first;
    bool asked =
        s_made == 1 && last->serial == program->keySerial && last->seat && last->surface == focused;
    bool activated = s_activations == 2 && s_activated == s_first && Named(0);
    bool latest = s_made == 3 && s_activations == 3 && s_activated == s_first && Named(2);
    pthread_mutex_unlock(&s_server.lock);
    switch (program->step)
    {
    case 0:
        CHECK(first && getenv("XDG_ACTIVATION_TOKEN") == nullptr,
              "the first window shown activated with the launcher's token, which leaves");
        ServerEnter(&s_server);
        ServerKey(&s_server, KEY_A, true);
        pthread_mutex_lock(&s_server.lock);
        program->keySerial = s_server.serial;
        pthread_mutex_unlock(&s_server.lock);
        break;
    case 1:
        CHECK(mwinRequestFocus(context, program->windows[0], &program->requests[0]) == mwin_success,
              "focus");
        break;
    case 2:
        CHECK(program->outcomes[0] == mwin_outcomeDone && asked,
              "a token asked for with the input's serial, the seat and the focused surface");
        CHECK(activated, "the window activated with it");
        program->outcomes[0] = -1;
        CHECK(mwinRequestFocus(context, program->windows[0], &program->requests[0]) ==
                      mwin_success &&
                  mwinRequestFocus(context, program->windows[0], &program->requests[1]) ==
                      mwin_success,
              "focus twice");
        break;
    default:
        CHECK(program->outcomes[0] == mwin_outcomeSuperseded &&
                  program->outcomes[1] == mwin_outcomeDone && latest,
              "the first of two superseded, and only the second's token activating");
        program->done = true;
        break;
    }
    program->step += 1;
}

// Whether the step's wait is over: on the program's side, and for the
// activations, on the compositor's.
static bool Ready(const Program* program)
{
    pthread_mutex_lock(&s_server.lock);
    int activations = s_activations;
    pthread_mutex_unlock(&s_server.lock);
    switch (program->step)
    {
    case 0:
        return program->created && activations >= 1;
    case 1:
        return program->pressed;
    case 2:
        return program->outcomes[0] >= 0 && activations >= 2;
    default:
        return program->outcomes[0] >= 0 && program->outcomes[1] >= 0 && activations >= 3;
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startNs = NowNs();
    program->outcomes[0] = -1;
    program->outcomes[1] = -1;
    mwinWindowDef def = mwinDefaultWindowDef();
    // The first is shown first, and the keyboard goes to the second, the
    // last surface the compositor saw made.
    mwinResult first = mwinCreateWindow(context, &def, &program->windows[0], nullptr);
    return first == mwin_success ? mwinCreateWindow(context, &def, &program->windows[1], nullptr)
                                 : first;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(context, program);
    if (!program->done && Ready(program))
    {
        Next(context, program);
    }
    struct timespec pause = {0, 500000};
    (void)nanosleep(&pause, nullptr);
    bool late = NowNs() - program->startNs > DEADLINE_NS;
    return program->done || late ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&s_server, "us", ""))
    {
        return 77;
    }
    pthread_mutex_lock(&s_server.lock);
    wl_global_create(s_server.display, &xdg_activation_v1_interface, 1, nullptr, BindActivation);
    s_server.commit = OnCommit;
    pthread_mutex_unlock(&s_server.lock);
    (void)setenv("XDG_ACTIVATION_TOKEN", "launch-1", 1);
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, "the program runs on the test compositor");
    ServerStop(&s_server);
    return s_failures == 0 ? 0 : 1;
}
