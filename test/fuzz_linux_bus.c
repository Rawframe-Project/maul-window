// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the Linux services' reading of what comes over D-Bus from
// whatever owns a bus name: the Settings portal's ReadAll and
// SettingChanged (linux_settings.c), the file chooser's handle and its
// Response with the URIs (linux_dialog.c), the power profile monitor's
// and UPower's Get and PropertiesChanged (linux_power.c), AccountsService's
// user, its GetAll, PropertiesChanged and Changed, and its name's owner
// changing (linux_languages.c),
// and Fcitx 5's and IBus's input contexts, key answers, committed text
// and compositions (linux_ime.c). Each input runs on a context of the test
// backend, its dialog requests going to the portal code, with the
// modules on stand-in connections: the real libdbus builds and reads the
// messages, and only the functions that would reach a bus are replaced.
// The input is a script of replies to the calls the modules made (of the
// shape asked, of another, or an error), signals, dialogs, focus and keys
// for the input method, pumps and deadlines, and windows destroyed. Each
// message's types follow the shape the module expects with others put in
// their place now and then, and every message is marshalled and read
// back as libdbus reads one off a bus, so only messages a bus could
// deliver reach the modules. This libdbus checks its arguments and ends
// the process on a call that breaks its rules, as a getter of the wrong
// type, which is how a module's mistake shows.
//
// A value of an unexpected type must be left alone; no call may be used
// after it was let go, and every call and filter is let go by the end;
// every request completes exactly once, and a dialog's paths are
// absolute UTF-8 within the limits; committed text and compositions are
// UTF-8, a composition's caret, selection and segments within its text
// on character boundaries; the facts the program reads are in range,
// the text scale finite and above zero; the preferred locales are never
// empty, and hold tags of letters, digits and hyphens between single
// commas; and the program reads each
// key at most once, in the order offered, and only for the window it
// was offered for.

#include "linux_dialog.h"
#include "linux_ime.h"
#include "linux_languages.h"
#include "linux_power.h"
#include "linux_settings.h"
#include "monotonic.h"

#include "maul-unicode/encoding.h"
#include "maul-window/dialog.h"
#include "maul-window/system.h"
#include "maul-window/window.h"

#include <dlfcn.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOWS 3
// The context's window slots: a destroyed window's slot is free again
// only once its end is drained, after its replacement is made.
#define SLOTS        8
#define FILES        8u
#define DIALOG_BYTES 4096u
// The calls a run can make: more than an input of 256 bytes asks for.
#define CALLS   512
#define FILTERS 8
#define TRACKED 128
#define KEYS    1024
// How deep variants may nest in a value.
#define DEPTH 6

static const uint8_t* s_data;
static size_t s_size;

static void ExpectAt(bool condition, const char* text, int line)
{
    if (!condition)
    {
        (void)fprintf(stderr, "expected at line %d: %s\n", line, text);
        abort();
    }
}

#define Expect(condition) ExpectAt((condition), #condition, __LINE__)

static uint8_t Byte(void)
{
    if (s_size == 0)
    {
        return 0;
    }
    s_size--;
    return *s_data++;
}

// What libdbus offers to build a message and read it back, beyond the
// functions the library's table holds.
static struct
{
    DBusMessage* (*make)(int type);
    DBusMessage* (*signal)(const char* path, const char* interface, const char* name);
    mwinDBusBool (*setErrorName)(DBusMessage* message, const char* name);
    mwinDBusBool (*setReplySerial)(DBusMessage* message, uint32_t serial);
    mwinDBusBool (*setSender)(DBusMessage* message, const char* sender);
    void (*setSerial)(DBusMessage* message, uint32_t serial);
    mwinDBusBool (*marshal)(DBusMessage* message, char** bytes, int* length);
    DBusMessage* (*demarshal)(const char* bytes, int length, void* error);
    void (*release)(void* memory);
    const char* (*member)(DBusMessage* message);
    const char* (*signature)(DBusMessage* message);
} s_dbus;

// MWIN_FUZZ_TRACE set prints each message a module is handed, to read
// what a finding sent.
static bool s_trace;

// The stand-in connections, by address.
static char s_sessionConnection;
static char s_systemConnection;
#define SESSION ((DBusConnection*)&s_sessionConnection)
#define SYSTEM  ((DBusConnection*)&s_systemConnection)

// A call a module made: what it asked, its reply once the script gave
// one, and whether the module took the reply and let the call go.
typedef enum CallKind
{
    callOther,
    callReadAll,
    callGet,
    callChoose,
    callCreate,
    callKey,
    callFindUser,
    callGetAll,
} CallKind;

typedef struct Call
{
    CallKind kind;
    DBusConnection* connection;
    DBusMessage* reply;
    bool answered;
    bool taken;
    bool released;
} Call;

static Call s_calls[CALLS];
static uint32_t s_callCount;

typedef struct Filter
{
    DBusConnection* connection;
    mwinDBusFilter* filter;
    void* data;
} Filter;

static Filter s_filters[FILTERS];
static uint32_t s_filterCount;

static mwinLinuxBus s_session;
static mwinLinuxSettings s_settings;
static mwinLinuxPower s_power;
static mwinLinuxBus s_system;
static mwinLinuxLanguages s_languages;
static mwinLinuxDialogs s_dialogs;
static mwinLinuxIme s_ime;

static mwinBackendOps s_ops;
static const mwinBackendOps* s_test;

static struct
{
    mwinRequestId id;
    uint32_t completions;
} s_requests[TRACKED];
static uint32_t s_made;

// The keys offered in a run, by the serial each carries as its time
// (from 1): its window and its type. The queue orders records by its
// own sequence, never by their time. The serial of the last key read.
static struct
{
    mwinWindowId window;
    mwinEventType type;
} s_keys[KEYS];
static uint32_t s_keyCount;
static uint64_t s_keyRead;

static Call* CallOf(DBusPendingCall* pending)
{
    Call* call = (Call*)pending;
    Expect(call >= s_calls && call < s_calls + s_callCount && !call->released);
    return call;
}

static mwinDBusBool SendWithReply(DBusConnection* connection, DBusMessage* message,
                                  DBusPendingCall** pending, int timeoutMs)
{
    (void)timeoutMs;
    Expect(connection == SESSION || connection == SYSTEM);
    *pending = nullptr;
    if (s_callCount == CALLS)
    {
        return 0;
    }
    const char* member = s_dbus.member(message);
    Call* call = &s_calls[s_callCount++];
    *call = (Call){.connection = connection};
    call->kind = strcmp(member, "ReadAll") == 0              ? callReadAll
                 : strcmp(member, "Get") == 0                ? callGet
                 : strcmp(member, "OpenFile") == 0           ? callChoose
                 : strcmp(member, "SaveFile") == 0           ? callChoose
                 : strcmp(member, "CreateInputContext") == 0 ? callCreate
                 : strcmp(member, "ProcessKeyEvent") == 0    ? callKey
                 : strcmp(member, "FindUserById") == 0       ? callFindUser
                 : strcmp(member, "GetAll") == 0             ? callGetAll
                                                             : callOther;
    *pending = (DBusPendingCall*)call;
    return 1;
}

static mwinDBusBool Send(DBusConnection* connection, DBusMessage* message, uint32_t* serial)
{
    (void)message;
    (void)serial;
    Expect(connection == SESSION || connection == SYSTEM);
    return 1;
}

static mwinDBusBool Completed(DBusPendingCall* pending)
{
    return CallOf(pending)->answered;
}

static DBusMessage* StealReply(DBusPendingCall* pending)
{
    Call* call = CallOf(pending);
    Expect(call->answered && !call->taken);
    call->taken = true;
    DBusMessage* reply = call->reply;
    call->reply = nullptr;
    return reply;
}

static void Cancel(DBusPendingCall* pending)
{
    (void)CallOf(pending);
}

static void UnrefPending(DBusPendingCall* pending)
{
    Call* call = CallOf(pending);
    call->released = true;
    if (call->reply != nullptr)
    {
        s_session.api.unrefMessage(call->reply);
        call->reply = nullptr;
    }
}

static mwinDBusBool AddFilter(DBusConnection* connection, mwinDBusFilter* filter, void* data,
                              void (*release)(void* data))
{
    Expect(release == nullptr && s_filterCount < FILTERS);
    s_filters[s_filterCount++] = (Filter){connection, filter, data};
    return 1;
}

static void RemoveFilter(DBusConnection* connection, mwinDBusFilter* filter, void* data)
{
    for (uint32_t i = 0; i < s_filterCount; i++)
    {
        Filter* found = &s_filters[i];
        if (found->connection == connection && found->filter == filter && found->data == data)
        {
            *found = s_filters[--s_filterCount];
            return;
        }
    }
    Expect(!"a filter removed that was never added");
}

static const char* UniqueName(DBusConnection* connection)
{
    (void)connection;
    return ":1.42";
}

static mwinDBusBool ReadWrite(DBusConnection* connection, int timeoutMs)
{
    (void)connection;
    (void)timeoutMs;
    return 1;
}

static int Dispatch(DBusConnection* connection)
{
    (void)connection;
    // DBUS_DISPATCH_COMPLETE.
    return 1;
}

static void Flush(DBusConnection* connection)
{
    (void)connection;
}

static void CloseConnection(DBusConnection* connection)
{
    (void)connection;
}

// A bus of libdbus's own whose connection is a stand-in.
static void Join(mwinLinuxBus* bus, DBusConnection* connection)
{
    Expect(mwinLoadDBus(&bus->api) == mwin_success);
    bus->api.sendWithReply = SendWithReply;
    bus->api.send = Send;
    bus->api.completed = Completed;
    bus->api.stealReply = StealReply;
    bus->api.cancel = Cancel;
    bus->api.unrefPending = UnrefPending;
    bus->api.addFilter = AddFilter;
    bus->api.removeFilter = RemoveFilter;
    bus->api.uniqueName = UniqueName;
    bus->api.readWrite = ReadWrite;
    bus->api.dispatch = Dispatch;
    bus->api.flush = Flush;
    bus->api.close = CloseConnection;
    bus->api.unrefConnection = CloseConnection;
    bus->tried = true;
    bus->connection = connection;
}

// Words the strings of a message take most of the time: the names the
// modules look for, and some they do not.
static const char* const s_words[] = {
    "org.freedesktop.appearance",
    "org.gnome.desktop.interface",
    "org.kde.kdeglobals.KDE",
    "color-scheme",
    "reduced-motion",
    "accent-color",
    "enable-animations",
    "text-scaling-factor",
    "AnimationDurationFactor",
    "0",
    "0.0",
    "1.5",
    ".",
    "org.freedesktop.portal.PowerProfileMonitor",
    "power-saver-enabled",
    "org.freedesktop.UPower",
    "OnBattery",
    "org.freedesktop.Accounts.User",
    "Languages",
    "Language",
    "de_DE.UTF-8",
    "sr_RS@latin",
    "C",
    "uris",
    "choices",
    "file:///tmp/a",
    "file:///tmp/%C3%A9%20b",
    "file:///tmp/%FF",
    "file:///a%00b",
    "file:///%2",
    "file://host/tmp/a",
    "file://localhost/tmp/a",
    "https://example.com/a",
    "file:relative",
    "IBusText",
    "IBusAttrList",
    "IBusAttribute",
    "",
    "a",
    "\xC3\xA9t\xC3\xA9",
    "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",
    "\xF0\x9F\x98\x80",
};

// Units of UTF-8 a long string repeats: one to four bytes each.
static const char* const s_units[] = {"a", "\xC3\xA9", "\xE6\x97\xA5", "\xF0\x9F\x98\x80"};

static char s_text[16384];

// A string of the input's: a word; bytes of the input's, a word in their
// place where they are not UTF-8, which libdbus refuses to carry; a unit
// repeated past the modules' bounds; or a file URI that long.
static const char* Text(void)
{
    uint8_t shape = Byte();
    size_t length = 0;
    switch (shape & 15)
    {
    case 10:
    case 11:
        for (size_t count = Byte() % 32; length < count && s_size > 0; length++)
        {
            uint8_t byte = Byte();
            s_text[length] = (char)(byte != 0 ? byte : 'z');
        }
        if (muniValidateUtf8(s_text, length).status != muni_success)
        {
            return s_words[0];
        }
        break;
    case 12:
    case 13:
    {
        const char* unit = s_units[(shape >> 4) & 3];
        size_t size = strlen(unit);
        for (size_t count = (size_t)Byte() * 4 + 1; count > 0; count--)
        {
            memcpy(s_text + length, unit, size);
            length += size;
        }
        break;
    }
    case 14:
    case 15:
        memcpy(s_text, "file:///", 8);
        length = 8;
        for (size_t count = (size_t)Byte() * 64; count > 0; count--)
        {
            s_text[length++] = (char)('a' + count % 26);
        }
        break;
    default:
        return s_words[Byte() % (sizeof(s_words) / sizeof(s_words[0]))];
    }
    s_text[length] = '\0';
    return s_text;
}

// An object path: a dialog's request, the input context's, the user's,
// others, or a long one.
static const char* Path(void)
{
    uint8_t shape = Byte();
    uint32_t i = shape % 4;
    switch ((shape >> 2) & 7)
    {
    case 0:
    case 1:
        return s_dialogs.dialogs[i].handle[0] == '/' ? s_dialogs.dialogs[i].handle : "/";
    case 2:
    case 3:
        return s_ime.path[0] == '/' ? s_ime.path : "/org/freedesktop/portal/inputcontext/1";
    case 4:
        return "/org/freedesktop/IBus/InputContext_7";
    case 5:
        return (shape & 32) != 0 && s_languages.user[0] == '/' ? s_languages.user : "/";
    default:
        s_text[0] = '/';
        size_t length = 1;
        for (size_t count = (size_t)Byte() + 1; count > 0; count--)
        {
            s_text[length++] = (char)('a' + count % 26);
        }
        s_text[length] = '\0';
        return s_text;
    }
}

// The types a value may take in place of the one a module expects: basic
// ones, and containers the modules recurse into.
static const char* const s_others[] = {"s",     "u",     "i",    "b",     "d",  "o",
                                       "t",     "y",     "v",    "as",    "ad", "a{sv}",
                                       "a{ss}", "a{us}", "(si)", "(ddd)", "ay", "a(si)"};
static const char* const s_basics[] = {"s", "u", "i", "b", "d", "o", "t", "y"};

// The types a variant holds, which each message sets.
static const char* const* s_contents;
static uint32_t s_contentCount;

// Past one complete type.
static const char* Skip(const char* type)
{
    if (*type == 'a')
    {
        return Skip(type + 1);
    }
    if (*type == '(' || *type == '{')
    {
        char close = *type == '(' ? ')' : '}';
        type++;
        while (*type != close)
        {
            type = Skip(type);
        }
    }
    return type + 1;
}

typedef struct Signature
{
    char text[256];
    size_t length;
} Signature;

static void Put(Signature* signature, const char* text, size_t length)
{
    // A signature too long for the room left is cut, and libdbus refuses
    // the message it makes.
    if (signature->length + length < sizeof(signature->text))
    {
        memcpy(signature->text + signature->length, text, length);
        signature->length += length;
        signature->text[signature->length] = '\0';
    }
}

// Copies one complete type, now and then another in its place; a dict
// entry's key stays basic, as D-Bus requires.
static void Mutate(const char** type, Signature* out, bool key)
{
    const char* start = *type;
    *type = Skip(start);
    uint8_t shape = Byte();
    if ((shape & 7) == 0)
    {
        const char* other = key ? s_basics[(shape >> 3) % (sizeof(s_basics) / sizeof(s_basics[0]))]
                                : s_others[(shape >> 3) % (sizeof(s_others) / sizeof(s_others[0]))];
        Put(out, other, strlen(other));
        return;
    }
    const char* inner = start + 1;
    switch (*start)
    {
    case 'a':
        Put(out, "a", 1);
        if (*inner == '{')
        {
            inner++;
            Put(out, "{", 1);
            Mutate(&inner, out, true);
            Mutate(&inner, out, false);
            Put(out, "}", 1);
        }
        else
        {
            Mutate(&inner, out, false);
        }
        break;
    case '(':
        Put(out, "(", 1);
        while (*inner != ')')
        {
            Mutate(&inner, out, false);
        }
        Put(out, ")", 1);
        break;
    default:
        Put(out, start, 1);
        break;
    }
}

static void Value(mwinDBusIter* iter, const char** type, int depth);

static void Basic(mwinDBusIter* iter, int type, const void* value)
{
    (void)s_session.api.appendBasic(iter, type, value);
}

static void Variant(mwinDBusIter* iter, int depth)
{
    Signature content = {0};
    const char* chosen =
        depth < DEPTH && s_contentCount > 0 ? s_contents[Byte() % s_contentCount] : "s";
    Mutate(&chosen, &content, false);
    if (depth >= DEPTH && content.text[0] != 's')
    {
        content = (Signature){"s", 1};
    }
    mwinDBusIter inside;
    if (s_session.api.openContainer(iter, mwin_dbusTypeVariant, content.text, &inside))
    {
        const char* at = content.text;
        Value(&inside, &at, depth + 1);
        (void)s_session.api.closeContainer(iter, &inside);
    }
}

// A value of the type at *type, which it moves past.
static void Value(mwinDBusIter* iter, const char** type, int depth)
{
    static const uint32_t unsigned32[] = {0, 1, 2, 3, 4, 16, 255, UINT32_MAX};
    static const int32_t signed32[] = {0, 1, -1, 3, 8, 16, 24, INT32_MAX, INT32_MIN};
    static const double doubles[] = {
        0.0,         0.5,    1.0, 1.25, 2.0, -1.0, 1e300, (double)INFINITY, -(double)INFINITY,
        (double)NAN, 1e-310, 3.0};
    const char* start = *type;
    *type = Skip(start);
    const mwinDBusApi* api = &s_session.api;
    switch (*start)
    {
    case 's':
    {
        const char* text = Text();
        Basic(iter, mwin_dbusTypeString, &text);
        break;
    }
    case 'o':
    {
        const char* path = Path();
        Basic(iter, mwin_dbusTypeObjectPath, &path);
        break;
    }
    case 'u':
    {
        uint32_t value = unsigned32[Byte() % 8];
        Basic(iter, mwin_dbusTypeUint32, &value);
        break;
    }
    case 'i':
    {
        int32_t value = signed32[Byte() % 9];
        Basic(iter, mwin_dbusTypeInt32, &value);
        break;
    }
    case 'b':
    {
        mwinDBusBool value = Byte() & 1;
        Basic(iter, mwin_dbusTypeBoolean, &value);
        break;
    }
    case 'd':
    {
        double value = doubles[Byte() % 12];
        Basic(iter, mwin_dbusTypeDouble, &value);
        break;
    }
    case 't':
    {
        uint64_t value = Byte();
        Basic(iter, mwin_dbusTypeUint64, &value);
        break;
    }
    case 'y':
    {
        uint8_t value = Byte();
        Basic(iter, mwin_dbusTypeByte, &value);
        break;
    }
    case 'v':
        Variant(iter, depth);
        break;
    case 'a':
    {
        Signature element = {0};
        Put(&element, start + 1, (size_t)(*type - start - 1));
        mwinDBusIter items;
        if (!api->openContainer(iter, mwin_dbusTypeArray, element.text, &items))
        {
            break;
        }
        uint8_t count = Byte();
        // Mostly a few, now and then many.
        count = (count & 0xC0) == 0xC0 ? count & 0x3F : count & 3;
        for (uint8_t i = 0; i < count && s_size > 0; i++)
        {
            const char* at = element.text;
            if (*at == '{')
            {
                mwinDBusIter entry;
                if (api->openContainer(&items, mwin_dbusTypeDictEntry, nullptr, &entry))
                {
                    at++;
                    Value(&entry, &at, depth);
                    Value(&entry, &at, depth);
                    (void)api->closeContainer(&items, &entry);
                }
            }
            else
            {
                Value(&items, &at, depth);
            }
        }
        (void)api->closeContainer(iter, &items);
        break;
    }
    case '(':
    {
        mwinDBusIter fields;
        if (api->openContainer(iter, mwin_dbusTypeStruct, nullptr, &fields))
        {
            const char* at = start + 1;
            while (*at != ')')
            {
                Value(&fields, &at, depth);
            }
            (void)api->closeContainer(iter, &fields);
        }
        break;
    }
    default:
        Expect(!"a type the generator does not know");
    }
}

// Fills a message with arguments of the types given, now and then
// others, and reads it back as a bus would deliver it: NULL when libdbus
// refuses it.
static DBusMessage* Fill(DBusMessage* message, const char* types, const char* const* contents,
                         uint32_t contentCount)
{
    if (message == nullptr)
    {
        return nullptr;
    }
    s_contents = contents;
    s_contentCount = contentCount;
    Signature mutated = {0};
    for (const char* at = types; *at != '\0';)
    {
        Mutate(&at, &mutated, false);
    }
    mwinDBusIter arguments;
    s_session.api.iterInitAppend(message, &arguments);
    for (const char* at = mutated.text; *at != '\0';)
    {
        Value(&arguments, &at, 0);
    }
    s_dbus.setSerial(message, 7);
    char* bytes = nullptr;
    int length = 0;
    DBusMessage* read = nullptr;
    if (s_dbus.marshal(message, &bytes, &length))
    {
        read = s_dbus.demarshal(bytes, length, nullptr);
        s_dbus.release(bytes);
    }
    s_session.api.unrefMessage(message);
    if (s_trace && read != nullptr)
    {
        const char* member = s_dbus.member(read);
        (void)fprintf(stderr, "message %s (%s)\n", member != nullptr ? member : "reply",
                      s_dbus.signature(read));
    }
    return read;
}

#define CONTENTS(list) list, (uint32_t)(sizeof(list) / sizeof(list[0]))

static const char* const s_settingTypes[] = {"u", "b", "d", "s", "(ddd)"};
static const char* const s_propertyTypes[] = {"b", "u", "s"};
static const char* const s_resultTypes[] = {"as", "s", "a(sa(us))"};
static const char* const s_languageTypes[] = {"as", "s"};
// IBus's serialized objects: a text, its attribute list, an attribute.
static const char* const s_ibusTypes[] = {"(sa{sv}sv)", "(sa{sv}av)", "(sa{sv}uuuu)", "s"};

// A reply to a call: of the shape it asks for, now and then another, or
// an error.
static DBusMessage* Reply(const Call* call)
{
    uint8_t shape = Byte();
    if ((shape & 7) == 0)
    {
        DBusMessage* error = s_dbus.make(mwin_dbusMessageError);
        if (error != nullptr)
        {
            (void)s_dbus.setErrorName(error, "org.freedesktop.DBus.Error.Failed");
            (void)s_dbus.setReplySerial(error, 1);
        }
        return Fill(error, "s", nullptr, 0);
    }
    DBusMessage* reply = s_dbus.make(2);
    if (reply != nullptr)
    {
        (void)s_dbus.setReplySerial(reply, 1);
    }
    switch (call->kind)
    {
    case callReadAll:
        return Fill(reply, "a{sa{sv}}", CONTENTS(s_settingTypes));
    case callGet:
        return Fill(reply, "v", CONTENTS(s_propertyTypes));
    case callChoose:
        return Fill(reply, "o", nullptr, 0);
    case callCreate:
        return Fill(reply, (shape & 8) != 0 ? "oay" : "o", nullptr, 0);
    case callKey:
        return Fill(reply, "b", nullptr, 0);
    case callFindUser:
        return Fill(reply, "o", nullptr, 0);
    case callGetAll:
        return Fill(reply, "a{sv}", CONTENTS(s_languageTypes));
    default:
        return Fill(reply, "", nullptr, 0);
    }
}

// Answers a call of a kind not answered yet, the one the input picks.
static void Answer(CallKind kind)
{
    uint32_t open[CALLS];
    uint32_t count = 0;
    for (uint32_t i = 0; i < s_callCount; i++)
    {
        const Call* call = &s_calls[i];
        if (call->kind == kind && !call->answered && !call->released)
        {
            open[count++] = i;
        }
    }
    if (count == 0)
    {
        return;
    }
    Call* call = &s_calls[open[Byte() % count]];
    call->reply = Reply(call);
    // A reply libdbus refused never came: the call waits on.
    call->answered = call->reply != nullptr;
}

// Hands a signal to the filters of a connection, as a dispatch would.
static void Deliver(DBusConnection* connection, DBusMessage* message)
{
    if (message == nullptr)
    {
        return;
    }
    Filter filters[FILTERS];
    uint32_t count = s_filterCount;
    memcpy(filters, s_filters, sizeof(filters));
    for (uint32_t i = 0; i < count; i++)
    {
        if (filters[i].connection == connection)
        {
            (void)filters[i].filter(connection, message, filters[i].data);
        }
    }
    s_session.api.unrefMessage(message);
}

static DBusMessage* Signal(const char* path, const char* interface, const char* name)
{
    return s_dbus.signal(path, interface, name);
}

static void SettingChanged(void)
{
    Deliver(SESSION, Fill(Signal("/org/freedesktop/portal/desktop",
                                 "org.freedesktop.portal.Settings", "SettingChanged"),
                          "ssv", CONTENTS(s_settingTypes)));
}

static void PropertiesChanged(uint8_t op)
{
    const char* path =
        (op & 16) != 0 ? "/org/freedesktop/UPower" : "/org/freedesktop/portal/desktop";
    Deliver((op & 32) != 0 ? SYSTEM : SESSION,
            Fill(Signal(path, "org.freedesktop.DBus.Properties", "PropertiesChanged"), "sa{sv}as",
                 CONTENTS(s_propertyTypes)));
}

// The user's PropertiesChanged, the older Changed, or NameOwnerChanged
// from the bus, or now and then from another connection.
static void UserSignal(uint8_t op)
{
    if ((op & 160) == 160)
    {
        // Mostly a name and owners of its own, now and then the script's.
        static const char* const names[] = {"org.freedesktop.Accounts", "org.freedesktop.UPower"};
        static const char* const owners[] = {"", ":1.7", ":1.8", ""};
        DBusMessage* message =
            Signal("/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged");
        uint8_t shape = Byte();
        if (message == nullptr)
        {
            return;
        }
        (void)s_dbus.setSender(message, (op & 16) != 0 ? ":1.9" : "org.freedesktop.DBus");
        if ((shape & 1) != 0)
        {
            Deliver(SYSTEM, Fill(message, "sss", nullptr, 0));
            return;
        }
        const char* arguments[] = {names[(shape >> 1) & 1], owners[(shape >> 2) & 3],
                                   owners[(shape >> 4) & 3]};
        mwinDBusIter iter;
        s_session.api.iterInitAppend(message, &iter);
        for (size_t i = 0; i < 3; i++)
        {
            Basic(&iter, mwin_dbusTypeString, &arguments[i]);
        }
        Deliver(SYSTEM, Fill(message, "", nullptr, 0));
        return;
    }
    const char* path = Path();
    if ((op & 128) != 0)
    {
        Deliver(SYSTEM,
                Fill(Signal(path, "org.freedesktop.Accounts.User", "Changed"), "", nullptr, 0));
        return;
    }
    Deliver(SYSTEM, Fill(Signal(path, "org.freedesktop.DBus.Properties", "PropertiesChanged"),
                         "sa{sv}as", CONTENTS(s_languageTypes)));
}

static void Response(void)
{
    Deliver(SESSION, Fill(Signal(Path(), "org.freedesktop.portal.Request", "Response"), "ua{sv}",
                          CONTENTS(s_resultTypes)));
}

static void ImeSignal(uint8_t op)
{
    static const struct
    {
        const char* interface;
        const char* name;
        const char* types;
    } signals[] = {
        {"org.fcitx.Fcitx.InputContext1", "CommitString", "s"},
        {"org.fcitx.Fcitx.InputContext1", "UpdateFormattedPreedit", "a(si)i"},
        {"org.freedesktop.IBus.InputContext", "CommitText", "v"},
        {"org.freedesktop.IBus.InputContext", "UpdatePreeditText", "vub"},
        {"org.freedesktop.IBus.InputContext", "HidePreeditText", ""},
        {"org.freedesktop.IBus.InputContext", "UpdatePreeditText", "vub"},
        {"org.fcitx.Fcitx.InputContext1", "UpdateFormattedPreedit", "a(si)i"},
        {"org.freedesktop.portal.Settings", "SettingChanged", "ssv"},
    };
    uint32_t i = (op >> 4) & 7;
    Deliver(SESSION, Fill(Signal(Path(), signals[i].interface, signals[i].name), signals[i].types,
                          CONTENTS(s_ibusTypes)));
}

// The test backend hands a dialog request to the portal code, as the
// Linux backends do.
static void Submit(mwinContext* context, uint32_t slot, uint32_t index)
{
    context->backend = s_test;
    Expect(context->windows[slot].requests[index].kind == mwin_requestFileDialog);
    int outcome = mwinDialogsOpen(&s_dialogs, slot, index, "");
    if (outcome >= 0)
    {
        mwinComplete(context, slot, index, (mwinOutcome)outcome);
    }
}

// The test backend's clock, which the core reads when it completes a
// request the new one supersedes, before handing the new one over. The
// context itself is not const.
static uint64_t Now(const mwinContext* context)
{
    mwinContext* own = (mwinContext*)context;
    own->backend = s_test;
    uint64_t now = s_test->now(own);
    own->backend = &s_ops;
    return now;
}

static void Dialog(mwinContext* context, mwinWindowId window, uint8_t op)
{
    mwinFileDialogDef def = mwinDefaultFileDialogDef();
    def.kind = (mwinDialogKind)((op >> 4) & 3);
    def.title = "t";
    def.titleLength = 1;
    mwinRequestId id;
    // The test backend knows itself by its operations, so they are
    // swapped only until the core hands the request over; Submit swaps
    // them back.
    context->backend = &s_ops;
    mwinResult result = mwinRequestFileDialog(context, window, &def, &id);
    context->backend = s_test;
    if (result == mwin_success && s_made < TRACKED)
    {
        s_requests[s_made++] = (typeof(s_requests[0])){id, 0};
    }
}

static void Key(mwinContext* context, mwinWindowId window, uint8_t op)
{
    static const char* const texts[] = {"a", "\xC3\xA9", "", "\xF0\x9F\x98\x80"};
    if (s_keyCount == KEYS)
    {
        return;
    }
    mwinEvent key = {0};
    key.type = (op & 16) != 0 ? mwin_eventKeyUp : mwin_eventKeyDown;
    s_keys[s_keyCount] = (typeof(s_keys[0])){window, key.type};
    key.timeNs = ++s_keyCount;
    key.data.key.code = (mwinKeyCode)(1 + (op >> 5));
    const char* text = key.type == mwin_eventKeyDown ? texts[Byte() & 3] : "";
    uint32_t keycode = 8u + (op >> 5);
    uint32_t slot = window.index1 - 1;
    if (!mwinImeOffer(&s_ime, slot, 'a', keycode, 0, &key, text, (uint32_t)strlen(text),
                      mwinMonotonicNow()))
    {
        // The backend posts a key the input method does not hold.
        mwinPost(context, slot, &key);
    }
}

static void Pump(uint64_t nowNs)
{
    mwinBusPump(&s_session, 0);
    mwinSettingsPump(&s_settings, nowNs);
    if (s_system.connection != nullptr)
    {
        mwinBusPump(&s_system, 0);
    }
    mwinPowerPump(&s_power, nowNs);
    mwinLanguagesPump(&s_languages, nowNs);
    mwinDialogsPump(&s_dialogs, nowNs);
    mwinImePump(&s_ime, nowNs);
}

static bool IsUtf8(const char* text, size_t length)
{
    return length == 0 ||
           (text != nullptr && muniValidateUtf8(text, length).status == muni_success);
}

// Whether an offset falls on a character's start or the text's end.
static bool OnBoundary(const char* text, uint32_t length, uint32_t at)
{
    return at <= length && (at == length || ((unsigned char)text[at] & 0xC0u) != 0x80u);
}

static void CheckPreedit(const mwinPreeditEvent* preedit)
{
    const char* text = preedit->text;
    uint32_t length = preedit->length;
    Expect(IsUtf8(text, length));
    Expect(preedit->caret == -1 || OnBoundary(text, length, (uint32_t)preedit->caret));
    Expect(preedit->selectionStart <= preedit->selectionEnd &&
           OnBoundary(text, length, preedit->selectionStart) &&
           OnBoundary(text, length, preedit->selectionEnd));
    Expect(preedit->segmentCount <= MWIN_MAX_PREEDIT_SEGMENTS);
    for (uint32_t i = 0; i < preedit->segmentCount; i++)
    {
        const mwinPreeditSegment* segment = &preedit->segments[i];
        Expect(segment->start <= length && segment->length <= length - segment->start &&
               OnBoundary(text, length, segment->start) &&
               OnBoundary(text, length, segment->start + segment->length));
    }
}

static void CheckFacts(const mwinContext* context)
{
    mwinSystemFacts facts;
    Expect(mwinGetSystemFacts(context, &facts) == mwin_success);
    Expect(facts.theme <= mwin_themeDark && facts.onBattery <= mwin_yes &&
           facts.lowPower <= mwin_yes);
    // A NaN fails both.
    Expect(facts.textScale > 0.0f && facts.textScale <= FLT_MAX);
}

// A dialog's paths: its count of absolute UTF-8 paths, within the limits.
static void CheckFiles(const mwinContext* context, mwinRequestId request)
{
    static char bytes[DIALOG_BYTES + 1];
    size_t length = 0;
    uint32_t count = 0;
    Expect(mwinGetDialogFiles(context, request, bytes, sizeof(bytes), &length, &count) ==
               mwin_success &&
           length <= DIALOG_BYTES && count <= FILES);
    uint32_t paths = 0;
    for (size_t at = 0; at < length; paths++)
    {
        const char* end = memchr(bytes + at, '\0', length - at);
        Expect(end != nullptr);
        size_t path = (size_t)(end - (bytes + at));
        Expect(path > 0 && bytes[at] == '/' && IsUtf8(bytes + at, path));
        at += path + 1;
    }
    Expect(paths == count);
}

static bool IsTagByte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

// The preferred locales: never empty, tags between single commas.
static void CheckLocales(const mwinContext* context)
{
    char locales[MWIN_LINUX_LOCALE_BYTES];
    size_t length = 0;
    Expect(mwinGetPreferredLocales(context, locales, sizeof(locales), &length) == mwin_success &&
           length > 0 && locales[0] != ',' && locales[length - 1] != ',');
    for (size_t i = 0; i < length; i++)
    {
        Expect(IsTagByte(locales[i]) || (locales[i] == ',' && locales[i + 1] != ','));
    }
}

static void Drain(mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        switch (event.type)
        {
        case mwin_eventTextInput:
            Expect(IsUtf8(event.data.text.text, event.data.text.length));
            break;
        case mwin_eventImePreedit:
            CheckPreedit(&event.data.preedit);
            break;
        case mwin_eventThemeChanged:
        case mwin_eventPowerChanged:
            CheckFacts(context);
            break;
        case mwin_eventLocaleChanged:
            CheckLocales(context);
            break;
        case mwin_eventKeyDown:
        case mwin_eventKeyUp:
        {
            // A key may be taken or dropped, never moved, repeated or
            // given another window or type.
            uint64_t serial = event.timeNs;
            Expect(serial > s_keyRead && serial <= s_keyCount);
            Expect(memcmp(&s_keys[serial - 1].window, &event.window, sizeof(event.window)) == 0 &&
                   s_keys[serial - 1].type == event.type);
            s_keyRead = serial;
            break;
        }
        case mwin_eventRequestCompleted:
        {
            const mwinCompletion* completion = &event.data.completion;
            for (uint32_t i = 0; i < s_made; i++)
            {
                if (memcmp(&s_requests[i].id, &completion->request, sizeof(mwinRequestId)) == 0)
                {
                    Expect(s_requests[i].completions == 0);
                    s_requests[i].completions++;
                    if (completion->outcome == mwin_outcomeDone)
                    {
                        CheckFiles(context, completion->request);
                    }
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

// The system bus, joined by hand: the power module's own start finds no
// system bus here, so it is given the stand-in, listened on with the
// module's filter, and asked for OnBattery, as its start does.
static void JoinSystem(void)
{
    mwinDBusFilter* filter = nullptr;
    for (uint32_t i = 0; i < s_filterCount; i++)
    {
        filter = s_filters[i].data == &s_power ? s_filters[i].filter : filter;
    }
    Expect(filter != nullptr && s_system.connection == nullptr);
    Join(&s_system, SYSTEM);
    s_power.systemListening = AddFilter(SYSTEM, filter, &s_power, nullptr);
    DBusMessage* get =
        s_system.api.newMethodCall("org.freedesktop.UPower", "/org/freedesktop/UPower",
                                   "org.freedesktop.DBus.Properties", "Get");
    Expect(get != nullptr);
    (void)mwinBusSend(&s_system, get, &s_power.batteryCall, mwinMonotonicNow());
}

static void Start(mwinContext* context)
{
    s_callCount = 0;
    s_filterCount = 0;
    s_made = 0;
    s_keyCount = 0;
    s_keyRead = 0;
    s_session = (mwinLinuxBus){0};
    s_system = (mwinLinuxBus){.system = true};
    Join(&s_session, SESSION);
    uint64_t now = mwinMonotonicNow();
    mwinSettingsStart(&s_settings, context, &s_session, now);
    mwinPowerStart(&s_power, context, &s_session, &s_system, now);
    JoinSystem();
    // It finds the stand-in, and no configuration folder.
    mwinLanguagesStart(&s_languages, context, &s_system, "en-US", 5, now);
    mwinDialogsStart(&s_dialogs, context, &s_session);
    mwinImeStart(&s_ime, context, &s_session);
    s_test = context->backend;
    s_ops = *s_test;
    s_ops.submit = Submit;
    s_ops.now = Now;
}

static void Stop(mwinContext* context)
{
    mwinImeStop(&s_ime);
    mwinDialogsStop(&s_dialogs);
    mwinPowerStop(&s_power);
    mwinLanguagesStop(&s_languages);
    mwinBusClose(&s_system);
    mwinSettingsStop(&s_settings);
    Drain(context);
    Expect(s_filterCount == 0);
    for (uint32_t i = 0; i < s_callCount; i++)
    {
        Expect(s_calls[i].released);
    }
    mwinBusClose(&s_session);
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)user;
    Start(context);
    mwinWindowId windows[WINDOWS];
    mwinWindowDef def = mwinDefaultWindowDef();
    for (uint32_t i = 0; i < WINDOWS; i++)
    {
        Expect(mwinCreateWindow(context, &def, &windows[i], nullptr) == mwin_success);
    }
    // An op makes a few calls at most: the script ends cleanly before the
    // stand-in could run out, whatever the input's length.
    while (s_size > 0 && s_callCount + 16 <= CALLS)
    {
        uint8_t op = Byte();
        uint32_t i = (op >> 4) % WINDOWS;
        switch (op & 15)
        {
        case 0:
            SettingChanged();
            break;
        case 1:
            Answer(callReadAll);
            break;
        case 2:
            if ((op & 64) != 0)
            {
                UserSignal(op);
            }
            else
            {
                PropertiesChanged(op);
            }
            break;
        case 3:
            Answer((op & 48) == 16 ? callFindUser : (op & 48) == 32 ? callGetAll : callGet);
            break;
        case 4:
            Dialog(context, windows[i], op);
            break;
        case 5:
            Answer(callChoose);
            break;
        case 6:
            Response();
            break;
        case 7:
        {
            // The window with the focus that takes text, or none.
            int32_t slot = (op & 64) != 0 ? -1 : (int32_t)windows[i].index1 - 1;
            mwinImeFocus(&s_ime, slot, (mwinRect){(float)(op >> 4), 2.0f, 3.0f, 4.0f});
            break;
        }
        case 8:
            Key(context, windows[i], op);
            break;
        case 9:
            Answer(callCreate);
            break;
        case 10:
            Answer(callKey);
            break;
        case 11:
            ImeSignal(op);
            break;
        case 12:
            Pump(mwinMonotonicNow());
            break;
        case 13:
            // Every call past its deadline.
            Pump(UINT64_MAX);
            break;
        default:
            // A window goes while a dialog or keys may wait on it; the
            // backend's next pump takes the input method's focus off it.
            if (s_ime.focus == (int32_t)windows[i].index1 - 1)
            {
                mwinImeFocus(&s_ime, -1, (mwinRect){0});
            }
            Expect(mwinDestroyWindow(context, windows[i]) == mwin_success &&
                   mwinCreateWindow(context, &def, &windows[i], nullptr) == mwin_success);
            break;
        }
        Drain(context);
    }
    // The windows go: their requests are cancelled, and the pump closes
    // the dialogs that showed for them.
    for (uint32_t i = 0; i < WINDOWS; i++)
    {
        Expect(mwinDestroyWindow(context, windows[i]) == mwin_success);
    }
    Pump(mwinMonotonicNow());
    Drain(context);
    for (uint32_t r = 0; r < s_made; r++)
    {
        Expect(s_requests[r].completions == 1);
    }
    Stop(context);
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    (void)context;
    (void)user;
    return mwin_frameStop;
}

int LLVMFuzzerInitialize(int* argc, char*** argv);

int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    (void)argc;
    (void)argv;
    // No system bus to reach, and no zenity to fall back to: a dialog the
    // portal fails answers unsupported.
    (void)setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent/mwin-fuzz", 1);
    (void)setenv("PATH", "/nonexistent/mwin-fuzz", 1);
    (void)setenv("XDG_CONFIG_HOME", "/nonexistent/mwin-fuzz", 1);
    void* library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    Expect(library != nullptr);
    *(void**)&s_dbus.make = dlsym(library, "dbus_message_new");
    *(void**)&s_dbus.signal = dlsym(library, "dbus_message_new_signal");
    *(void**)&s_dbus.setErrorName = dlsym(library, "dbus_message_set_error_name");
    *(void**)&s_dbus.setReplySerial = dlsym(library, "dbus_message_set_reply_serial");
    *(void**)&s_dbus.setSender = dlsym(library, "dbus_message_set_sender");
    *(void**)&s_dbus.setSerial = dlsym(library, "dbus_message_set_serial");
    *(void**)&s_dbus.marshal = dlsym(library, "dbus_message_marshal");
    *(void**)&s_dbus.demarshal = dlsym(library, "dbus_message_demarshal");
    *(void**)&s_dbus.release = dlsym(library, "dbus_free");
    *(void**)&s_dbus.member = dlsym(library, "dbus_message_get_member");
    *(void**)&s_dbus.signature = dlsym(library, "dbus_message_get_signature");
    s_trace = getenv("MWIN_FUZZ_TRACE") != nullptr;
    Expect(s_dbus.make != nullptr && s_dbus.signal != nullptr && s_dbus.setErrorName != nullptr &&
           s_dbus.setReplySerial != nullptr && s_dbus.setSender != nullptr &&
           s_dbus.setSerial != nullptr && s_dbus.marshal != nullptr &&
           s_dbus.demarshal != nullptr && s_dbus.release != nullptr && s_dbus.member != nullptr &&
           s_dbus.signature != nullptr);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    s_data = data;
    s_size = size;
    // Fcitx first when XMODIFIERS names it, else IBus.
    if ((Byte() & 1) != 0)
    {
        (void)setenv("XMODIFIERS", "@im=fcitx", 1);
    }
    else
    {
        (void)unsetenv("XMODIFIERS");
    }
    mwinAppDef def = mwinDefaultAppDef();
    def.context.backend = mwin_backendTest;
    def.context.limits.windows = SLOTS;
    def.context.limits.dialogFiles = FILES;
    def.context.limits.dialogBytes = DIALOG_BYTES;
    def.init = Init;
    def.frame = Frame;
    Expect(mwinRun(&def) == mwin_success);
    return 0;
}
