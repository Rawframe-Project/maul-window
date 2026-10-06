// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Stand-ins for Fcitx 5 and IBus on the tests' session bus
// (linux_bus_fake.h), for the X11 input method test: each makes an input
// context, writes down the focus, the caret and the last key's code it
// was told, and answers keys as a Japanese input method would: "a"
// starts a composition of "あ", Return commits it, and every other key
// is left to the program. The compositions and commits go out as each
// framework's signals, formatted preedit for Fcitx and IBusText for
// IBus.

#ifndef MAUL_WINDOW_TEST_LINUX_IME_FAKE_H
#define MAUL_WINDOW_TEST_LINUX_IME_FAKE_H

#include "linux_bus_fake.h"

#define IME_FCITX         "org.freedesktop.portal.Fcitx"
#define IME_FCITX_METHOD  "org.fcitx.Fcitx.InputMethod1"
#define IME_FCITX_CONTEXT "org.fcitx.Fcitx.InputContext1"
#define IME_FCITX_PATH    "/org/freedesktop/portal/inputcontext/1"
#define IME_IBUS          "org.freedesktop.portal.IBus"
#define IME_IBUS_PORTAL   "org.freedesktop.IBus.Portal"
#define IME_IBUS_CONTEXT  "org.freedesktop.IBus.InputContext"
#define IME_IBUS_PATH     "/org/freedesktop/IBus/InputContext_1"
#define IME_KANA          "\xE3\x81\x82"

// What the input methods were told, by the framework that was asked.
typedef struct FakeIme
{
    // The framework that made a context: 0 none, 1 Fcitx, 2 IBus.
    int made;
    int focusIns;
    int focusOuts;
    int resets;
    int32_t caret[4];
    uint32_t keycode;
    // Key releases the method was told of.
    int releases;
    uint32_t capabilities;
    bool composing;
} FakeIme;

static FakeIme s_fakeIme;

// D-Bus's type codes the stand-ins append.
enum
{
    imeArray = 'a',
    imeBoolean = 'b',
    imeDictEntry = 'e',
    imeInt32 = 'i',
    imeObjectPath = 'o',
    imeStruct = 'r',
    imeString = 's',
    imeUint32 = 'u',
    imeUint64 = 't',
    imeVariant = 'v',
};

static inline void FakeImeEmit(FakeBus* fake, void* signal)
{
    (void)fake->send(fake->connection, signal, nullptr);
    fake->unref(signal);
}

// An empty a{sv} of IBus's serialized objects.
static inline void FakeImeNoAttachments(FakeBus* fake, void* iter)
{
    void* sub[16];
    (void)fake->openContainer(iter, imeArray, "{sv}", sub);
    (void)fake->closeContainer(iter, sub);
}

// An IBusText of a string, its first characters marked as being
// converted when marked is more than 0.
static inline void FakeImeIBusText(FakeBus* fake, void* iter, const char* text, uint32_t marked)
{
    void* variant[16];
    void* object[16];
    void* listVariant[16];
    void* list[16];
    void* attributes[16];
    void* attributeVariant[16];
    void* attribute[16];
    const char* name = "IBusText";
    const char* listName = "IBusAttrList";
    const char* attributeName = "IBusAttribute";
    (void)fake->openContainer(iter, imeVariant, "(sa{sv}sv)", variant);
    (void)fake->openContainer(variant, imeStruct, nullptr, object);
    (void)fake->appendBasic(object, imeString, &name);
    FakeImeNoAttachments(fake, object);
    (void)fake->appendBasic(object, imeString, &text);
    (void)fake->openContainer(object, imeVariant, "(sa{sv}av)", listVariant);
    (void)fake->openContainer(listVariant, imeStruct, nullptr, list);
    (void)fake->appendBasic(list, imeString, &listName);
    FakeImeNoAttachments(fake, list);
    (void)fake->openContainer(list, imeArray, "v", attributes);
    if (marked > 0)
    {
        uint32_t values[4] = {3, 0xFFFFFF, 0, marked};
        (void)fake->openContainer(attributes, imeVariant, "(sa{sv}uuuu)", attributeVariant);
        (void)fake->openContainer(attributeVariant, imeStruct, nullptr, attribute);
        (void)fake->appendBasic(attribute, imeString, &attributeName);
        FakeImeNoAttachments(fake, attribute);
        for (int i = 0; i < 4; i++)
        {
            (void)fake->appendBasic(attribute, imeUint32, &values[i]);
        }
        (void)fake->closeContainer(attributeVariant, attribute);
        (void)fake->closeContainer(attributes, attributeVariant);
    }
    (void)fake->closeContainer(list, attributes);
    (void)fake->closeContainer(listVariant, list);
    (void)fake->closeContainer(object, listVariant);
    (void)fake->closeContainer(variant, object);
    (void)fake->closeContainer(iter, variant);
}

// The composition "あ", or none, as the framework's signal.
static inline void FakeImePreedit(FakeBus* fake, bool fcitx, bool composing)
{
    void* arguments[16];
    void* parts[16];
    void* part[16];
    void* signal =
        fcitx ? fake->newSignal(IME_FCITX_PATH, IME_FCITX_CONTEXT, "UpdateFormattedPreedit")
              : fake->newSignal(IME_IBUS_PATH, IME_IBUS_CONTEXT, "UpdatePreeditText");
    fake->initAppend(signal, arguments);
    if (fcitx)
    {
        const char* text = IME_KANA;
        int32_t format = 8 | 16;
        int32_t caret = composing ? 3 : -1;
        (void)fake->openContainer(arguments, imeArray, "(si)", parts);
        if (composing)
        {
            (void)fake->openContainer(parts, imeStruct, nullptr, part);
            (void)fake->appendBasic(part, imeString, &text);
            (void)fake->appendBasic(part, imeInt32, &format);
            (void)fake->closeContainer(parts, part);
        }
        (void)fake->closeContainer(arguments, parts);
        (void)fake->appendBasic(arguments, imeInt32, &caret);
    }
    else
    {
        uint32_t caret = composing ? 1 : 0;
        unsigned visible = composing;
        FakeImeIBusText(fake, arguments, composing ? IME_KANA : "", composing ? 1 : 0);
        (void)fake->appendBasic(arguments, imeUint32, &caret);
        (void)fake->appendBasic(arguments, imeBoolean, &visible);
    }
    FakeImeEmit(fake, signal);
}

static inline void FakeImeCommit(FakeBus* fake, bool fcitx)
{
    void* arguments[16];
    const char* text = IME_KANA;
    void* signal = fcitx ? fake->newSignal(IME_FCITX_PATH, IME_FCITX_CONTEXT, "CommitString")
                         : fake->newSignal(IME_IBUS_PATH, IME_IBUS_CONTEXT, "CommitText");
    fake->initAppend(signal, arguments);
    if (fcitx)
    {
        (void)fake->appendBasic(arguments, imeString, &text);
    }
    else
    {
        FakeImeIBusText(fake, arguments, text, 0);
    }
    FakeImeEmit(fake, signal);
}

// ProcessKeyEvent: "a" composes, Return commits, the rest is left.
static inline void* FakeImeKey(FakeBus* fake, void* message, bool fcitx)
{
    void* iter[16];
    void* arguments[16];
    uint32_t keysym = 0;
    uint32_t state = 0;
    unsigned release = 0;
    (void)fake->iterInit(message, iter);
    fake->getBasic(iter, &keysym);
    (void)fake->next(iter);
    fake->getBasic(iter, &s_fakeIme.keycode);
    (void)fake->next(iter);
    fake->getBasic(iter, &state);
    if (fcitx)
    {
        (void)fake->next(iter);
        fake->getBasic(iter, &release);
    }
    else
    {
        release = (state & (1u << 30)) != 0;
    }
    s_fakeIme.releases += release != 0;
    unsigned taken = keysym == 'a' || (keysym == 0xFF0D && s_fakeIme.composing);
    if (taken && !release && keysym == 'a')
    {
        s_fakeIme.composing = true;
        FakeImePreedit(fake, fcitx, true);
    }
    else if (taken && !release)
    {
        s_fakeIme.composing = false;
        FakeImeCommit(fake, fcitx);
        FakeImePreedit(fake, fcitx, false);
    }
    void* reply = fake->newReturn(message);
    fake->initAppend(reply, arguments);
    (void)fake->appendBasic(arguments, imeBoolean, &taken);
    return reply;
}

static inline void* FakeImeCreate(FakeBus* fake, void* message, bool fcitx)
{
    void* arguments[16];
    void* bytes[16];
    const char* path = fcitx ? IME_FCITX_PATH : IME_IBUS_PATH;
    s_fakeIme.made = fcitx ? 1 : 2;
    void* reply = fake->newReturn(message);
    fake->initAppend(reply, arguments);
    (void)fake->appendBasic(arguments, imeObjectPath, &path);
    if (fcitx)
    {
        (void)fake->openContainer(arguments, imeArray, "y", bytes);
        (void)fake->closeContainer(arguments, bytes);
    }
    return reply;
}

static inline void* FakeImeAnswer(FakeBus* fake, void* message)
{
    void* iter[16];
    const char* member = fake->member(message);
    if (member == nullptr)
    {
        return nullptr;
    }
    bool fcitx = fake->isCall(message, IME_FCITX_CONTEXT, member);
    bool ibus = fake->isCall(message, IME_IBUS_CONTEXT, member);
    if (fake->isCall(message, IME_FCITX_METHOD, "CreateInputContext") ||
        fake->isCall(message, IME_IBUS_PORTAL, "CreateInputContext"))
    {
        return FakeImeCreate(fake, message,
                             fake->isCall(message, IME_FCITX_METHOD, "CreateInputContext"));
    }
    if (!fcitx && !ibus)
    {
        return nullptr;
    }
    if (strcmp(member, "ProcessKeyEvent") == 0)
    {
        return FakeImeKey(fake, message, fcitx);
    }
    s_fakeIme.focusIns += strcmp(member, "FocusIn") == 0;
    s_fakeIme.focusOuts += strcmp(member, "FocusOut") == 0;
    s_fakeIme.resets += strcmp(member, "Reset") == 0;
    if (strcmp(member, "SetCursorRect") == 0 || strcmp(member, "SetCursorLocation") == 0)
    {
        (void)fake->iterInit(message, iter);
        for (int i = 0; i < 4; i++)
        {
            fake->getBasic(iter, &s_fakeIme.caret[i]);
            (void)fake->next(iter);
        }
    }
    if (strcmp(member, "SetCapability") == 0 || strcmp(member, "SetCapabilities") == 0)
    {
        uint64_t wide = 0;
        (void)fake->iterInit(message, iter);
        if (fcitx)
        {
            fake->getBasic(iter, &wide);
            s_fakeIme.capabilities = (uint32_t)wide;
        }
        else
        {
            fake->getBasic(iter, &s_fakeIme.capabilities);
        }
    }
    return fake->newReturn(message);
}

// Takes both frameworks' names on the bus and answers for them.
static inline bool FakeImeStart(FakeBus* fake)
{
    fake->more = FakeImeAnswer;
    return fake->requestName(fake->connection, IME_FCITX, 4, nullptr) == 1 &&
           fake->requestName(fake->connection, IME_IBUS, 4, nullptr) == 1;
}

#endif // MAUL_WINDOW_TEST_LINUX_IME_FAKE_H
