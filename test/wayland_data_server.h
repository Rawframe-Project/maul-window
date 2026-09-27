// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The data device of the test compositor (wayland_server.h), for the
// clipboard tests: the selection the client sets, with the text types
// its source offers and the serial it quotes; text another client
// would offer, which the compositor writes into the client's pipe from
// a thread of its own; and the client's selection read as another
// client would, through a pipe the test reads without waiting.

#ifndef MAUL_WINDOW_TEST_WAYLAND_DATA_SERVER_H
#define MAUL_WINDOW_TEST_WAYLAND_DATA_SERVER_H

#include "wayland_server.h"

#include <fcntl.h>

#define DATA_TYPES      4
#define DATA_TYPE_BYTES 40

typedef struct DataServer DataServer;

// The text types a source offers.
typedef struct DataTypes
{
    DataServer* data;
    char types[DATA_TYPES][DATA_TYPE_BYTES];
    int count;
} DataTypes;

struct DataServer
{
    Server* server;
    struct wl_resource* device;
    // The client's source that holds the selection, or NULL, and the
    // serial it quoted and how many selections it set.
    struct wl_resource* source;
    uint32_t serial;
    int selections;
    // The text the compositor offers as another client's.
    char* offered;
    size_t offeredLength;
    // The type the client last asked the offer for.
    char received[DATA_TYPE_BYTES];
};

// A copy of text and the pipe to write it into.
typedef struct DataWrite
{
    int fd;
    char* text;
    size_t length;
} DataWrite;

static inline void* DataWriteRun(void* data)
{
    DataWrite* job = data;
    size_t done = 0;
    while (done < job->length)
    {
        ssize_t written = write(job->fd, job->text + done, job->length - done);
        if (written <= 0)
        {
            break;
        }
        done += (size_t)written;
    }
    close(job->fd);
    free(job->text);
    free(job);
    return nullptr;
}

static inline void DataSourceOffer(struct wl_client* client, struct wl_resource* resource,
                                   const char* type)
{
    (void)client;
    DataTypes* types = wl_resource_get_user_data(resource);
    if (types->count < DATA_TYPES && strlen(type) < DATA_TYPE_BYTES)
    {
        strcpy(types->types[types->count++], type);
    }
}

static inline void DataSourceActions(struct wl_client* client, struct wl_resource* resource,
                                     uint32_t actions)
{
    (void)client;
    (void)resource;
    (void)actions;
}

static const struct wl_data_source_interface s_dataSource = {
    DataSourceOffer,
    ServerDestroyResource,
    DataSourceActions,
};

// A source going away stops holding the selection.
static inline void DataSourceGone(struct wl_resource* resource)
{
    DataTypes* types = wl_resource_get_user_data(resource);
    if (types->data->source == resource)
    {
        types->data->source = nullptr;
    }
    free(types);
}

static inline void DataCreateSource(struct wl_client* client, struct wl_resource* resource,
                                    uint32_t id)
{
    struct wl_resource* source = wl_resource_create(client, &wl_data_source_interface,
                                                    wl_resource_get_version(resource), id);
    DataTypes* types = calloc(1, sizeof(DataTypes));
    types->data = wl_resource_get_user_data(resource);
    wl_resource_set_implementation(source, &s_dataSource, types, DataSourceGone);
}

static inline void DataStartDrag(struct wl_client* client, struct wl_resource* resource,
                                 struct wl_resource* source, struct wl_resource* origin,
                                 struct wl_resource* icon, uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)source;
    (void)origin;
    (void)icon;
    (void)serial;
}

static inline void DataSetSelection(struct wl_client* client, struct wl_resource* resource,
                                    struct wl_resource* source, uint32_t serial)
{
    (void)client;
    DataServer* data = wl_resource_get_user_data(resource);
    data->source = source;
    data->serial = serial;
    data->selections += 1;
}

static const struct wl_data_device_interface s_dataDevice = {
    DataStartDrag,
    DataSetSelection,
    ServerDestroyResource,
};

static inline void DataGetDevice(struct wl_client* client, struct wl_resource* resource,
                                 uint32_t id, struct wl_resource* seat)
{
    (void)seat;
    DataServer* data = wl_resource_get_user_data(resource);
    data->device = wl_resource_create(client, &wl_data_device_interface,
                                      wl_resource_get_version(resource), id);
    wl_resource_set_implementation(data->device, &s_dataDevice, data, nullptr);
}

static const struct wl_data_device_manager_interface s_dataManager = {
    DataCreateSource,
    DataGetDevice,
};

static inline void DataBindManager(struct wl_client* client, void* data, uint32_t version,
                                   uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &wl_data_device_manager_interface, version, id);
    wl_resource_set_implementation(resource, &s_dataManager, data, nullptr);
}

static inline void DataAccept(struct wl_client* client, struct wl_resource* resource,
                              uint32_t serial, const char* type)
{
    (void)client;
    (void)resource;
    (void)serial;
    (void)type;
}

// The client reads the offered text: a thread writes it.
static inline void DataReceive(struct wl_client* client, struct wl_resource* resource,
                               const char* type, int32_t fd)
{
    (void)client;
    DataServer* data = wl_resource_get_user_data(resource);
    (void)snprintf(data->received, sizeof(data->received), "%s", type);
    DataWrite* job = malloc(sizeof(DataWrite));
    *job = (DataWrite){fd, malloc(data->offeredLength + 1), data->offeredLength};
    memcpy(job->text, data->offered, data->offeredLength);
    pthread_t thread;
    pthread_create(&thread, nullptr, DataWriteRun, job);
    pthread_detach(thread);
}

static inline void DataOfferActions(struct wl_client* client, struct wl_resource* resource,
                                    uint32_t actions, uint32_t preferred)
{
    (void)client;
    (void)resource;
    (void)actions;
    (void)preferred;
}

static const struct wl_data_offer_interface s_dataOffer = {
    DataAccept, DataReceive, ServerDestroyResource, ServerNoRequest, DataOfferActions,
};

static inline void DataStart(DataServer* data, Server* server)
{
    *data = (DataServer){.server = server};
    pthread_mutex_lock(&server->lock);
    wl_global_create(server->display, &wl_data_device_manager_interface, 3, data, DataBindManager);
    pthread_mutex_unlock(&server->lock);
}

static inline void DataStop(DataServer* data)
{
    free(data->offered);
}

// Another client takes the selection with this text, the client's
// source cancelled; NULL takes it with nothing.
static inline void DataOffer(DataServer* data, const char* text, size_t length)
{
    Server* server = data->server;
    pthread_mutex_lock(&server->lock);
    free(data->offered);
    data->offered = malloc(length + 1);
    memcpy(data->offered, text != nullptr ? text : "", length);
    data->offeredLength = length;
    if (data->source != nullptr)
    {
        wl_data_source_send_cancelled(data->source);
        data->source = nullptr;
    }
    struct wl_resource* offer = nullptr;
    if (text != nullptr)
    {
        offer = wl_resource_create(wl_resource_get_client(data->device), &wl_data_offer_interface,
                                   wl_resource_get_version(data->device), 0);
        wl_resource_set_implementation(offer, &s_dataOffer, data, nullptr);
        wl_data_device_send_data_offer(data->device, offer);
        // The best text type among others, before a lesser one.
        wl_data_offer_send_offer(offer, "text/html");
        wl_data_offer_send_offer(offer, "text/plain;charset=utf-8");
        wl_data_offer_send_offer(offer, "text/plain");
    }
    wl_data_device_send_selection(data->device, offer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// The client's selection: the serial it quoted, and whether its source
// offers every text type; -1 selections when it holds none.
static inline int DataSelection(DataServer* data, uint32_t* serialOut, bool* typesOut)
{
    pthread_mutex_lock(&data->server->lock);
    int selections = data->source != nullptr ? data->selections : -1;
    *serialOut = data->serial;
    *typesOut = false;
    if (data->source != nullptr)
    {
        const DataTypes* types = wl_resource_get_user_data(data->source);
        *typesOut = types->count == 3 && strcmp(types->types[0], "text/plain;charset=utf-8") == 0 &&
                    strcmp(types->types[1], "UTF8_STRING") == 0 &&
                    strcmp(types->types[2], "text/plain") == 0;
    }
    pthread_mutex_unlock(&data->server->lock);
    return selections;
}

// Whether the client last asked an offer for this type.
static inline bool DataReceived(DataServer* data, const char* type)
{
    pthread_mutex_lock(&data->server->lock);
    bool same = strcmp(data->received, type) == 0;
    pthread_mutex_unlock(&data->server->lock);
    return same;
}

// Asks the client for its selection's text as another client would: the
// pipe to read it from, without waiting, or -1.
static inline int DataRequest(DataServer* data)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0)
    {
        return -1;
    }
    (void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
    pthread_mutex_lock(&data->server->lock);
    if (data->source != nullptr)
    {
        wl_data_source_send_send(data->source, "text/plain;charset=utf-8", fds[1]);
        wl_display_flush_clients(data->server->display);
    }
    pthread_mutex_unlock(&data->server->lock);
    close(fds[1]);
    return fds[0];
}

#endif // MAUL_WINDOW_TEST_WAYLAND_DATA_SERVER_H
