// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A session bus of the test's own for the Linux services tests: a
// dbus-daemon with nothing to start on demand, and on the test's own
// connection through libdbus, stand-ins for the file manager
// (FileManager1's ShowItems), the screensaver (Inhibit and UnInhibit)
// and the desktop portal (Inhibit, FileChooser's OpenFile and SaveFile
// answered by a Response when the test says, Close on a request, and
// Settings' ReadAll answered with a desktop's settings when the test
// says, and SettingChanged; PowerProfileMonitor's power-saver-enabled),
// each writing down what it was asked and answering, or refusing as
// told. Started as a system bus, it plays UPower (OnBattery) instead.

#ifndef MAUL_WINDOW_TEST_LINUX_BUS_FAKE_H
#define MAUL_WINDOW_TEST_LINUX_BUS_FAKE_H

#include <dlfcn.h>
#include <fcntl.h>
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
    void* (*openPrivate)(const char* address, void* error);
    unsigned (*busRegister)(void* connection, void* error);
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
    const char* (*sender)(void* message);
    const char* (*member)(void* message);
    void* (*newSignal)(const char* path, const char* interface, const char* name);
    unsigned (*setDestination)(void* message, const char* destination);
    unsigned (*openContainer)(void* iter, int type, const char* signature, void* sub);
    unsigned (*closeContainer)(void* iter, void* sub);
    void (*getFixedArray)(void* iter, void* values, int* count);
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
    // The file chooser: what it was asked last, as text; the request it
    // answers on and its caller; whether that request was closed, and
    // whether it refuses as a missing portal would.
    char chooser[1024];
    char handle[192];
    char caller[64];
    int chosen;
    bool closed;
    bool refuseChooser;
    // The settings: which desktop's ReadAll answers with (fakeNoSettings
    // for no answer), and how often it was asked.
    int desktop;
    int readAlls;
    // The power: power-saver-enabled and OnBattery, -1 to refuse Get.
    int saver;
    int battery;
    // Started as the system bus.
    bool system;
    // Another stand-in's answers, tried after these (linux_ime_fake.h).
    void* (*more)(struct FakeBus* fake, void* message);
} FakeBus;

enum
{
    fakeNoSettings,
    fakeGnome,
    fakeKde,
};

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
           FAKE_FIND(openPrivate, dbus_connection_open_private) &&
           FAKE_FIND(busRegister, dbus_bus_register) &&
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
           FAKE_FIND(setExit, dbus_connection_set_exit_on_disconnect) &&
           FAKE_FIND(sender, dbus_message_get_sender) &&
           FAKE_FIND(member, dbus_message_get_member) &&
           FAKE_FIND(newSignal, dbus_message_new_signal) &&
           FAKE_FIND(setDestination, dbus_message_set_destination) &&
           FAKE_FIND(openContainer, dbus_message_iter_open_container) &&
           FAKE_FIND(closeContainer, dbus_message_iter_close_container) &&
           FAKE_FIND(getFixedArray, dbus_message_iter_get_fixed_array);
}

// Starts the bus in a directory, names it in DBUS_SESSION_BUS_ADDRESS
// (DBUS_SYSTEM_BUS_ADDRESS for fake->system), and takes the services'
// names: false where there is no dbus-daemon or libdbus-1.
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
    // Its output goes nowhere, so a test that dies never leaves it
    // holding the pipes of whoever runs the test.
    posix_spawn_file_actions_t actions;
    (void)posix_spawn_file_actions_init(&actions);
    (void)posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    (void)posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    int spawned = posix_spawn(&fake->daemon, arguments[0], &actions, nullptr, arguments, environ);
    (void)posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0)
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
    (void)setenv(fake->system ? "DBUS_SYSTEM_BUS_ADDRESS" : "DBUS_SESSION_BUS_ADDRESS", config, 1);
    // libdbus keeps the addresses it saw first for its buses, so the
    // system bus is opened by its address.
    fake->connection = !FakeLoad(fake) ? nullptr
                       : fake->system  ? fake->openPrivate(config, nullptr)
                                       : fake->busGet(0, nullptr);
    if (fake->connection == nullptr ||
        (fake->system && !fake->busRegister(fake->connection, nullptr)))
    {
        return false;
    }
    fake->setExit(fake->connection, 0);
    // 4: do not queue; 1: the primary owner.
    if (fake->system)
    {
        return fake->requestName(fake->connection, "org.freedesktop.UPower", 4, nullptr) == 1;
    }
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

// Appends text to what the chooser was asked.
static inline void FakeNote(FakeBus* fake, const char* format, const char* text)
{
    size_t length = strlen(fake->chooser);
    (void)snprintf(fake->chooser + length, sizeof(fake->chooser) - length, format, text);
}

// Notes a filter, (sa(us)): "name:glob,glob".
static inline void FakeFilter(FakeBus* fake, void* filter)
{
    void* fields[16];
    void* patterns[16];
    void* pattern[16];
    char text[256];
    fake->recurse(filter, fields);
    FakeString(fake, fields, text, sizeof(text));
    FakeNote(fake, "%s:", text);
    fake->recurse(fields, patterns);
    for (bool more = fake->argType(patterns) == 'r'; more; more = fake->next(patterns))
    {
        fake->recurse(patterns, pattern);
        (void)fake->next(pattern);
        FakeString(fake, pattern, text, sizeof(text));
        FakeNote(fake, "%s,", text);
    }
}

// Notes an option of a{sv} as "key=value;".
static inline void FakeOption(FakeBus* fake, const char* key, void* value)
{
    char text[256] = {0};
    void* inner[16];
    FakeNote(fake, "%s=", key);
    if (fake->argType(value) == 's')
    {
        FakeString(fake, value, text, sizeof(text));
        FakeNote(fake, "%s", text);
    }
    else if (fake->argType(value) == 'b')
    {
        unsigned yes = 0;
        fake->getBasic(value, (void*)&yes);
        FakeNote(fake, "%s", yes != 0 ? "true" : "false");
    }
    else if (fake->argType(value) == 'r')
    {
        FakeFilter(fake, value);
    }
    else if (strcmp(key, "current_folder") == 0)
    {
        const char* bytes = nullptr;
        int count = 0;
        fake->recurse(value, inner);
        fake->getFixedArray(inner, (void*)&bytes, &count);
        FakeNote(fake, "%s", count > 0 && bytes[count - 1] == '\0' ? bytes : "(no NUL)");
    }
    else if (fake->argType(value) == 'a')
    {
        fake->recurse(value, inner);
        for (bool more = fake->argType(inner) == 'r'; more; more = fake->next(inner))
        {
            FakeFilter(fake, inner);
            FakeNote(fake, "%s", "|");
        }
    }
    FakeNote(fake, "%s", ";");
}

// Answers OpenFile or SaveFile: what it was asked noted, a request made
// for the token.
static inline void* FakeChoose(FakeBus* fake, void* message)
{
    if (fake->refuseChooser)
    {
        return fake->newError(message, "org.freedesktop.DBus.Error.UnknownMethod", "no");
    }
    void* iter[16];
    void* options[16];
    void* entry[16];
    void* value[16];
    char text[256];
    char token[64] = "";
    fake->chooser[0] = '\0';
    FakeNote(fake, "%s;", fake->member(message));
    (void)fake->iterInit(message, iter);
    FakeString(fake, iter, text, sizeof(text));
    FakeNote(fake, "parent=%s;", text);
    FakeString(fake, iter, text, sizeof(text));
    FakeNote(fake, "title=%s;", text);
    fake->recurse(iter, options);
    for (bool more = fake->argType(options) == 'e'; more; more = fake->next(options))
    {
        fake->recurse(options, entry);
        FakeString(fake, entry, text, sizeof(text));
        fake->recurse(entry, value);
        if (strcmp(text, "handle_token") == 0)
        {
            FakeString(fake, value, token, sizeof(token));
            continue;
        }
        FakeOption(fake, text, value);
    }
    (void)snprintf(fake->caller, sizeof(fake->caller), "%s", fake->sender(message));
    char sender[64];
    size_t length = 0;
    for (const char* at = fake->caller + 1; *at != '\0' && length < 63; at++)
    {
        sender[length++] = *at == '.' ? '_' : *at;
    }
    sender[length] = '\0';
    (void)snprintf(fake->handle, sizeof(fake->handle),
                   "/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
    fake->closed = false;
    fake->chosen++;
    void* reply = fake->newReturn(message);
    const char* handle = fake->handle;
    fake->initAppend(reply, iter);
    (void)fake->appendBasic(iter, 'o', (const void*)&handle);
    return reply;
}

// Sends the Response to the last request: a code, and file URIs.
static inline void FakeRespond(FakeBus* fake, uint32_t code, const char* const* uris, int count)
{
    void* message = fake->newSignal(fake->handle, "org.freedesktop.portal.Request", "Response");
    void* iter[16];
    void* results[16];
    void* entry[16];
    void* value[16];
    void* array[16];
    const char* key = "uris";
    (void)fake->setDestination(message, fake->caller);
    fake->initAppend(message, iter);
    (void)fake->appendBasic(iter, 'u', (const void*)&code);
    (void)fake->openContainer(iter, 'a', "{sv}", results);
    (void)fake->openContainer(results, 'e', nullptr, entry);
    (void)fake->appendBasic(entry, 's', (const void*)&key);
    (void)fake->openContainer(entry, 'v', "as", value);
    (void)fake->openContainer(value, 'a', "s", array);
    for (int i = 0; i < count; i++)
    {
        (void)fake->appendBasic(array, 's', (const void*)&uris[i]);
    }
    (void)fake->closeContainer(value, array);
    (void)fake->closeContainer(entry, value);
    (void)fake->closeContainer(results, entry);
    (void)fake->closeContainer(iter, results);
    (void)fake->send(fake->connection, message, nullptr);
    fake->unref(message);
    fake->flush(fake->connection);
}

// A variant of a basic type, or of three doubles for type 'r'.
static inline void FakeVariant(FakeBus* fake, void* iter, int type, const void* value)
{
    void* variant[16];
    void* channels[16];
    char signature[2] = {(char)type, '\0'};
    (void)fake->openContainer(iter, 'v', type == 'r' ? "(ddd)" : signature, variant);
    if (type == 'r')
    {
        (void)fake->openContainer(variant, 'r', nullptr, channels);
        for (int i = 0; i < 3; i++)
        {
            (void)fake->appendBasic(channels, 'd', (const void*)&((const double*)value)[i]);
        }
        (void)fake->closeContainer(variant, channels);
    }
    else
    {
        (void)fake->appendBasic(variant, type, value);
    }
    (void)fake->closeContainer(iter, variant);
}

// A namespace's key and value, in its a{sv}.
static inline void FakeSetting(FakeBus* fake, void* keys, const char* key, int type,
                               const void* value)
{
    void* entry[16];
    (void)fake->openContainer(keys, 'e', nullptr, entry);
    (void)fake->appendBasic(entry, 's', (const void*)&key);
    FakeVariant(fake, entry, type, value);
    (void)fake->closeContainer(keys, entry);
}

static inline void FakeSpace(FakeBus* fake, void* spaces, const char* name, void* space, void* keys)
{
    (void)fake->openContainer(spaces, 'e', nullptr, space);
    (void)fake->appendBasic(space, 's', (const void*)&name);
    (void)fake->openContainer(space, 'a', "{sv}", keys);
}

static inline void FakeSpaceEnd(FakeBus* fake, void* spaces, void* space, void* keys)
{
    (void)fake->closeContainer(space, keys);
    (void)fake->closeContainer(spaces, space);
}

// ReadAll: GNOME's dark style, an accent, animations off and large
// text; or KDE's no preference and animations off, its factor as text.
static inline void* FakeReadAll(FakeBus* fake, void* message)
{
    fake->readAlls += 1;
    if (fake->desktop == fakeNoSettings)
    {
        return nullptr;
    }
    void* reply = fake->newReturn(message);
    void* iter[16];
    void* spaces[16];
    void* space[16];
    void* keys[16];
    bool gnome = fake->desktop == fakeGnome;
    uint32_t scheme = gnome ? 1u : 0u;
    const double accent[3] = {0.5, 0.25, 1.0};
    unsigned animations = 0;
    const double scale = 1.25;
    const char* factor = "0.0";
    fake->initAppend(reply, iter);
    (void)fake->openContainer(iter, 'a', "{sa{sv}}", spaces);
    FakeSpace(fake, spaces, "org.freedesktop.appearance", space, keys);
    FakeSetting(fake, keys, "color-scheme", 'u', &scheme);
    if (gnome)
    {
        FakeSetting(fake, keys, "accent-color", 'r', accent);
    }
    FakeSpaceEnd(fake, spaces, space, keys);
    FakeSpace(fake, spaces, gnome ? "org.gnome.desktop.interface" : "org.kde.kdeglobals.KDE", space,
              keys);
    if (gnome)
    {
        FakeSetting(fake, keys, "enable-animations", 'b', &animations);
        FakeSetting(fake, keys, "text-scaling-factor", 'd', &scale);
    }
    else
    {
        FakeSetting(fake, keys, "AnimationDurationFactor", 's', (const void*)&factor);
    }
    FakeSpaceEnd(fake, spaces, space, keys);
    (void)fake->closeContainer(iter, spaces);
    return reply;
}

// Tells every listener of a setting's change.
static inline void FakeSettingChanged(FakeBus* fake, const char* space, const char* key, int type,
                                      const void* value)
{
    void* message = fake->newSignal("/org/freedesktop/portal/desktop",
                                    "org.freedesktop.portal.Settings", "SettingChanged");
    void* iter[16];
    fake->initAppend(message, iter);
    (void)fake->appendBasic(iter, 's', (const void*)&space);
    (void)fake->appendBasic(iter, 's', (const void*)&key);
    FakeVariant(fake, iter, type, value);
    (void)fake->send(fake->connection, message, nullptr);
    fake->unref(message);
    fake->flush(fake->connection);
}

// Properties.Get of power-saver-enabled or OnBattery: a boolean, or
// a refusal as told.
static inline void* FakeGet(FakeBus* fake, void* message)
{
    void* iter[16];
    char interface[64] = "";
    char name[64] = "";
    if (fake->iterInit(message, iter))
    {
        FakeString(fake, iter, interface, sizeof(interface));
        FakeString(fake, iter, name, sizeof(name));
    }
    bool saver = strcmp(interface, "org.freedesktop.portal.PowerProfileMonitor") == 0 &&
                 strcmp(name, "power-saver-enabled") == 0;
    bool battery =
        strcmp(interface, "org.freedesktop.UPower") == 0 && strcmp(name, "OnBattery") == 0;
    int value = saver ? fake->saver : battery ? fake->battery : -1;
    if (value < 0)
    {
        return fake->newError(message, "org.freedesktop.DBus.Error.UnknownProperty", "none");
    }
    void* reply = fake->newReturn(message);
    unsigned boolean = (unsigned)value;
    fake->initAppend(reply, iter);
    FakeVariant(fake, iter, 'b', &boolean);
    return reply;
}

// Tells every listener of a boolean property's change.
static inline void FakePropertyChanged(FakeBus* fake, const char* path, const char* interface,
                                       const char* name, int type, const void* value)
{
    void* message = fake->newSignal(path, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    void* iter[16];
    void* changed[16];
    void* invalidated[16];
    fake->initAppend(message, iter);
    (void)fake->appendBasic(iter, 's', (const void*)&interface);
    (void)fake->openContainer(iter, 'a', "{sv}", changed);
    FakeSetting(fake, changed, name, type, value);
    (void)fake->closeContainer(iter, changed);
    (void)fake->openContainer(iter, 'a', "s", invalidated);
    (void)fake->closeContainer(iter, invalidated);
    (void)fake->send(fake->connection, message, nullptr);
    fake->unref(message);
    fake->flush(fake->connection);
}

static inline void* FakeAnswer(FakeBus* fake, void* message)
{
    if (fake->isCall(message, "org.freedesktop.DBus.Properties", "Get"))
    {
        return FakeGet(fake, message);
    }
    if (fake->isCall(message, "org.freedesktop.portal.Settings", "ReadAll"))
    {
        return FakeReadAll(fake, message);
    }
    if (fake->isCall(message, "org.freedesktop.portal.FileChooser", "OpenFile") ||
        fake->isCall(message, "org.freedesktop.portal.FileChooser", "SaveFile"))
    {
        return FakeChoose(fake, message);
    }
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
        fake->closed = fake->closed || strcmp(fake->path(message), fake->handle) == 0;
        return fake->newReturn(message);
    }
    return fake->more != nullptr ? fake->more(fake, message) : nullptr;
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
    (void)unsetenv(fake->system ? "DBUS_SYSTEM_BUS_ADDRESS" : "DBUS_SESSION_BUS_ADDRESS");
}

// Removes what the bus left in its directory.
static inline void FakeClean(const char* directory)
{
    char path[256];
    (void)snprintf(path, sizeof(path), "%s/bus.conf", directory);
    (void)remove(path);
    (void)snprintf(path, sizeof(path), "%s/bus", directory);
    (void)remove(path);
}

#endif // MAUL_WINDOW_TEST_LINUX_BUS_FAKE_H
