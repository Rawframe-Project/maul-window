// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A wl_output for the test compositor of wayland_server.h, which a test
// adds, changes and removes while the client runs, as a monitor plugged
// in, set to another mode and unplugged. It sends its geometry, current
// mode, scale and name (wl_output 4) to every client that binds it, and
// sends them again when it changes; it tells the window's surface when
// the surface is shown on it.

#ifndef MAUL_WINDOW_TEST_WAYLAND_OUTPUT_SERVER_H
#define MAUL_WINDOW_TEST_WAYLAND_OUTPUT_SERVER_H

#include "wayland_server.h"

#define OUTPUT_RESOURCES 4

typedef struct OutputServer
{
    Server* server;
    struct wl_global* global;
    struct wl_resource* resources[OUTPUT_RESOURCES];
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t scale;
    int32_t widthMm;
    int32_t heightMm;
    int32_t refresh;
    const char* name;
} OutputServer;

static inline void OutputSend(const OutputServer* output, struct wl_resource* resource)
{
    wl_output_send_geometry(resource, output->x, output->y, output->widthMm, output->heightMm,
                            WL_OUTPUT_SUBPIXEL_UNKNOWN, "Maul", "Test", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT, output->width, output->height,
                        output->refresh);
    wl_output_send_scale(resource, output->scale);
    wl_output_send_name(resource, output->name);
    wl_output_send_description(resource, "A test monitor");
    wl_output_send_done(resource);
}

static inline void OutputForget(struct wl_resource* resource)
{
    OutputServer* output = wl_resource_get_user_data(resource);
    for (int i = 0; i < OUTPUT_RESOURCES; i++)
    {
        if (output->resources[i] == resource)
        {
            output->resources[i] = nullptr;
        }
    }
}

static const struct wl_output_interface s_outputServer = {ServerDestroyResource};

static inline void OutputBind(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    OutputServer* output = data;
    struct wl_resource* resource =
        wl_resource_create(client, &wl_output_interface, (int)(version < 4 ? version : 4), id);
    wl_resource_set_implementation(resource, &s_outputServer, output, OutputForget);
    for (int i = 0; i < OUTPUT_RESOURCES; i++)
    {
        if (output->resources[i] == nullptr)
        {
            output->resources[i] = resource;
            break;
        }
    }
    OutputSend(output, resource);
}

// Plugs the monitor in: a wl_output global the client's registry
// announces.
static inline void OutputAdd(OutputServer* output, Server* server, const char* name, int32_t x,
                             int32_t y, int32_t width, int32_t height, int32_t scale)
{
    pthread_mutex_lock(&server->lock);
    *output = (OutputServer){.server = server,
                             .x = x,
                             .y = y,
                             .width = width,
                             .height = height,
                             .scale = scale,
                             .widthMm = 300,
                             .heightMm = 200,
                             .refresh = 60000,
                             .name = name};
    output->global = wl_global_create(server->display, &wl_output_interface, 4, output, OutputBind);
    pthread_mutex_unlock(&server->lock);
}

// Sets another mode and scale, sent to every client bound to it.
static inline void OutputChange(OutputServer* output, int32_t width, int32_t height, int32_t scale)
{
    pthread_mutex_lock(&output->server->lock);
    output->width = width;
    output->height = height;
    output->scale = scale;
    for (int i = 0; i < OUTPUT_RESOURCES; i++)
    {
        if (output->resources[i] != nullptr)
        {
            OutputSend(output, output->resources[i]);
        }
    }
    pthread_mutex_unlock(&output->server->lock);
}

// Sets another physical size, refresh rate and scale, which a test
// makes ones out of their range.
static inline void OutputChangeFacts(OutputServer* output, int32_t widthMm, int32_t heightMm,
                                     int32_t refresh, int32_t scale)
{
    pthread_mutex_lock(&output->server->lock);
    output->widthMm = widthMm;
    output->heightMm = heightMm;
    output->refresh = refresh;
    output->scale = scale;
    for (int i = 0; i < OUTPUT_RESOURCES; i++)
    {
        if (output->resources[i] != nullptr)
        {
            OutputSend(output, output->resources[i]);
        }
    }
    pthread_mutex_unlock(&output->server->lock);
}

// Tells the window's surface it is shown on the monitor.
static inline void OutputEnter(OutputServer* output)
{
    Server* server = output->server;
    pthread_mutex_lock(&server->lock);
    struct wl_client* client = wl_resource_get_client(server->surface);
    for (int i = 0; i < OUTPUT_RESOURCES; i++)
    {
        if (output->resources[i] != nullptr &&
            wl_resource_get_client(output->resources[i]) == client)
        {
            wl_surface_send_enter(server->surface, output->resources[i]);
        }
    }
    pthread_mutex_unlock(&server->lock);
}

// Unplugs it: the registry announces the global's removal. The global
// itself goes with the display, so that a client's late bind still
// finds it.
static inline void OutputRemove(OutputServer* output)
{
    pthread_mutex_lock(&output->server->lock);
    wl_global_remove(output->global);
    pthread_mutex_unlock(&output->server->lock);
}

#endif // MAUL_WINDOW_TEST_WAYLAND_OUTPUT_SERVER_H
