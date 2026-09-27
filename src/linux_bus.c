// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The session bus for the Linux services.

#include "linux_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

// The user's bus when no address is named: a socket in the runtime
// directory. Anything else would have the library start a bus.
static DBusConnection* OpenUserBus(const mwinDBusApi* api)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    char address[512];
    struct stat status;
    int length =
        runtime != nullptr ? snprintf(address, sizeof(address), "unix:path=%s/bus", runtime) : -1;
    if (length <= 0 || (size_t)length >= sizeof(address) ||
        stat(address + sizeof("unix:path=") - 1, &status) != 0 || !S_ISSOCK(status.st_mode))
    {
        return nullptr;
    }
    DBusConnection* connection = api->openPrivate(address, nullptr);
    if (connection != nullptr && !api->busRegister(connection, nullptr))
    {
        api->close(connection);
        api->unrefConnection(connection);
        return nullptr;
    }
    return connection;
}

bool mwinBusConnect(mwinLinuxBus* bus)
{
    if (!bus->tried)
    {
        bus->tried = true;
        if (mwinLoadDBus(&bus->api) != mwin_success)
        {
            return false;
        }
        const char* named = getenv("DBUS_SESSION_BUS_ADDRESS");
        bus->connection = named != nullptr && named[0] != '\0'
                              ? bus->api.busGetPrivate(mwin_dbusSession, nullptr)
                              : OpenUserBus(&bus->api);
        if (bus->connection == nullptr)
        {
            mwinUnloadDBus(&bus->api);
            return false;
        }
        // The library ends the process when the bus goes, unless told.
        bus->api.setExitOnDisconnect(bus->connection, 0);
    }
    return bus->connection != nullptr;
}

DBusMessage* mwinBusMethod(mwinLinuxBus* bus, const char* destination, const char* path,
                           const char* interface, const char* method)
{
    return mwinBusConnect(bus) ? bus->api.newMethodCall(destination, path, interface, method)
                               : nullptr;
}

bool mwinBusSend(mwinLinuxBus* bus, DBusMessage* message, mwinBusCall* call, uint64_t nowNs)
{
    call->pending = nullptr;
    // The library's own timeout never fires without a main loop; the
    // call's deadline stands for it.
    bool sent = bus->api.sendWithReply(bus->connection, message, &call->pending, -1) &&
                call->pending != nullptr;
    bus->api.unrefMessage(message);
    call->deadlineNs = nowNs + MWIN_BUS_DEADLINE_NS;
    if (sent)
    {
        mwinBusPump(bus);
    }
    return sent;
}

void mwinBusTell(mwinLinuxBus* bus, DBusMessage* message)
{
    (void)bus->api.send(bus->connection, message, nullptr);
    bus->api.unrefMessage(message);
    mwinBusPump(bus);
}

void mwinBusPump(mwinLinuxBus* bus)
{
    if (bus->connection == nullptr)
    {
        return;
    }
    (void)bus->api.readWrite(bus->connection, 0);
    // Nothing handles what comes unasked; dispatching lets it go.
    while (bus->api.dispatch(bus->connection) == mwin_dbusDataRemains)
    {
    }
}

DBusMessage* mwinBusAnswer(mwinLinuxBus* bus, mwinBusCall* call, uint64_t nowNs, bool* failed)
{
    *failed = false;
    if (!bus->api.completed(call->pending))
    {
        if (nowNs >= call->deadlineNs)
        {
            mwinBusDrop(bus, call);
            *failed = true;
        }
        return nullptr;
    }
    DBusMessage* reply = bus->api.stealReply(call->pending);
    bus->api.unrefPending(call->pending);
    call->pending = nullptr;
    *failed = reply == nullptr || bus->api.messageType(reply) == mwin_dbusMessageError;
    return reply;
}

void mwinBusDrop(mwinLinuxBus* bus, mwinBusCall* call)
{
    if (call->pending != nullptr)
    {
        bus->api.cancel(call->pending);
        bus->api.unrefPending(call->pending);
        call->pending = nullptr;
    }
}

void mwinBusClose(mwinLinuxBus* bus)
{
    if (bus->connection != nullptr)
    {
        bus->api.close(bus->connection);
        bus->api.unrefConnection(bus->connection);
    }
    if (bus->api.library != nullptr)
    {
        mwinUnloadDBus(&bus->api);
    }
    *bus = (mwinLinuxBus){0};
}
