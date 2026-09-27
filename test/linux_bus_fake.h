// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A session bus of the test's own for the Linux services tests: a
// dbus-daemon with nothing to start on demand, and on the test's own
// connection through libdbus, stand-ins for the file manager
// (FileManager1's ShowItems), the screensaver (Inhibit and UnInhibit)
// and the desktop portal (Inhibit, and Close on its handle), each
// writing down what it was asked and answering, or refusing as told.

#ifndef MAUL_WINDOW_TEST_LINUX_BUS_FAKE_H
#define MAUL_WINDOW_TEST_LINUX_BUS_FAKE_H

#include <dlfcn.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

extern char** environ;

#define FAKE_COOKIE 7u
#define FAKE_HANDLE "/org/freedesktop/portal/desktop/request/1_1/mwin"

typedef struct FakeBus
{
    pid_t daemon;
    void* library;
    void* connection;
    void* (*busGet)(int type, void* error);
    int (*requestName)(void* connection, const char* name, unsigned flags, void* error);
    unsigned (*readWrite)(void* connection, int timeout);
    void* (*pop)(void* connection);
    unsigned (*isCall)(void* message, const char* interface, const char* method);
    const char* (*path)(void* message);
    unsigned (*iterInit)(void* message, void* iter);
    void (*recurse)(void* iter, void* sub);
    int (*argType)(void* iter);
    void (*getBasic)(void* iter, void* value);
    unsigned (*next)(void* iter);
    void (*initAppend)(void* message, void* iter);
    unsigned (*appendBasic)(void* iter, int type, const void* value);
    void* (*newReturn)(void* message);
    void* (*newError)(void* message, const char* name, const char* text);
    unsigned (*send)(void* connection, void* message, unsigned* serial);
    void (*flush)(void* connection);
    void (*unref)(void* message);
    void (*close)(void* connection);
    void (*unrefConnection)(void* connection);
    void (*setExit)(void* connection, unsigned exit);
    // The file manager: what it was asked last, how often, and whether
    // it refuses.
    char item[256];
    char startup[64];
    int shown;
    bool refuseShow;
    // The screensaver: the program's name and reason it was given, the
    // inhibitions it holds, and whether it refuses.
    char application[64];
    char reason[64];
    int inhibited;
    bool refuseInhibit;
    // The portal: the flags it was given, and whether its handle is open.
    uint32_t flags;
    bool portalHeld;
} FakeBus;

#define FAKE_FIND(field, name)                                                                     \
    (symbol = dlsym(fake->library, #name),                                                         \
     symbol != nullptr &&                                                                          \
         (memcpy((void*)&fake->field, (const void*)&symbol, sizeof(symbol)), true))

static inline bool FakeLoad(FakeBus* fake)
{
    // Never unloaded, as the library does with it.
    fake->library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    void* symbol = nullptr;
    return fake->library != nullptr && FAKE_FIND(busGet, dbus_bus_get_private) &&
           FAKE_FIND(requestName, dbus_bus_request_name) &&
           FAKE_FIND(readWrite, dbus_connection_read_write) &&
           FAKE_FIND(pop, dbus_connection_pop_message) &&
           FAKE_FIND(isCall, dbus_message_is_method_call) &&
           FAKE_FIND(path, dbus_message_get_path) && FAKE_FIND(iterInit, dbus_message_iter_init) &&
           FAKE_FIND(recurse, dbus_message_iter_recurse) &&
           FAKE_FIND(argType, dbus_message_iter_get_arg_type) &&
           FAKE_FIND(getBasic, dbus_message_iter_get_basic) &&
           FAKE_FIND(next, dbus_message_iter_next) &&
           FAKE_FIND(initAppend, dbus_message_iter_init_append) &&
           FAKE_FIND(appendBasic, dbus_message_iter_append_basic) &&
           FAKE_FIND(newReturn, dbus_message_new_method_return) &&
           FAKE_FIND(newError, dbus_message_new_error) && FAKE_FIND(send, dbus_connection_send) &&
           FAKE_FIND(flush, dbus_connection_flush) && FAKE_FIND(unref, dbus_message_unref) &&
           FAKE_FIND(close, dbus_connection_close) &&
           FAKE_FIND(unrefConnection, dbus_connection_unref) &&
           FAKE_FIND(setExit, dbus_connection_set_exit_on_disconnect);
}

// Starts the bus in a directory, names it in DBUS_SESSION_BUS_ADDRESS,
// and takes the services' names: false where there is no dbus-daemon or
// libdbus-1.
static inline bool FakeStart(FakeBus* fake, const char* directory)
{
    char path[256];
    char config[768];
    (void)snprintf(config, sizeof(config),
                   "<busconfig><type>session</type><listen>unix:path=%s/bus</listen>"
                   "<policy context=\"default\"><allow send_destination=\"*\"/>"
                   "<allow receive_sender=\"*\"/><allow own=\"*\"/></policy></busconfig>",
                   directory);
    (void)snprintf(path, sizeof(path), "%s/bus.conf", directory);
    FILE* file = fopen(path, "w");
    (void)fputs(config, file);
    (void)fclose(file);
    char argument[300];
    (void)snprintf(argument, sizeof(argument), "--config-file=%s", path);
    char* arguments[] = {(char*)"/usr/bin/dbus-daemon", argument, (char*)"--nofork",
                         (char*)"--nopidfile", nullptr};
    if (posix_spawn(&fake->daemon, arguments[0], nullptr, nullptr, arguments, environ) != 0)
    {
        fake->daemon = 0;
        return false;
    }
    (void)snprintf(path, sizeof(path), "%s/bus", directory);
    struct stat status;
    for (int i = 0; i < 500 && stat(path, &status) != 0; i++)
    {
        struct timespec pause = {0, 10000000};
        (void)nanosleep(&pause, nullptr);
    }
    (void)snprintf(config, sizeof(config), "unix:path=%s", path);
    (void)setenv("DBUS_SESSION_BUS_ADDRESS", config, 1);
    fake->connection = FakeLoad(fake) ? fake->busGet(0, nullptr) : nullptr;
    if (fake->connection == nullptr)
    {
        return false;
    }
    fake->setExit(fake->connection, 0);
    // 4: do not queue; 1: the primary owner.
    return fake->requestName(fake->connection, "org.freedesktop.FileManager1", 4, nullptr) == 1 &&
           fake->requestName(fake->connection, "org.freedesktop.ScreenSaver", 4, nullptr) == 1 &&
           fake->requestName(fake->connection, "org.freedesktop.portal.Desktop", 4, nullptr) == 1;
}

// The string argument an iterator is at, into a buffer; it moves on.
static inline void FakeString(FakeBus* fake, void* iter, char* out, size_t size)
{
    const char* text = "";
    if (fake->argType(iter) == 's')
    {
        fake->getBasic(iter, (void*)&text);
    }
    (void)snprintf(out, size, "%s", text);
    (void)fake->next(iter);
}

// Answers ShowItems: its URIs and startup id written down.
static inline void* FakeShow(FakeBus* fake, void* message)
{
    void* iter[16];
    void* items[16];
    fake->item[0] = '\0';
    if (fake->iterInit(message, iter) && fake->argType(iter) == 'a')
    {
        fake->recurse(iter, items);
        FakeString(fake, items, fake->item, sizeof(fake->item));
        (void)fake->next(iter);
        FakeString(fake, iter, fake->startup, sizeof(fake->startup));
    }
    fake->shown++;
    return fake->refuseShow ? fake->newError(message, "org.freedesktop.DBus.Error.Failed", "no")
                            : fake->newReturn(message);
}

static inline void* FakeInhibit(FakeBus* fake, void* message)
{
    void* iter[16];
    if (fake->refuseInhibit || !fake->iterInit(message, iter))
    {
        return fake->newError(message, "org.freedesktop.DBus.Error.NotSupported", "no");
    }
    FakeString(fake, iter, fake->application, sizeof(fake->application));
    FakeString(fake, iter, fake->reason, sizeof(fake->reason));
    fake->inhibited++;
    void* reply = fake->newReturn(message);
    uint32_t cookie = FAKE_COOKIE;
    fake->initAppend(reply, iter);
    (void)fake->appendBasic(iter, 'u', (const void*)&cookie);
    return reply;
}

static inline void* FakeUninhibit(FakeBus* fake, void* message)
{
    void* iter[16];
    uint32_t cookie = 0;
    if (fake->iterInit(message, iter) && fake->argType(iter) == 'u')
    {
        fake->getBasic(iter, (void*)&cookie);
    }
    fake->inhibited -= cookie == FAKE_COOKIE ? 1 : 0;
    return fake->newReturn(message);
}

static inline void* FakePortalInhibit(FakeBus* fake, void* message)
{
    void* iter[16];
    if (fake->iterInit(message, iter))
    {
        (void)fake->next(iter);
        if (fake->argType(iter) == 'u')
        {
            fake->getBasic(iter, (void*)&fake->flags);
        }
    }
    fake->portalHeld = true;
    void* reply = fake->newReturn(message);
    const char* handle = FAKE_HANDLE;
    fake->initAppend(reply, iter);
    (void)fake->appendBasic(iter, 'o', (const void*)&handle);
    return reply;
}

static inline void* FakeAnswer(FakeBus* fake, void* message)
{
    if (fake->isCall(message, "org.freedesktop.FileManager1", "ShowItems"))
    {
        return FakeShow(fake, message);
    }
    if (fake->isCall(message, "org.freedesktop.ScreenSaver", "Inhibit"))
    {
        return FakeInhibit(fake, message);
    }
    if (fake->isCall(message, "org.freedesktop.ScreenSaver", "UnInhibit"))
    {
        return FakeUninhibit(fake, message);
    }
    if (fake->isCall(message, "org.freedesktop.portal.Inhibit", "Inhibit"))
    {
        return FakePortalInhibit(fake, message);
    }
    if (fake->isCall(message, "org.freedesktop.portal.Request", "Close"))
    {
        fake->portalHeld = fake->portalHeld && strcmp(fake->path(message), FAKE_HANDLE) != 0;
        return fake->newReturn(message);
    }
    return nullptr;
}

// Answers what the services were asked.
static inline void FakePump(FakeBus* fake)
{
    if (fake == nullptr || fake->connection == nullptr)
    {
        return;
    }
    (void)fake->readWrite(fake->connection, 0);
    for (void* message = fake->pop(fake->connection); message != nullptr;
         message = fake->pop(fake->connection))
    {
        void* reply = FakeAnswer(fake, message);
        if (reply != nullptr)
        {
            (void)fake->send(fake->connection, reply, nullptr);
            fake->unref(reply);
        }
        fake->unref(message);
    }
    fake->flush(fake->connection);
}

static inline void FakeStop(FakeBus* fake)
{
    if (fake->connection != nullptr)
    {
        fake->close(fake->connection);
        fake->unrefConnection(fake->connection);
    }
    if (fake->library != nullptr)
    {
        (void)dlclose(fake->library);
    }
    if (fake->daemon != 0)
    {
        (void)kill(fake->daemon, SIGTERM);
        (void)waitpid(fake->daemon, nullptr, 0);
    }
    (void)unsetenv("DBUS_SESSION_BUS_ADDRESS");
}

#endif // MAUL_WINDOW_TEST_LINUX_BUS_FAKE_H
