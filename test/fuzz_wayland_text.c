// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the Wayland backend's reading of the input method's
// text-input-v3 events, which the compositor relays from another program
// (wayland_text.c). Each input runs on a context of the test backend with
// the Wayland text input on a stand-in for libwayland that calls its
// listener as the compositor's events would. The input is a script of
// enter (on one of the program's surfaces, another or none), leave,
// preedit and commit strings of any bytes and length with cursor offsets
// of any value, delete_surrounding_text of any lengths, done, a window's
// text input turned on and off, and windows destroyed. At each done the
// records it should give are worked out apart from the backend: a reset
// where a string was lost to the limit, the committed text, then the new
// composition, or its end where the window composes, each string as its
// UTF-8 repair, the core's reset in place of one whose text the
// window's storage has no room for. The program must read exactly those
// text records for each window, in that order, and a reset exactly where
// one is due (resets coalesce, so its place is the core's); committed
// text must be
// UTF-8 within the limit, a composition UTF-8 with its caret, selection
// and segments on character boundaries within it; no record may name a
// destroyed window; and a window's composition ends when it loses the
// focus or its text input is turned off.

#include "utf8.h"
#include "wayland_text.h"

#include "maul-unicode/encoding.h"
#include "maul-window/window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOWS 3
// The context's window slots: a destroyed window's slot is free again
// only once its end is drained, after its replacement is made.
#define SLOTS 8
// The text limit, small enough for strings to pass it.
#define LIMIT 64u
// The objects a run can make: more than an input of 256 bytes asks for.
#define PROXIES 1024
// The records a window should get from one op, at most.
#define EXPECTED 4

static const uint8_t* s_data;
static size_t s_size;
// MWIN_FUZZ_TRACE set prints each string and record expected, to read
// what a finding sent.
static bool s_trace;

typedef struct Proxy
{
    const void* listener;
    void* data;
    bool destroyed;
} Proxy;

static Proxy s_proxies[PROXIES];
static uint32_t s_proxyCount;

static mwinWaylandPlatform s_platform;
static mwinWaylandWindow s_windows[SLOTS];
static char s_surfaces[SLOTS];
static char s_foreign;
static char s_preeditBytes[LIMIT];
static char s_commitBytes[LIMIT];
static mwinWindowId s_ids[WINDOWS];

// A record a window should get: its type and, for text, its bytes.
typedef struct Expected
{
    mwinEventType type;
    uint32_t length;
    char text[LIMIT];
} Expected;

// What a window should read from one op: its text records in order, and
// whether a reset, which coalesces with any other waiting and so has no
// place of its own; the storage its text takes, from empty at each op.
static struct
{
    Expected records[EXPECTED];
    uint32_t count;
    uint32_t read;
    bool reset;
    bool resetRead;
    uint32_t used;
} s_expected[SLOTS];

// The input method's pending strings, as the backend should keep them:
// the UTF-8 repair, or lost past the limit; and the focus.
static struct
{
    char text[LIMIT];
    uint32_t length;
    bool lost;
} s_preedit, s_commit;
static int32_t s_focus;

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

static int32_t Int(void)
{
    uint32_t value = Byte();
    value |= (uint32_t)Byte() << 8;
    value |= (uint32_t)Byte() << 16;
    value |= (uint32_t)Byte() << 24;
    return (int32_t)value;
}

static Proxy* NewProxy(void)
{
    Expect(s_proxyCount < PROXIES);
    Proxy* proxy = &s_proxies[s_proxyCount++];
    *proxy = (Proxy){0};
    return proxy;
}

// The stand-in compositor's side of libwayland.
static struct wl_proxy* MarshalFlags(struct wl_proxy* object, uint32_t opcode,
                                     const struct wl_interface* interface, uint32_t, uint32_t flags,
                                     ...)
{
    (void)opcode;
    Proxy* proxy = (Proxy*)object;
    Expect(proxy != nullptr && !proxy->destroyed);
    if ((flags & WL_MARSHAL_FLAG_DESTROY) != 0)
    {
        proxy->destroyed = true;
    }
    return interface != nullptr ? (struct wl_proxy*)NewProxy() : nullptr;
}

static int AddListener(struct wl_proxy* object, void (**implementation)(void), void* data)
{
    Proxy* proxy = (Proxy*)object;
    Expect(proxy != nullptr && !proxy->destroyed);
    proxy->listener = (const void*)implementation;
    proxy->data = data;
    return 0;
}

static uint32_t GetVersion(struct wl_proxy*)
{
    return 1;
}

static void Destroy(struct wl_proxy* object)
{
    Proxy* proxy = (Proxy*)object;
    Expect(proxy != nullptr && !proxy->destroyed);
    proxy->destroyed = true;
}

static const struct zwp_text_input_v3_listener* Listener(void)
{
    const Proxy* proxy = (const Proxy*)s_platform.text.textInput;
    Expect(proxy != nullptr && proxy->listener != nullptr);
    return proxy->listener;
}

static struct zwp_text_input_v3* TextInput(void)
{
    return s_platform.text.textInput;
}

static const char* const s_units[] = {
    "a", "Z", " ", "\xC3\xA9", "\xE6\x97\xA5", "\xF0\x9F\x98\x80"};

// A string of the input method's: none now and then, else units of
// UTF-8, bytes that are not, or both, up to past the limit; never a NUL,
// which ends a protocol string.
static const char* NextString(char* out, size_t size)
{
    uint8_t shape = Byte();
    if ((shape & 15) == 0)
    {
        return nullptr;
    }
    size_t length = 0;
    size_t count = Byte() % (LIMIT + 16);
    for (size_t i = 0; i < count; i++)
    {
        uint8_t pick = Byte();
        if ((shape & 16) != 0 && (pick & 3) == 0)
        {
            char raw = (char)(pick | 0x80);
            if (length + 1 < size)
            {
                out[length++] = raw;
            }
            continue;
        }
        const char* unit = s_units[pick % (sizeof(s_units) / sizeof(s_units[0]))];
        size_t bytes = strlen(unit);
        if (length + bytes < size)
        {
            memcpy(out + length, unit, bytes);
            length += bytes;
        }
    }
    out[length] = '\0';
    return out;
}

// Keeps a string as the backend should: its UTF-8 repair, lost past the
// limit.
static void Model(const char* string, char* text, uint32_t* length, bool* lost)
{
    size_t raw = string != nullptr ? strlen(string) : 0;
    if (s_trace)
    {
        (void)fprintf(stderr, "string of %zu bytes, UTF-8 %d\n", raw,
                      muniValidateUtf8(string, raw).status == muni_success);
    }
    size_t repaired = mwinRepairUtf8(string, raw, nullptr);
    *lost = repaired > LIMIT;
    *length = *lost ? 0 : (uint32_t)repaired;
    if (*length > 0)
    {
        (void)mwinRepairUtf8(string, raw, text);
    }
}

// A record the window should get. One whose text the window's storage
// has no room for, as the core reserves it (aligned for the segments a
// composition stores before its text), becomes a reset instead.
static void Push(uint32_t slot, mwinEventType type, const char* text, uint32_t length)
{
    if (s_trace)
    {
        (void)fprintf(stderr, "expect slot %u type %d length %u\n", slot, (int)type, length);
    }
    uint32_t need = length > 0 && type == mwin_eventImePreedit
                        ? length + (uint32_t)sizeof(mwinPreeditSegment)
                        : length;
    uint32_t align = (uint32_t)alignof(mwinPreeditSegment);
    uint32_t start = (s_expected[slot].used + align - 1) & ~(align - 1);
    if (type == mwin_eventInputStateReset || (need > 0 && start + need > LIMIT))
    {
        s_expected[slot].reset = true;
        return;
    }
    s_expected[slot].used = need > 0 ? start + need : s_expected[slot].used;
    Expect(s_expected[slot].count < EXPECTED);
    Expected* record = &s_expected[slot].records[s_expected[slot].count++];
    *record = (Expected){.type = type, .length = length};
    if (length > 0)
    {
        memcpy(record->text, text, length);
    }
}

static bool Composing(const mwinContext* context, uint32_t slot)
{
    return context->windows[slot].imeComposing;
}

// The input method's composition ends, which the program reads even
// after a reset ended the one it shows.
static void ExpectEnd(const mwinContext* context, uint32_t slot)
{
    if (Composing(context, slot))
    {
        Push(slot, mwin_eventImePreedit, nullptr, 0);
    }
}

static void Done(const mwinContext* context)
{
    if (s_focus >= 0)
    {
        uint32_t slot = (uint32_t)s_focus;
        if (s_commit.lost || s_preedit.lost)
        {
            Push(slot, mwin_eventInputStateReset, nullptr, 0);
        }
        if (s_commit.length > 0)
        {
            Push(slot, mwin_eventTextInput, s_commit.text, s_commit.length);
        }
        if (s_preedit.length > 0)
        {
            Push(slot, mwin_eventImePreedit, s_preedit.text, s_preedit.length);
        }
        else
        {
            ExpectEnd(context, slot);
        }
    }
    s_preedit.length = s_commit.length = 0;
    s_preedit.lost = s_commit.lost = false;
    Listener()->done(&s_platform, TextInput(), Byte());
}

static bool OnBoundary(const char* text, uint32_t length, uint32_t at)
{
    return at <= length && (at == length || ((unsigned char)text[at] & 0xC0) != 0x80);
}

static void CheckComposition(const mwinPreeditEvent* preedit)
{
    Expect(muniValidateUtf8(preedit->text, preedit->length).status == muni_success);
    Expect(preedit->caret == -1 ||
           (preedit->caret >= 0 &&
            OnBoundary(preedit->text, preedit->length, (uint32_t)preedit->caret)));
    Expect(preedit->selectionStart <= preedit->selectionEnd &&
           OnBoundary(preedit->text, preedit->length, preedit->selectionStart) &&
           OnBoundary(preedit->text, preedit->length, preedit->selectionEnd));
    for (uint32_t i = 0; i < preedit->segmentCount; i++)
    {
        const mwinPreeditSegment* segment = &preedit->segments[i];
        Expect(segment->start <= preedit->length &&
               segment->length <= preedit->length - segment->start &&
               OnBoundary(preedit->text, preedit->length, segment->start) &&
               OnBoundary(preedit->text, preedit->length, segment->start + segment->length));
    }
}

// Takes a record the window read against what it should get: a reset
// where one is due, else the next text record.
static void Match(uint32_t slot, const mwinEvent* event)
{
    if (event->type == mwin_eventInputStateReset)
    {
        Expect(s_expected[slot].reset && !s_expected[slot].resetRead);
        s_expected[slot].resetRead = true;
        return;
    }
    Expect(s_expected[slot].read < s_expected[slot].count);
    const Expected* expected = &s_expected[slot].records[s_expected[slot].read++];
    Expect(event->type == expected->type && event->data.text.length == expected->length &&
           (expected->length == 0 ||
            memcmp(event->data.text.text, expected->text, expected->length) == 0));
}

// Reads the records, each window's text records against what it should
// get, in order.
static void Drain(mwinContext* context)
{
    mwinEvent event;
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        bool text = event.type == mwin_eventTextInput || event.type == mwin_eventImePreedit ||
                    event.type == mwin_eventInputStateReset;
        if (!text)
        {
            continue;
        }
        uint32_t slot = event.window.index1 - 1;
        if (s_trace)
        {
            (void)fprintf(stderr, "read slot %u type %d length %u\n", slot, (int)event.type,
                          event.data.text.length);
        }
        bool live = false;
        for (uint32_t i = 0; i < WINDOWS; i++)
        {
            live = live || memcmp(&s_ids[i], &event.window, sizeof(event.window)) == 0;
        }
        Expect(live);
        if (event.type == mwin_eventTextInput)
        {
            Expect(event.data.text.length <= LIMIT &&
                   muniValidateUtf8(event.data.text.text, event.data.text.length).status ==
                       muni_success);
        }
        if (event.type == mwin_eventImePreedit && event.data.preedit.length > 0)
        {
            CheckComposition(&event.data.preedit);
        }
        Match(slot, &event);
    }
    for (uint32_t slot = 0; slot < SLOTS; slot++)
    {
        Expect(s_expected[slot].read == s_expected[slot].count &&
               s_expected[slot].resetRead == s_expected[slot].reset);
        s_expected[slot].read = s_expected[slot].count = s_expected[slot].used = 0;
        s_expected[slot].reset = s_expected[slot].resetRead = false;
    }
    // The next pump frees the text read, as each backend pump begins.
    mwinBeginPump(context);
}

static void Start(mwinContext* context)
{
    s_proxyCount = 0;
    s_focus = -1;
    s_preedit.length = s_commit.length = 0;
    s_preedit.lost = s_commit.lost = false;
    memset(s_windows, 0, sizeof(s_windows));
    memset(s_expected, 0, sizeof(s_expected));
    s_platform = (mwinWaylandPlatform){.context = context, .windows = s_windows};
    s_platform.api.proxyMarshalFlags = MarshalFlags;
    s_platform.api.proxyAddListener = AddListener;
    s_platform.api.proxyGetVersion = GetVersion;
    s_platform.api.proxyDestroy = Destroy;
    s_platform.seat = (void*)NewProxy();
    s_platform.textInputs = (void*)NewProxy();
    s_platform.text.preedit.bytes = s_preeditBytes;
    s_platform.text.commit.bytes = s_commitBytes;
    mwinWaylandAttachText(&s_platform);
    (void)Listener();
}

static void Enter(uint8_t op, uint32_t i)
{
    struct wl_surface* surface = (op & 64) != 0   ? nullptr
                                 : (op & 32) != 0 ? (struct wl_surface*)&s_foreign
                                                  : s_windows[s_ids[i].index1 - 1].surface;
    s_focus = (op & 96) == 0 ? (int32_t)s_ids[i].index1 - 1 : -1;
    Listener()->enter(&s_platform, TextInput(), surface);
}

static void Leave(const mwinContext* context)
{
    if (s_focus >= 0)
    {
        ExpectEnd(context, (uint32_t)s_focus);
    }
    s_focus = -1;
    Listener()->leave(&s_platform, TextInput(), nullptr);
}

static void Preedit(void)
{
    static char string[LIMIT * 4 + 1];
    const char* text = NextString(string, sizeof(string));
    int32_t begin = (Byte() & 1) != 0 ? Int() : (int32_t)(Byte() % 16) - 1;
    int32_t end = (Byte() & 1) != 0 ? Int() : (int32_t)(Byte() % 16) - 1;
    Model(text, s_preedit.text, &s_preedit.length, &s_preedit.lost);
    Listener()->preedit_string(&s_platform, TextInput(), text, begin, end);
}

static void Commit(void)
{
    static char string[LIMIT * 4 + 1];
    const char* text = NextString(string, sizeof(string));
    Model(text, s_commit.text, &s_commit.length, &s_commit.lost);
    Listener()->commit_string(&s_platform, TextInput(), text);
}

static void SetTextInput(mwinContext* context, uint8_t op, uint32_t i)
{
    uint32_t slot = s_ids[i].index1 - 1;
    bool enabled = (op & 64) != 0;
    if (!enabled)
    {
        ExpectEnd(context, slot);
    }
    Expect(mwinWaylandSetTextInput(&s_platform, slot, enabled,
                                   (mwinRect){(float)(op >> 4), 1.0f, 2.0f, 3.0f}) ==
           mwin_outcomeDone);
}

// A window goes, as the backend's destroy has it, and another comes.
static void Replace(mwinContext* context, uint32_t i)
{
    uint32_t slot = s_ids[i].index1 - 1;
    mwinWaylandForgetTextFocus(&s_platform, slot);
    s_windows[slot].surface = nullptr;
    s_windows[slot].textInput = false;
    s_focus = s_focus == (int32_t)slot ? -1 : s_focus;
    Expect(mwinDestroyWindow(context, s_ids[i]) == mwin_success);
    s_expected[slot] = (typeof(s_expected[slot])){0};
    mwinWindowDef def = mwinDefaultWindowDef();
    Expect(mwinCreateWindow(context, &def, &s_ids[i], nullptr) == mwin_success);
    s_windows[s_ids[i].index1 - 1].surface = (void*)&s_surfaces[s_ids[i].index1 - 1];
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)user;
    Start(context);
    mwinWindowDef def = mwinDefaultWindowDef();
    for (uint32_t i = 0; i < WINDOWS; i++)
    {
        Expect(mwinCreateWindow(context, &def, &s_ids[i], nullptr) == mwin_success);
        s_windows[s_ids[i].index1 - 1].surface = (void*)&s_surfaces[s_ids[i].index1 - 1];
    }
    Drain(context);
    while (s_size > 0 && s_proxyCount + 16 <= PROXIES)
    {
        uint8_t op = Byte();
        uint32_t i = (op >> 4) % WINDOWS;
        switch (op & 15)
        {
        case 0:
            Enter(op, i);
            break;
        case 1:
            Leave(context);
            break;
        case 2:
        case 3:
            Preedit();
            break;
        case 4:
            Commit();
            break;
        case 5:
            Listener()->delete_surrounding_text(&s_platform, TextInput(), (uint32_t)Int(),
                                                (uint32_t)Int());
            break;
        case 6:
        case 7:
        case 8:
            Done(context);
            break;
        case 9:
        case 10:
            SetTextInput(context, op, i);
            break;
        case 11:
            Replace(context, i);
            break;
        default:
            // A string or two and done, as an input method sends them.
            if ((op & 16) != 0)
            {
                Commit();
            }
            Preedit();
            Done(context);
            break;
        }
        Drain(context);
    }
    if (s_focus >= 0)
    {
        Leave(context);
    }
    Drain(context);
    mwinWaylandDetachText(&s_platform);
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
    s_trace = getenv("MWIN_FUZZ_TRACE") != nullptr;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    s_data = data;
    s_size = size;
    mwinAppDef def = mwinDefaultAppDef();
    def.context.backend = mwin_backendTest;
    def.context.limits.windows = SLOTS;
    def.context.limits.textBytesPerWindow = LIMIT;
    def.init = Init;
    def.frame = Frame;
    Expect(mwinRun(&def) == mwin_success);
    return 0;
}
