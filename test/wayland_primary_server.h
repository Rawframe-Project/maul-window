// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The primary selection device of the test compositor
// (wayland_server.h), for the clipboard data test: the selection the
// client sets, with the types its source offers; text another client
// would offer, which the compositor writes into the client's pipe from
// a thread of its own (wayland_data_server.h's writer); and the client's
// selection read as another client would.

#ifndef MAUL_WINDOW_TEST_WAYLAND_PRIMARY_SERVER_H
#define MAUL_WINDOW_TEST_WAYLAND_PRIMARY_SERVER_H

#include "wayland_data_server.h"

#include <primary-selection-unstable-v1-server-protocol.h>

typedef struct PrimaryServer PrimaryServer;

// The types a source offers.
typedef struct PrimaryTypes
{
    PrimaryServer* primary;
    char types[DATA_TYPES][DATA_TYPE_BYTES];
    int count;
} PrimaryTypes;

struct PrimaryServer
{
    Server* server;
    struct wl_resource* device;
    // The client's source that holds the selection, or NULL.
    struct wl_resource* source;
    // The text the compositor offers as another client's.
    char* offered;
    size_t offeredLength;
};

static inline void PrimarySourceOffer(struct wl_client* client, struct wl_resource* resource,
                                      const char* type)
{
    (void)client;
    PrimaryTypes* types = wl_resource_get_user_data(resource);
    if (types->count < DATA_TYPES && strlen(type) < DATA_TYPE_BYTES)
    {
        strcpy(types->types[types->count++], type);
    }
}

static const struct zwp_primary_selection_source_v1_interface s_primarySource = {
    PrimarySourceOffer,
    ServerDestroyResource,
};

// A source going away stops holding the selection.
static inline void PrimarySourceGone(struct wl_resource* resource)
{
    PrimaryTypes* types = wl_resource_get_user_data(resource);
    if (types->primary->source == resource)
    {
        types->primary->source = nullptr;
    }
    free(types);
}

static inline void PrimaryCreateSource(struct wl_client* client, struct wl_resource* resource,
                                       uint32_t id)
{
    struct wl_resource* source = wl_resource_create(
        client, &zwp_primary_selection_source_v1_interface, wl_resource_get_version(resource), id);
    PrimaryTypes* types = calloc(1, sizeof(PrimaryTypes));
    types->primary = wl_resource_get_user_data(resource);
    wl_resource_set_implementation(source, &s_primarySource, types, PrimarySourceGone);
}

static inline void PrimarySetSelection(struct wl_client* client, struct wl_resource* resource,
                                       struct wl_resource* source, uint32_t serial)
{
    (void)client;
    (void)serial;
    PrimaryServer* primary = wl_resource_get_user_data(resource);
    primary->source = source;
}

static const struct zwp_primary_selection_device_v1_interface s_primaryDevice = {
    PrimarySetSelection,
    ServerDestroyResource,
};

static inline void PrimaryGetDevice(struct wl_client* client, struct wl_resource* resource,
                                    uint32_t id, struct wl_resource* seat)
{
    (void)seat;
    PrimaryServer* primary = wl_resource_get_user_data(resource);
    primary->device = wl_resource_create(client, &zwp_primary_selection_device_v1_interface,
                                         wl_resource_get_version(resource), id);
    wl_resource_set_implementation(primary->device, &s_primaryDevice, primary, nullptr);
}

static const struct zwp_primary_selection_device_manager_v1_interface s_primaryManager = {
    PrimaryCreateSource,
    PrimaryGetDevice,
    ServerDestroyResource,
};

static inline void PrimaryBindManager(struct wl_client* client, void* data, uint32_t version,
                                      uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &zwp_primary_selection_device_manager_v1_interface, version, id);
    wl_resource_set_implementation(resource, &s_primaryManager, data, nullptr);
}

// The client reads the offered text: a thread writes it.
static inline void PrimaryReceive(struct wl_client* client, struct wl_resource* resource,
                                  const char* type, int32_t fd)
{
    (void)client;
    (void)type;
    PrimaryServer* primary = wl_resource_get_user_data(resource);
    DataWrite* job = malloc(sizeof(DataWrite));
    *job = (DataWrite){fd, malloc(primary->offeredLength + 1), primary->offeredLength};
    memcpy(job->text, primary->offered, primary->offeredLength);
    pthread_t thread;
    pthread_create(&thread, nullptr, DataWriteRun, job);
    pthread_detach(thread);
}

static const struct zwp_primary_selection_offer_v1_interface s_primaryOffer = {
    PrimaryReceive,
    ServerDestroyResource,
};

static inline void PrimaryStart(PrimaryServer* primary, Server* server)
{
    *primary = (PrimaryServer){.server = server};
    pthread_mutex_lock(&server->lock);
    wl_global_create(server->display, &zwp_primary_selection_device_manager_v1_interface, 1,
                     primary, PrimaryBindManager);
    pthread_mutex_unlock(&server->lock);
}

static inline void PrimaryStop(PrimaryServer* primary)
{
    free(primary->offered);
}

// Another client takes the selection with this text, the client's
// source cancelled.
static inline void PrimaryOffer(PrimaryServer* primary, const char* text, size_t length)
{
    Server* server = primary->server;
    pthread_mutex_lock(&server->lock);
    free(primary->offered);
    primary->offered = malloc(length + 1);
    memcpy(primary->offered, text, length);
    primary->offeredLength = length;
    if (primary->source != nullptr)
    {
        zwp_primary_selection_source_v1_send_cancelled(primary->source);
        primary->source = nullptr;
    }
    struct wl_resource* offer = wl_resource_create(wl_resource_get_client(primary->device),
                                                   &zwp_primary_selection_offer_v1_interface,
                                                   wl_resource_get_version(primary->device), 0);
    wl_resource_set_implementation(offer, &s_primaryOffer, primary, nullptr);
    zwp_primary_selection_device_v1_send_data_offer(primary->device, offer);
    zwp_primary_selection_offer_v1_send_offer(offer, "text/plain;charset=utf-8");
    zwp_primary_selection_device_v1_send_selection(primary->device, offer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// Whether the client's source holds the selection offering a type.
static inline bool PrimarySourceHas(PrimaryServer* primary, const char* type)
{
    pthread_mutex_lock(&primary->server->lock);
    bool has = false;
    const PrimaryTypes* types =
        primary->source != nullptr ? wl_resource_get_user_data(primary->source) : nullptr;
    for (int i = 0; types != nullptr && i < types->count; i++)
    {
        has = has || strcmp(types->types[i], type) == 0;
    }
    pthread_mutex_unlock(&primary->server->lock);
    return has;
}

// Asks the client for its selection's text as another client would: the
// pipe to read it from, without waiting, or -1.
static inline int PrimaryRequest(PrimaryServer* primary)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0)
    {
        return -1;
    }
    (void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
    pthread_mutex_lock(&primary->server->lock);
    if (primary->source != nullptr)
    {
        zwp_primary_selection_source_v1_send_send(primary->source, "text/plain;charset=utf-8",
                                                  fds[1]);
        wl_display_flush_clients(primary->server->display);
    }
    pthread_mutex_unlock(&primary->server->lock);
    close(fds[1]);
    return fds[0];
}

#endif // MAUL_WINDOW_TEST_WAYLAND_PRIMARY_SERVER_H
