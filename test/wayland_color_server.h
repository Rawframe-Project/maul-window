// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The color manager of the test compositor (wayland_server.h): every
// output's image description is the one a test sets, told in full as
// parametric information; a test changes it while the client runs, as
// an output switched into or out of HDR, or makes describing fail.

#ifndef MAUL_WINDOW_TEST_WAYLAND_COLOR_SERVER_H
#define MAUL_WINDOW_TEST_WAYLAND_COLOR_SERVER_H

#include "wayland_server.h"

#include <color-management-v1-server-protocol.h>
#include <stdlib.h>

#define COLOR_OUTPUTS 4

// An output's image description: its transfer function and luminances
// in nits (0 leaves the content light levels untold), or a failure.
typedef struct ColorDescription
{
    uint32_t transfer;
    uint32_t referenceNits;
    uint32_t targetMaxNits;
    uint32_t maxCll;
    uint32_t maxFall;
    bool fails;
} ColorDescription;

typedef struct ColorServer
{
    Server* server;
    struct wl_global* global;
    struct wl_resource* outputs[COLOR_OUTPUTS];
    ColorDescription description;
    uint32_t identity;
} ColorServer;

static inline void ColorGetInformation(struct wl_client* client, struct wl_resource* resource,
                                       uint32_t id)
{
    const ColorDescription* description = wl_resource_get_user_data(resource);
    struct wl_resource* information =
        wl_resource_create(client, &wp_image_description_info_v1_interface, 1, id);
    wl_resource_set_implementation(information, nullptr, nullptr, nullptr);
    wp_image_description_info_v1_send_primaries_named(information,
                                                      WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
    wp_image_description_info_v1_send_tf_named(information, description->transfer);
    // The least luminance in ten-thousandths of a nit: 0.2 nits.
    wp_image_description_info_v1_send_luminances(information, 2000, description->targetMaxNits,
                                                 description->referenceNits);
    wp_image_description_info_v1_send_target_luminance(information, 2000,
                                                       description->targetMaxNits);
    if (description->maxCll != 0)
    {
        wp_image_description_info_v1_send_target_max_cll(information, description->maxCll);
    }
    if (description->maxFall != 0)
    {
        wp_image_description_info_v1_send_target_max_fall(information, description->maxFall);
    }
    wp_image_description_info_v1_send_done(information);
    wl_resource_destroy(information);
}

static const struct wp_image_description_v1_interface s_colorDescription = {
    ServerDestroyResource,
    ColorGetInformation,
};

static inline void ColorFreeDescription(struct wl_resource* resource)
{
    free(wl_resource_get_user_data(resource));
}

// A description is the output's at the time it is asked for, and never
// changes after.
static inline void ColorGetDescription(struct wl_client* client, struct wl_resource* resource,
                                       uint32_t id)
{
    ColorServer* color = wl_resource_get_user_data(resource);
    struct wl_resource* created =
        wl_resource_create(client, &wp_image_description_v1_interface, 1, id);
    ColorDescription* description = malloc(sizeof(*description));
    *description = color->description;
    wl_resource_set_implementation(created, &s_colorDescription, description, ColorFreeDescription);
    if (description->fails)
    {
        wp_image_description_v1_send_failed(created, WP_IMAGE_DESCRIPTION_V1_CAUSE_NO_OUTPUT,
                                            "the test fails it");
    }
    else
    {
        wp_image_description_v1_send_ready(created, ++color->identity);
    }
}

static const struct wp_color_management_output_v1_interface s_colorOutput = {
    ServerDestroyResource,
    ColorGetDescription,
};

static inline void ColorForgetOutput(struct wl_resource* resource)
{
    ColorServer* color = wl_resource_get_user_data(resource);
    for (int i = 0; i < COLOR_OUTPUTS; i++)
    {
        if (color->outputs[i] == resource)
        {
            color->outputs[i] = nullptr;
        }
    }
}

static inline void ColorGetOutput(struct wl_client* client, struct wl_resource* resource,
                                  uint32_t id, struct wl_resource* output)
{
    (void)output;
    ColorServer* color = wl_resource_get_user_data(resource);
    struct wl_resource* created =
        wl_resource_create(client, &wp_color_management_output_v1_interface, 1, id);
    wl_resource_set_implementation(created, &s_colorOutput, color, ColorForgetOutput);
    for (int i = 0; i < COLOR_OUTPUTS; i++)
    {
        if (color->outputs[i] == nullptr)
        {
            color->outputs[i] = created;
            break;
        }
    }
}

// What the test compositor does not offer.
static inline void ColorNoSurface(struct wl_client* client, struct wl_resource* resource,
                                  uint32_t id, struct wl_resource* surface)
{
    (void)client;
    (void)id;
    (void)surface;
    wl_resource_post_error(resource, WP_COLOR_MANAGER_V1_ERROR_UNSUPPORTED_FEATURE,
                           "not in the test compositor");
}

static inline void ColorNoCreator(struct wl_client* client, struct wl_resource* resource,
                                  uint32_t id)
{
    (void)client;
    (void)id;
    wl_resource_post_error(resource, WP_COLOR_MANAGER_V1_ERROR_UNSUPPORTED_FEATURE,
                           "not in the test compositor");
}

static const struct wp_color_manager_v1_interface s_colorManager = {
    ServerDestroyResource, ColorGetOutput, ColorNoSurface, ColorNoSurface,
    ColorNoCreator,        ColorNoCreator, ColorNoCreator,
};

static inline void ColorBind(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    (void)version;
    struct wl_resource* resource =
        wl_resource_create(client, &wp_color_manager_v1_interface, 1, id);
    wl_resource_set_implementation(resource, &s_colorManager, data, nullptr);
    wp_color_manager_v1_send_supported_intent(resource,
                                              WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
    wp_color_manager_v1_send_supported_feature(resource, WP_COLOR_MANAGER_V1_FEATURE_PARAMETRIC);
    wp_color_manager_v1_send_supported_tf_named(resource,
                                                WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
    wp_color_manager_v1_send_supported_tf_named(resource,
                                                WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
    wp_color_manager_v1_send_supported_primaries_named(resource,
                                                       WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
    wp_color_manager_v1_send_done(resource);
}

// Offers the color manager, with every output described as given.
static inline void ColorAdd(ColorServer* color, Server* server, ColorDescription description)
{
    pthread_mutex_lock(&server->lock);
    *color = (ColorServer){.server = server, .description = description};
    color->global =
        wl_global_create(server->display, &wp_color_manager_v1_interface, 1, color, ColorBind);
    pthread_mutex_unlock(&server->lock);
}

// Describes every output anew, and tells the client each changed.
static inline void ColorChange(ColorServer* color, ColorDescription description)
{
    pthread_mutex_lock(&color->server->lock);
    color->description = description;
    for (int i = 0; i < COLOR_OUTPUTS; i++)
    {
        if (color->outputs[i] != nullptr)
        {
            wp_color_management_output_v1_send_image_description_changed(color->outputs[i]);
        }
    }
    pthread_mutex_unlock(&color->server->lock);
}

#endif // MAUL_WINDOW_TEST_WAYLAND_COLOR_SERVER_H
