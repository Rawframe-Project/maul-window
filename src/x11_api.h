// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// XCB, opened at run time (W7) into a table each context loads for
// itself: libxcb, and libxcb-randr for monitors where it is there. The
// X server's replies come from the C library's malloc and go back
// through mwinReleaseSystemMemory.

#ifndef MAUL_WINDOW_SRC_X11_API_H
#define MAUL_WINDOW_SRC_X11_API_H

#include "maul-window/base.h"

#include <xcb/randr.h>
#include <xcb/xcb.h>

typedef struct mwinX11Api
{
    void* library;
    typeof(xcb_connect)* connect;
    typeof(xcb_disconnect)* disconnect;
    typeof(xcb_connection_has_error)* connectionHasError;
    typeof(xcb_get_setup)* getSetup;
    typeof(xcb_setup_roots_iterator)* setupRootsIterator;
    typeof(xcb_screen_next)* screenNext;
    typeof(xcb_generate_id)* generateId;
    typeof(xcb_flush)* flush;
    typeof(xcb_poll_for_event)* pollForEvent;
    typeof(xcb_get_extension_data)* getExtensionData;
    typeof(xcb_request_check)* requestCheck;
    typeof(xcb_create_window_checked)* createWindowChecked;
    typeof(xcb_destroy_window)* destroyWindow;
    typeof(xcb_map_window)* mapWindow;
    typeof(xcb_unmap_window)* unmapWindow;
    typeof(xcb_configure_window)* configureWindow;
    typeof(xcb_change_property)* changeProperty;
    typeof(xcb_delete_property)* deleteProperty;
    typeof(xcb_intern_atom)* internAtom;
    typeof(xcb_intern_atom_reply)* internAtomReply;
    typeof(xcb_get_property)* getProperty;
    typeof(xcb_get_property_reply)* getPropertyReply;
    typeof(xcb_get_property_value)* getPropertyValue;
    typeof(xcb_get_property_value_length)* getPropertyValueLength;
    typeof(xcb_get_atom_name)* getAtomName;
    typeof(xcb_get_atom_name_reply)* getAtomNameReply;
    typeof(xcb_get_atom_name_name)* getAtomNameName;
    typeof(xcb_get_atom_name_name_length)* getAtomNameNameLength;
    typeof(xcb_send_event)* sendEvent;
    typeof(xcb_set_input_focus)* setInputFocus;
    typeof(xcb_translate_coordinates)* translateCoordinates;
    typeof(xcb_translate_coordinates_reply)* translateCoordinatesReply;
    typeof(xcb_change_window_attributes)* changeWindowAttributes;
    // libxcb-randr, NULL where it is missing.
    void* randrLibrary;
    xcb_extension_t* randrId;
    typeof(xcb_randr_query_version)* randrQueryVersion;
    typeof(xcb_randr_query_version_reply)* randrQueryVersionReply;
    typeof(xcb_randr_select_input)* randrSelectInput;
    typeof(xcb_randr_get_monitors)* randrGetMonitors;
    typeof(xcb_randr_get_monitors_reply)* randrGetMonitorsReply;
    typeof(xcb_randr_get_monitors_monitors_iterator)* randrMonitorsIterator;
    typeof(xcb_randr_monitor_info_next)* randrMonitorInfoNext;
} mwinX11Api;

// Opens libxcb and fills the table: mwin_errorUnsupported when it or a
// function is missing. libxcb-randr is opened too when it is there.
mwinResult mwinLoadX11(mwinX11Api* api);

// Closes what mwinLoadX11 opened.
void mwinUnloadX11(mwinX11Api* api);

#endif // MAUL_WINDOW_SRC_X11_API_H
