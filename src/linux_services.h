// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Linux services, for the Wayland and X11 backends alike.
//
// An address goes to xdg-open, which every desktop has and which hands
// it to the desktop's own opener, or to the desktop portal inside a
// sandbox; it runs without a shell, and is answered by how it ends: 0
// done, 3 (no tool for the desktop) unsupported, anything else failed,
// or done when it still runs after a few seconds, showing what it
// opened. A file is shown by the file manager over the session bus
// (org.freedesktop.FileManager1's ShowItems, which selects it), else
// its folder is opened by xdg-open.

#ifndef MAUL_WINDOW_SRC_LINUX_SERVICES_H
#define MAUL_WINDOW_SRC_LINUX_SERVICES_H

#include "core.h"
#include "linux_bus.h"

#include <sys/types.h>

// The xdg-open runs followed at once, and the file manager's calls.
#define MWIN_LINUX_OPENERS 8
#define MWIN_LINUX_REVEALS 4

// The request an answer goes to, while it waits for one.
typedef struct mwinServiceAnswer
{
    uint32_t slot;
    uint32_t request;
    uint32_t generation;
    bool waiting;
} mwinServiceAnswer;

// An xdg-open run: 0 for none. It is followed until it ends, after its
// answer too, so none is left unreaped.
typedef struct mwinLinuxOpener
{
    pid_t pid;
    mwinServiceAnswer to;
    uint64_t deadlineNs;
} mwinLinuxOpener;

typedef struct mwinLinuxReveal
{
    mwinBusCall call;
    mwinServiceAnswer to;
} mwinLinuxReveal;

typedef struct mwinLinuxServices
{
    mwinContext* context;
    mwinLinuxBus bus;
    mwinLinuxOpener openers[MWIN_LINUX_OPENERS];
    mwinLinuxReveal reveals[MWIN_LINUX_REVEALS];
} mwinLinuxServices;

void mwinLinuxServicesStart(mwinLinuxServices* services, mwinContext* context);

// Carry out a request of the window: -1 while it is answered later,
// else the outcome.
int mwinLinuxOpenUrl(mwinLinuxServices* services, uint32_t slot, uint32_t request);
int mwinLinuxRevealFile(mwinLinuxServices* services, uint32_t slot, uint32_t request);

// Answers what has ended; each pump calls it.
void mwinLinuxServicesPump(mwinLinuxServices* services, uint64_t nowNs);

// Lets everything go; xdg-open runs still going are left to end alone.
void mwinLinuxServicesStop(mwinLinuxServices* services);

#endif // MAUL_WINDOW_SRC_LINUX_SERVICES_H
