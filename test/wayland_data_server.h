// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The data device of the test compositor (wayland_server.h), for the
// clipboard tests: the selection the client sets, with the types its
// source offers and the serial it quotes; bytes another client would
// offer as types of its own, which the compositor writes into the client's pipe from
// a thread of its own; and the client's selection read as another
// client would, through a pipe the test reads without waiting; and a
// drag another client makes over the last surface, with its files and
// text, the type the client accepts, the actions it sets and whether
// it finished the drop.

#ifndef MAUL_WINDOW_TEST_WAYLAND_DATA_SERVER_H
#define MAUL_WINDOW_TEST_WAYLAND_DATA_SERVER_H

#include "wayland_server.h"

#include <fcntl.h>
#include <stdio.h>

#define DATA_TYPES      8
#define DATA_TYPE_BYTES 64

typedef struct DataServer DataServer;

// What the client answered a drag: the type it accepts ("" for none),
// the actions it set, and whether it finished the drop.
typedef struct DataDrag
{
    char accepted[DATA_TYPE_BYTES];
    int accepts;
    uint32_t actions;
    bool finished;
} DataDrag;

// The types a source offers.
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
    // A drag's offer, its files as a uri-list and its text, and what the
    // client answered.
    struct wl_resource* dragOffer;
    char* dragFiles;
    char* dragText;
    DataDrag drag;
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

static inline void DataDragAccept(struct wl_client* client, struct wl_resource* resource,
                                  uint32_t serial, const char* type)
{
    (void)client;
    (void)serial;
    DataServer* data = wl_resource_get_user_data(resource);
    (void)snprintf(data->drag.accepted, sizeof(data->drag.accepted), "%s",
                   type != nullptr ? type : "");
    data->drag.accepts += 1;
}

// The client reads the drag's files or text: a thread writes them.
static inline void DataDragReceive(struct wl_client* client, struct wl_resource* resource,
                                   const char* type, int32_t fd)
{
    (void)client;
    DataServer* data = wl_resource_get_user_data(resource);
    const char* text = strcmp(type, "text/uri-list") == 0 ? data->dragFiles : data->dragText;
    text = text != nullptr ? text : "";
    DataWrite* job = malloc(sizeof(DataWrite));
    *job = (DataWrite){fd, strdup(text), strlen(text)};
    pthread_t thread;
    pthread_create(&thread, nullptr, DataWriteRun, job);
    pthread_detach(thread);
}

static inline void DataDragFinish(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    DataServer* data = wl_resource_get_user_data(resource);
    data->drag.finished = true;
}

static inline void DataDragActions(struct wl_client* client, struct wl_resource* resource,
                                   uint32_t actions, uint32_t preferred)
{
    (void)client;
    (void)preferred;
    DataServer* data = wl_resource_get_user_data(resource);
    data->drag.actions = actions;
}

static const struct wl_data_offer_interface s_dragOffer = {
    DataDragAccept, DataDragReceive, ServerDestroyResource, DataDragFinish, DataDragActions,
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
    free(data->dragFiles);
    free(data->dragText);
}

// Another client drags files (a uri-list) and text (either NULL), or
// only HTML when both are, over the last surface.
static inline void DataDragEnter(DataServer* data, double x, double y, const char* files,
                                 const char* text)
{
    Server* server = data->server;
    pthread_mutex_lock(&server->lock);
    free(data->dragFiles);
    free(data->dragText);
    data->dragFiles = files != nullptr ? strdup(files) : nullptr;
    data->dragText = text != nullptr ? strdup(text) : nullptr;
    data->drag = (DataDrag){0};
    struct wl_resource* offer =
        wl_resource_create(wl_resource_get_client(data->device), &wl_data_offer_interface,
                           wl_resource_get_version(data->device), 0);
    wl_resource_set_implementation(offer, &s_dragOffer, data, nullptr);
    data->dragOffer = offer;
    wl_data_device_send_data_offer(data->device, offer);
    wl_data_offer_send_offer(offer, "text/html");
    if (files != nullptr)
    {
        wl_data_offer_send_offer(offer, "text/uri-list");
    }
    if (text != nullptr)
    {
        wl_data_offer_send_offer(offer, "text/plain;charset=utf-8");
    }
    wl_data_device_send_enter(data->device, ++server->serial, server->surface,
                              wl_fixed_from_double(x), wl_fixed_from_double(y), offer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

static inline void DataDragMotion(DataServer* data, double x, double y)
{
    pthread_mutex_lock(&data->server->lock);
    wl_data_device_send_motion(data->device, 1000, wl_fixed_from_double(x),
                               wl_fixed_from_double(y));
    wl_display_flush_clients(data->server->display);
    pthread_mutex_unlock(&data->server->lock);
}

// The drag leaves, or drops.
static inline void DataDragEnd(DataServer* data, bool drop)
{
    pthread_mutex_lock(&data->server->lock);
    if (drop)
    {
        wl_data_device_send_drop(data->device);
    }
    else
    {
        wl_data_device_send_leave(data->device);
    }
    wl_display_flush_clients(data->server->display);
    pthread_mutex_unlock(&data->server->lock);
}

static inline DataDrag DataDragState(DataServer* data)
{
    pthread_mutex_lock(&data->server->lock);
    DataDrag drag = data->drag;
    pthread_mutex_unlock(&data->server->lock);
    return drag;
}

// Another client takes the selection with bytes of these types, the
// client's source cancelled; none takes it with nothing.
static inline void DataOfferTypes(DataServer* data, const char* const* types, int count,
                                  const char* bytes, size_t length)
{
    Server* server = data->server;
    pthread_mutex_lock(&server->lock);
    free(data->offered);
    data->offered = malloc(length + 1);
    memcpy(data->offered, bytes != nullptr ? bytes : "", length);
    data->offeredLength = length;
    if (data->source != nullptr)
    {
        wl_data_source_send_cancelled(data->source);
        data->source = nullptr;
    }
    struct wl_resource* offer = nullptr;
    if (count > 0)
    {
        offer = wl_resource_create(wl_resource_get_client(data->device), &wl_data_offer_interface,
                                   wl_resource_get_version(data->device), 0);
        wl_resource_set_implementation(offer, &s_dataOffer, data, nullptr);
        wl_data_device_send_data_offer(data->device, offer);
        for (int i = 0; i < count; i++)
        {
            wl_data_offer_send_offer(offer, types[i]);
        }
    }
    // Announced twice, as compositors may: the second keeps the first.
    wl_data_device_send_selection(data->device, offer);
    wl_data_device_send_selection(data->device, offer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// Another client takes the selection with this text, the best text type
// among others before a lesser one; NULL takes it with nothing.
static inline void DataOffer(DataServer* data, const char* text, size_t length)
{
    static const char* const s_textTypes[] = {"text/html", "text/plain;charset=utf-8",
                                              "text/plain"};
    DataOfferTypes(data, s_textTypes, text != nullptr ? 3 : 0, text, length);
}

// Whether the client's source offers a type.
static inline bool DataSourceHas(DataServer* data, const char* type)
{
    pthread_mutex_lock(&data->server->lock);
    bool has = false;
    const DataTypes* types =
        data->source != nullptr ? wl_resource_get_user_data(data->source) : nullptr;
    for (int i = 0; types != nullptr && i < types->count; i++)
    {
        has = has || strcmp(types->types[i], type) == 0;
    }
    pthread_mutex_unlock(&data->server->lock);
    return has;
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

// Asks the client for its selection as a type, as another client would:
// the pipe to read it from, without waiting, or -1.
static inline int DataRequestAs(DataServer* data, const char* type)
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
        wl_data_source_send_send(data->source, type, fds[1]);
        wl_display_flush_clients(data->server->display);
    }
    pthread_mutex_unlock(&data->server->lock);
    close(fds[1]);
    return fds[0];
}

// Asks the client for its selection's text.
static inline int DataRequest(DataServer* data)
{
    return DataRequestAs(data, "text/plain;charset=utf-8");
}

#endif // MAUL_WINDOW_TEST_WAYLAND_DATA_SERVER_H
