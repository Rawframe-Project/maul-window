// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version and result names.

#include "maul-window/base.h"

mwinVersion mwinGetVersion(void)
{
    return (mwinVersion){MWIN_VERSION_MAJOR, MWIN_VERSION_MINOR, MWIN_VERSION_PATCH};
}

const char* mwinResultName(mwinResult result)
{
    switch (result)
    {
    case mwin_success:
        return "mwin_success";
    case mwin_errorInvalid:
        return "mwin_errorInvalid";
    case mwin_errorCapacity:
        return "mwin_errorCapacity";
    default:
        return "unknown result";
    }
}
