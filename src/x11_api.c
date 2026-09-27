// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening XCB.

#include "x11_api.h"

#include <dlfcn.h>
#include <string.h>

// A symbol of a library by name. A function comes back as data from
// dlsym and is copied, not cast, into its pointer.
static bool Find(void* library, const char* name, void* target, size_t size)
{
    void* symbol = dlsym(library, name);
    if (symbol == nullptr)
    {
        return false;
    }
    memcpy(target, (const void*)&symbol, size);
    return true;
}

#define FIND(library, field, name) Find(api->library, #name, (void*)&api->field, sizeof(api->field))

static bool FindCore(mwinX11Api* api)
{
    return FIND(library, connect, xcb_connect) && FIND(library, disconnect, xcb_disconnect) &&
           FIND(library, connectionHasError, xcb_connection_has_error) &&
           FIND(library, getSetup, xcb_get_setup) &&
           FIND(library, setupRootsIterator, xcb_setup_roots_iterator) &&
           FIND(library, screenNext, xcb_screen_next) &&
           FIND(library, generateId, xcb_generate_id) && FIND(library, flush, xcb_flush) &&
           FIND(library, pollForEvent, xcb_poll_for_event) &&
           FIND(library, getExtensionData, xcb_get_extension_data) &&
           FIND(library, requestCheck, xcb_request_check) &&
           FIND(library, createWindowChecked, xcb_create_window_checked) &&
           FIND(library, destroyWindow, xcb_destroy_window) &&
           FIND(library, mapWindow, xcb_map_window) &&
           FIND(library, unmapWindow, xcb_unmap_window) &&
           FIND(library, configureWindow, xcb_configure_window) &&
           FIND(library, changeProperty, xcb_change_property) &&
           FIND(library, deleteProperty, xcb_delete_property) &&
           FIND(library, internAtom, xcb_intern_atom) &&
           FIND(library, internAtomReply, xcb_intern_atom_reply) &&
           FIND(library, getProperty, xcb_get_property) &&
           FIND(library, getPropertyReply, xcb_get_property_reply) &&
           FIND(library, getPropertyValue, xcb_get_property_value) &&
           FIND(library, getPropertyValueLength, xcb_get_property_value_length) &&
           FIND(library, getAtomName, xcb_get_atom_name) &&
           FIND(library, getAtomNameReply, xcb_get_atom_name_reply) &&
           FIND(library, getAtomNameName, xcb_get_atom_name_name) &&
           FIND(library, getAtomNameNameLength, xcb_get_atom_name_name_length) &&
           FIND(library, sendEvent, xcb_send_event) &&
           FIND(library, setInputFocus, xcb_set_input_focus) &&
           FIND(library, translateCoordinates, xcb_translate_coordinates) &&
           FIND(library, translateCoordinatesReply, xcb_translate_coordinates_reply) &&
           FIND(library, changeWindowAttributes, xcb_change_window_attributes);
}

static bool FindRandr(mwinX11Api* api)
{
    // The extension's id is data, not a function.
    api->randrId = dlsym(api->randrLibrary, "xcb_randr_id");
    return api->randrId != nullptr &&
           FIND(randrLibrary, randrQueryVersion, xcb_randr_query_version) &&
           FIND(randrLibrary, randrQueryVersionReply, xcb_randr_query_version_reply) &&
           FIND(randrLibrary, randrSelectInput, xcb_randr_select_input) &&
           FIND(randrLibrary, randrGetMonitors, xcb_randr_get_monitors) &&
           FIND(randrLibrary, randrGetMonitorsReply, xcb_randr_get_monitors_reply) &&
           FIND(randrLibrary, randrMonitorsIterator, xcb_randr_get_monitors_monitors_iterator) &&
           FIND(randrLibrary, randrMonitorInfoNext, xcb_randr_monitor_info_next);
}

mwinResult mwinLoadX11(mwinX11Api* api)
{
    memset(api, 0, sizeof(*api));
    api->library = dlopen("libxcb.so.1", RTLD_NOW | RTLD_LOCAL);
    if (api->library == nullptr || !FindCore(api))
    {
        mwinUnloadX11(api);
        return mwin_errorUnsupported;
    }
    // libxcb-randr needs libxcb's symbols, which RTLD_LOCAL keeps from
    // it unless it links libxcb itself, as every distribution's does.
    api->randrLibrary = dlopen("libxcb-randr.so.0", RTLD_NOW | RTLD_LOCAL);
    if (api->randrLibrary != nullptr && !FindRandr(api))
    {
        dlclose(api->randrLibrary);
        api->randrLibrary = nullptr;
    }
    return mwin_success;
}

void mwinUnloadX11(mwinX11Api* api)
{
    if (api->randrLibrary != nullptr)
    {
        dlclose(api->randrLibrary);
    }
    if (api->library != nullptr)
    {
        dlclose(api->library);
    }
    memset(api, 0, sizeof(*api));
}
