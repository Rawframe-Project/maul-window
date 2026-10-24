// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The desktop's languages from AccountsService and plasma-localerc.

#include "linux_languages.h"

#include "monotonic.h"

#include <errno.h>
#include <fcntl.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROPERTIES    "org.freedesktop.DBus.Properties"
#define ACCOUNTS      "org.freedesktop.Accounts"
#define ACCOUNTS_PATH "/org/freedesktop/Accounts"
#define USER          "org.freedesktop.Accounts.User"
#define PLASMA        "plasma-localerc"

static bool AppendString(const mwinDBusApi* api, mwinDBusIter* iter, const char* text)
{
    return api->appendBasic(iter, mwin_dbusTypeString, (const void*)&text);
}

// What a source said: its first word is its baseline; a later one that
// differs replaces the preferred locales, the starting list for one that
// names no language. The context tells a change only when the list
// changes.
static void Heard(mwinLinuxLanguages* languages, mwinLinuxLanguageWord* word, const char* tags,
                  size_t length, uint64_t nowNs)
{
    bool first = !word->said;
    bool same = word->said && word->length == length && memcmp(word->tags, tags, length) == 0;
    word->said = true;
    word->length = (uint16_t)length;
    if (length > 0)
    {
        memcpy(word->tags, tags, length);
    }
    if (first || same)
    {
        return;
    }
    const char* locales = length > 0 ? tags : languages->start;
    size_t count = length > 0 ? length : languages->startLength;
    (void)mwinSetLocales(languages->context, locales, count, nowNs);
}

// The tags of a user's properties, a{sv}: its Languages where they name
// a language, else its Language. False where neither is there in the
// shape expected (as, s): nothing said.
static bool UserLocales(const mwinLinuxLanguages* languages, const mwinDBusApi* api,
                        mwinDBusIter* properties, char* out, size_t* lengthOut)
{
    char single[MWIN_LINUX_LOCALE_BYTES];
    size_t fromList = 0;
    size_t fromOne = 0;
    bool list = false;
    bool one = false;
    for (; api->argType(properties) == mwin_dbusTypeDictEntry; (void)api->next(properties))
    {
        mwinDBusIter entry;
        mwinDBusIter value;
        const char* name = nullptr;
        if (!mwinBusEntry(api, properties, &name, &entry) ||
            api->argType(&entry) != mwin_dbusTypeVariant)
        {
            continue;
        }
        api->recurse(&entry, &value);
        if (strcmp(name, "Languages") == 0 && api->argType(&value) == mwin_dbusTypeArray)
        {
            mwinDBusIter names;
            api->recurse(&value, &names);
            // An array of strings, or an empty one; of other types it says
            // nothing.
            int type = api->argType(&names);
            list = list || type == mwin_dbusTypeString || type == 0;
            fromList = 0;
            for (; api->argType(&names) == mwin_dbusTypeString; (void)api->next(&names))
            {
                const char* text = nullptr;
                api->getBasic(&names, (void*)&text);
                fromList =
                    mwinLinuxAddLocales(text, strlen(text), out, fromList, languages->capacity);
            }
        }
        else if (strcmp(name, "Language") == 0 && api->argType(&value) == mwin_dbusTypeString)
        {
            const char* text = nullptr;
            api->getBasic(&value, (void*)&text);
            one = true;
            fromOne = mwinLinuxAddLocales(text, strlen(text), single, 0, languages->capacity);
        }
    }
    if (list && fromList > 0)
    {
        *lengthOut = fromList;
        return true;
    }
    if (one && fromOne > 0)
    {
        memcpy(out, single, fromOne);
    }
    *lengthOut = one ? fromOne : 0;
    return list || one;
}

// Asks for the user's properties.
static void ReadUser(mwinLinuxLanguages* languages, uint64_t nowNs)
{
    mwinLinuxBus* system = languages->system;
    DBusMessage* get = mwinBusMethod(system, ACCOUNTS, languages->user, PROPERTIES, "GetAll");
    if (get == nullptr)
    {
        return;
    }
    mwinDBusIter arguments;
    system->api.iterInitAppend(get, &arguments);
    if (AppendString(&system->api, &arguments, USER))
    {
        (void)mwinBusSend(system, get, &languages->readCall, nowNs);
    }
    else
    {
        system->api.unrefMessage(get);
    }
}

// Listens for the user's changes from AccountsService: PropertiesChanged,
// and the older Changed, which says only that something changed.
static void Listen(mwinLinuxLanguages* languages)
{
    static const char* const rules[] = {
        "type='signal',sender='" ACCOUNTS "',interface='" PROPERTIES "',"
        "member='PropertiesChanged',path='%s',arg0='" USER "'",
        "type='signal',sender='" ACCOUNTS "',interface='" USER "',member='Changed',path='%s'",
    };
    mwinLinuxBus* system = languages->system;
    for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++)
    {
        char rule[256 + MWIN_LINUX_USER_PATH_BYTES];
        (void)snprintf(rule, sizeof(rule), rules[i], languages->user);
        DBusMessage* match = mwinBusMethod(system, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                           "org.freedesktop.DBus", "AddMatch");
        if (match != nullptr)
        {
            mwinDBusIter arguments;
            system->api.iterInitAppend(match, &arguments);
            (void)AppendString(&system->api, &arguments, rule);
            mwinBusTell(system, match);
        }
    }
}

// The user's properties, a{sv}, once they came.
static void TakeProperties(mwinLinuxLanguages* languages, uint64_t nowNs)
{
    mwinLinuxBus* system = languages->system;
    bool failed = false;
    DBusMessage* reply = mwinBusAnswer(system, &languages->readCall, nowNs, &failed);
    mwinDBusIter arguments;
    mwinDBusIter properties;
    char tags[MWIN_LINUX_LOCALE_BYTES];
    size_t length = 0;
    if (reply != nullptr && system->api.iterInit(reply, &arguments) &&
        system->api.argType(&arguments) == mwin_dbusTypeArray)
    {
        system->api.recurse(&arguments, &properties);
        if (UserLocales(languages, &system->api, &properties, tags, &length))
        {
            Heard(languages, &languages->gnome, tags, length, nowNs);
        }
    }
    if (reply != nullptr)
    {
        system->api.unrefMessage(reply);
    }
}

// The user's signals: PropertiesChanged(s interface, a{sv} changed, as
// invalidated) of its interface, or Changed, after which its properties
// are asked for again. Another connection's are left alone: any program
// may send a signal on the system bus, to every listener or to this one.
static mwinDBusHandled Filter(DBusConnection* connection, DBusMessage* message, void* data)
{
    (void)connection;
    mwinLinuxLanguages* languages = data;
    const mwinDBusApi* api = &languages->system->api;
    const char* path = api->path(message);
    const char* sender = api->sender(message);
    if (languages->user[0] == '\0' || path == nullptr || strcmp(path, languages->user) != 0 ||
        strcmp(sender != nullptr ? sender : "", languages->owner) != 0)
    {
        return mwin_dbusNotHandled;
    }
    if (api->isSignal(message, USER, "Changed"))
    {
        // An answer on its way may be older than the change.
        mwinBusDrop(languages->system, &languages->readCall);
        ReadUser(languages, mwinMonotonicNow());
        return mwin_dbusNotHandled;
    }
    mwinDBusIter arguments;
    mwinDBusIter changed;
    const char* interface = nullptr;
    if (!api->isSignal(message, PROPERTIES, "PropertiesChanged") ||
        !api->iterInit(message, &arguments) || api->argType(&arguments) != mwin_dbusTypeString)
    {
        return mwin_dbusNotHandled;
    }
    api->getBasic(&arguments, (void*)&interface);
    if (strcmp(interface, USER) != 0 || !api->next(&arguments) ||
        api->argType(&arguments) != mwin_dbusTypeArray)
    {
        return mwin_dbusNotHandled;
    }
    api->recurse(&arguments, &changed);
    // The properties asked for before come first, where they came before
    // this change.
    if (languages->readCall.pending != nullptr)
    {
        TakeProperties(languages, mwinMonotonicNow());
    }
    char tags[MWIN_LINUX_LOCALE_BYTES];
    size_t length = 0;
    if (UserLocales(languages, api, &changed, tags, &length))
    {
        Heard(languages, &languages->gnome, tags, length, mwinMonotonicNow());
    }
    return mwin_dbusNotHandled;
}

// Reads plasma-localerc: what it says, no language where there is none
// or it names none. A file that cannot be read, is no regular file, is
// too long or holds a NUL is left alone.
static void ReadPlasma(mwinLinuxLanguages* languages, uint64_t nowNs)
{
    int file = open(languages->file, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (file < 0)
    {
        if (errno == ENOENT)
        {
            Heard(languages, &languages->kde, "", 0, nowNs);
        }
        return;
    }
    char text[MWIN_LINUX_PLASMA_BYTES + 1];
    size_t length = 0;
    struct stat status;
    bool regular = fstat(file, &status) == 0 && S_ISREG(status.st_mode);
    while (regular && length < sizeof(text))
    {
        ssize_t got = read(file, text + length, sizeof(text) - length);
        if (got <= 0)
        {
            regular = got == 0;
            break;
        }
        length += (size_t)got;
    }
    (void)close(file);
    char tags[MWIN_LINUX_LOCALE_BYTES];
    size_t count = 0;
    if (regular && length <= MWIN_LINUX_PLASMA_BYTES &&
        mwinLinuxPlasmaLocales(text, length, tags, languages->capacity, &count))
    {
        Heard(languages, &languages->kde, tags, count, nowNs);
    }
}

// Watches the configuration folder for plasma-localerc's writes and
// renames, and reads it for its baseline; nothing without the folder.
static void Watch(mwinLinuxLanguages* languages, uint64_t nowNs)
{
    const char* config = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    char folder[MWIN_LINUX_CONFIG_PATH_BYTES];
    // XDG_CONFIG_HOME counts only as an absolute path.
    int written =
        config != nullptr && config[0] == '/' ? snprintf(folder, sizeof(folder), "%s", config)
        : home != nullptr && home[0] == '/'   ? snprintf(folder, sizeof(folder), "%s/.config", home)
                                              : -1;
    if (written < 0 || (size_t)written >= sizeof(folder))
    {
        return;
    }
    (void)snprintf(languages->file, sizeof(languages->file), "%s/" PLASMA, folder);
    int watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch < 0)
    {
        return;
    }
    if (inotify_add_watch(watch, folder, IN_CLOSE_WRITE | IN_MOVED_TO) < 0)
    {
        (void)close(watch);
        return;
    }
    languages->watch = watch;
    ReadPlasma(languages, nowNs);
}

void mwinLanguagesStart(mwinLinuxLanguages* languages, mwinContext* context, mwinLinuxBus* system,
                        const char* start, size_t startLength, uint64_t nowNs)
{
    *languages = (mwinLinuxLanguages){.context = context, .system = system, .watch = -1};
    size_t capacity = context->limits.localeBytes < MWIN_LINUX_LOCALE_BYTES
                          ? context->limits.localeBytes
                          : MWIN_LINUX_LOCALE_BYTES;
    languages->capacity = (uint16_t)capacity;
    languages->startLength = (uint16_t)(startLength < capacity ? startLength : capacity);
    if (languages->startLength > 0)
    {
        memcpy(languages->start, start, languages->startLength);
    }
    if (mwinBusConnect(system) &&
        system->api.addFilter(system->connection, Filter, languages, nullptr))
    {
        languages->listening = true;
        DBusMessage* find =
            mwinBusMethod(system, ACCOUNTS, ACCOUNTS_PATH, ACCOUNTS, "FindUserById");
        int64_t user = (int64_t)getuid();
        mwinDBusIter arguments;
        if (find != nullptr)
        {
            system->api.iterInitAppend(find, &arguments);
            if (system->api.appendBasic(&arguments, mwin_dbusTypeInt64, &user))
            {
                (void)mwinBusSend(system, find, &languages->userCall, nowNs);
            }
            else
            {
                system->api.unrefMessage(find);
            }
        }
    }
    Watch(languages, nowNs);
}

// The user's path and the connection that gave it, once they came: then
// its changes are listened for and its properties asked for.
static void TakeUser(mwinLinuxLanguages* languages, uint64_t nowNs)
{
    mwinLinuxBus* system = languages->system;
    bool failed = false;
    DBusMessage* reply = mwinBusAnswer(system, &languages->userCall, nowNs, &failed);
    mwinDBusIter arguments;
    const char* path = nullptr;
    if (reply != nullptr && system->api.iterInit(reply, &arguments) &&
        system->api.argType(&arguments) == mwin_dbusTypeObjectPath)
    {
        system->api.getBasic(&arguments, (void*)&path);
    }
    const char* owner = reply != nullptr ? system->api.sender(reply) : nullptr;
    owner = owner != nullptr ? owner : "";
    if (path != nullptr && strlen(path) < sizeof(languages->user) &&
        strlen(owner) < sizeof(languages->owner))
    {
        memcpy(languages->user, path, strlen(path) + 1);
        memcpy(languages->owner, owner, strlen(owner) + 1);
        Listen(languages);
        ReadUser(languages, nowNs);
    }
    if (reply != nullptr)
    {
        system->api.unrefMessage(reply);
    }
}

// Drains the watch, and reads plasma-localerc once if it was written or
// renamed into place, or if the watch's queue overflowed.
static void PumpWatch(mwinLinuxLanguages* languages, uint64_t nowNs)
{
    alignas(struct inotify_event) char events[4096];
    bool changed = false;
    for (;;)
    {
        ssize_t got = read(languages->watch, events, sizeof(events));
        if (got <= 0)
        {
            break;
        }
        for (size_t at = 0; at + sizeof(struct inotify_event) <= (size_t)got;)
        {
            const struct inotify_event* event = (const struct inotify_event*)(events + at);
            changed = changed || (event->mask & IN_Q_OVERFLOW) != 0 ||
                      (event->len > 0 && strcmp(event->name, PLASMA) == 0);
            at += sizeof(struct inotify_event) + event->len;
        }
    }
    if (changed)
    {
        ReadPlasma(languages, nowNs);
    }
}

void mwinLanguagesPump(mwinLinuxLanguages* languages, uint64_t nowNs)
{
    if (languages->userCall.pending != nullptr)
    {
        TakeUser(languages, nowNs);
    }
    if (languages->readCall.pending != nullptr)
    {
        TakeProperties(languages, nowNs);
    }
    if (languages->watch >= 0)
    {
        PumpWatch(languages, nowNs);
    }
}

void mwinLanguagesStop(mwinLinuxLanguages* languages)
{
    if (languages->context == nullptr)
    {
        return;
    }
    mwinBusDrop(languages->system, &languages->userCall);
    mwinBusDrop(languages->system, &languages->readCall);
    if (languages->listening)
    {
        languages->system->api.removeFilter(languages->system->connection, Filter, languages);
        languages->listening = false;
    }
    if (languages->watch >= 0)
    {
        (void)close(languages->watch);
        languages->watch = -1;
    }
}
