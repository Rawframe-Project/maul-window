// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the Wayland backend's handling of data offers as other clients
// make them: the type lists of the clipboard's, the primary selection's
// and a drag's offers, and the bytes they send through the pipes the
// backend hands them, in pieces, cut short or past the limits. Each
// input runs on a context of the test backend whose clipboard and primary
// requests go through the Wayland code (wayland_clipboard*.c,
// wayland_primary.c, wayland_drop.c, wayland_pipe.c) against a stand-in
// compositor that keeps to the protocol: an offer comes with its types and
// then a selection or a drag's enter. The input is a script of offers,
// drags and drops, requests, pieces written and pipes closed, pumps and
// deadlines, and windows destroyed. Every request must complete exactly
// once; what a read found must be within the clipboard limit, its text
// UTF-8; a drop's paths must be non-empty UTF-8 and its text UTF-8 within
// their limits; a drop whose pipe was cut off or ran past the limit must
// say it was truncated; no request may name an object the backend
// destroyed, every offer must be destroyed by the end, and every pipe
// closed.

#include "wayland_clipboard.h"
#include "wayland_drop.h"

#include "maul-unicode/encoding.h"
#include "maul-window/clipboard.h"
#include "maul-window/drop.h"
#include "maul-window/window.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WINDOWS 3
// The context's window slots: a destroyed window's slot is free again
// only once its end is drained, after its replacement is made.
#define SLOTS 8
#define LIMIT 1024u
#define FILES 8u
// The objects a run can make: more than an input of 256 bytes asks for.
#define PROXIES 1024
#define WRITERS 8
#define TRACKED 128

static const uint8_t* s_data;
static size_t s_size;

// An object of the stand-in compositor: what it is, its listener, and
// whether the backend destroyed it.
typedef enum ProxyKind
{
    proxyOther,
    proxyDataOffer,
    proxyPrimaryOffer,
} ProxyKind;

typedef struct Proxy
{
    ProxyKind kind;
    const void* listener;
    void* data;
    bool destroyed;
} Proxy;

static Proxy s_proxies[PROXIES];
static uint32_t s_proxyCount;

// The writing ends the backend handed over, a copy each, by what they
// carry.
typedef enum Role
{
    roleNone,
    roleRead,
    roleDropFiles,
    roleDropText,
} Role;

typedef struct Writer
{
    int fd;
    Role role;
    uint32_t written;
    bool closed;
} Writer;

static Writer s_writers[WRITERS];
// The offer of the drop the drop writers are of.
static const Proxy* s_dropOffer;

static mwinWaylandPlatform s_platform;
static mwinWaylandWindow s_windows[SLOTS];
static char s_surfaces[SLOTS];
// The script's windows, by id.
static mwinWindowId s_ids[WINDOWS];
static Proxy* s_device;
static Proxy* s_primaryDevice;
static bool s_dragging;
static mwinBackendOps s_ops;
static const mwinBackendOps* s_test;

static struct
{
    mwinRequestId id;
    mwinRequestKind kind;
    uint32_t completions;
} s_requests[TRACKED];
static uint32_t s_made;

// Stops the run where a condition does not hold, saying which.
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

static Proxy* NewProxy(ProxyKind kind)
{
    Expect(s_proxyCount < PROXIES);
    Proxy* proxy = &s_proxies[s_proxyCount++];
    *proxy = (Proxy){.kind = kind};
    return proxy;
}

static void CloseWriter(Writer* writer)
{
    if (writer->fd >= 0)
    {
        (void)close(writer->fd);
    }
    *writer = (Writer){.fd = -1};
}

// Keeps a copy of a writing end the backend sends, nonblocking, so a full
// pipe never stops the run.
static void KeepWriter(int fd, Role role)
{
    Writer* free = nullptr;
    for (int i = 0; i < WRITERS && free == nullptr; i++)
    {
        free = s_writers[i].role == roleNone ? &s_writers[i] : nullptr;
    }
    if (free == nullptr)
    {
        // The oldest read's goes; a drop's is kept for its check.
        for (int i = 0; i < WRITERS && free == nullptr; i++)
        {
            free = s_writers[i].role == roleRead ? &s_writers[i] : nullptr;
        }
        Expect(free != nullptr);
        CloseWriter(free);
    }
    int copy = dup(fd);
    Expect(copy >= 0 && fcntl(copy, F_SETFL, O_NONBLOCK) == 0);
    *free = (Writer){.fd = copy, .role = role};
}

// The stand-in compositor's side of libwayland.
static struct wl_proxy* MarshalFlags(struct wl_proxy* object, uint32_t opcode,
                                     const struct wl_interface* interface, uint32_t, uint32_t flags,
                                     ...)
{
    Proxy* proxy = (Proxy*)object;
    // A request on an object the backend destroyed is a use after free.
    Expect(proxy != nullptr && !proxy->destroyed);
    bool receive =
        (proxy->kind == proxyDataOffer && opcode == WL_DATA_OFFER_RECEIVE) ||
        (proxy->kind == proxyPrimaryOffer && opcode == ZWP_PRIMARY_SELECTION_OFFER_V1_RECEIVE);
    if (receive)
    {
        va_list arguments;
        va_start(arguments, flags);
        const char* type = va_arg(arguments, const char*);
        int fd = va_arg(arguments, int);
        va_end(arguments);
        bool drop = (void*)proxy == (void*)s_platform.drag.dropped;
        if (drop && proxy != s_dropOffer)
        {
            // A new drop: what an earlier one kept is not its. One
            // delivered to a window since destroyed came with no record.
            for (int i = 0; i < WRITERS; i++)
            {
                if (s_writers[i].role == roleDropFiles || s_writers[i].role == roleDropText)
                {
                    CloseWriter(&s_writers[i]);
                }
            }
            s_dropOffer = proxy;
        }
        if (drop)
        {
            bool files = strcmp(type, "text/uri-list") == 0;
            KeepWriter(fd, files ? roleDropFiles : roleDropText);
        }
        else
        {
            KeepWriter(fd, roleRead);
        }
    }
    if ((flags & WL_MARSHAL_FLAG_DESTROY) != 0)
    {
        proxy->destroyed = true;
    }
    return interface != nullptr ? (struct wl_proxy*)NewProxy(proxyOther) : nullptr;
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
    return 3;
}

static void Destroy(struct wl_proxy* object)
{
    Proxy* proxy = (Proxy*)object;
    Expect(proxy != nullptr && !proxy->destroyed);
    proxy->destroyed = true;
}

// The clipboard's requests go to the Wayland code, as the backend's
// submit sends them.
static void Submit(mwinContext* context, uint32_t slot, uint32_t index)
{
    context->backend = s_test;
    const mwinRequest* request = &context->windows[slot].requests[index];
    bool write = request->kind == mwin_requestClipboardWrite ||
                 request->kind == mwin_requestClipboardWriteData ||
                 request->kind == mwin_requestPrimaryWrite;
    int outcome = write ? mwinWaylandWriteSelection(&s_platform, request->kind)
                        : mwinWaylandReadSelection(&s_platform, request);
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

// Keeps a request made through the Wayland code. The test backend knows
// itself by its operations, so they are swapped only until the core
// hands the request over, within the call; Submit swaps them back. On
// the way the core calls no operation but now and submit.
static void Track(mwinContext* context, mwinResult result, mwinRequestId id, mwinRequestKind kind)
{
    context->backend = s_test;
    if (result == mwin_success && s_made < TRACKED)
    {
        s_requests[s_made++] = (typeof(s_requests[0])){id, kind, 0};
    }
}

static bool IsUtf8(const char* text, size_t length)
{
    return muniValidateUtf8(text, length).status == muni_success;
}

// What a done read left must be within the limit, its text UTF-8; a
// later read may have replaced it.
static void CheckFound(const mwinContext* context, const mwinCompletion* completion)
{
    static char found[LIMIT];
    size_t length = 0;
    mwinResult result = mwin_errorStale;
    switch (completion->kind)
    {
    case mwin_requestClipboardRead:
        result = mwinGetClipboardText(context, completion->request, found, LIMIT, &length);
        break;
    case mwin_requestPrimaryRead:
        result = mwinGetPrimaryText(context, completion->request, found, LIMIT, &length);
        break;
    case mwin_requestClipboardReadData:
        result = mwinGetClipboardData(context, completion->request, found, LIMIT, &length);
        break;
    default:
        return;
    }
    Expect(result == mwin_success || result == mwin_errorStale);
    Expect(result != mwin_success ||
           (length <= LIMIT &&
            (completion->kind == mwin_requestClipboardReadData || IsUtf8(found, length))));
}

// A drop's paths are its count of non-empty UTF-8 paths and its text is
// UTF-8, each within the limit; one whose pipe was still open or ran past
// the limit when it came says it was truncated.
static void CheckDrop(const mwinContext* context, const mwinDropEvent* drop)
{
    static char bytes[LIMIT + 1];
    size_t length = 0;
    Expect(mwinGetDroppedFiles(context, drop->drop, bytes, sizeof(bytes), &length) ==
               mwin_success &&
           length <= LIMIT);
    uint32_t count = 0;
    for (size_t at = 0; at < length; count++)
    {
        const char* end = memchr(bytes + at, '\0', length - at);
        Expect(end != nullptr);
        size_t path = (size_t)(end - (bytes + at));
        Expect(path > 0 && IsUtf8(bytes + at, path));
        at += path + 1;
    }
    Expect(count == drop->fileCount && count <= FILES);
    Expect(mwinGetDroppedText(context, drop->drop, bytes, sizeof(bytes), &length) == mwin_success &&
           length == drop->textLength && length <= LIMIT && IsUtf8(bytes, length));
    for (int i = 0; i < WRITERS; i++)
    {
        Writer* writer = &s_writers[i];
        if (writer->role == roleDropFiles || writer->role == roleDropText)
        {
            Expect(drop->truncated || (writer->closed && writer->written <= LIMIT));
            CloseWriter(writer);
        }
    }
}

static void Drain(mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventDropped)
        {
            CheckDrop(context, &event.data.drop);
        }
        if (event.type != mwin_eventRequestCompleted)
        {
            continue;
        }
        const mwinCompletion* completion = &event.data.completion;
        for (uint32_t i = 0; i < s_made; i++)
        {
            if (memcmp(&s_requests[i].id, &completion->request, sizeof(mwinRequestId)) == 0)
            {
                Expect(s_requests[i].kind == completion->kind && s_requests[i].completions == 0);
                s_requests[i].completions++;
                if (completion->outcome == mwin_outcomeDone)
                {
                    CheckFound(context, completion);
                }
            }
        }
    }
}

// A type of an offer, from the input: one the backend knows or a data
// type; bytes of the input's, which may be long or not ASCII; or letters
// to a length, so a few bytes make a long list.
static void NextType(char* type, size_t size)
{
    static const char* const known[] = {
        "text/plain;charset=utf-8",
        "UTF8_STRING",
        "text/plain",
        "text/uri-list",
        "image/png",
        "text/html",
        "application/x-maul",
    };
    uint8_t shape = Byte();
    if (shape % 16 < 7)
    {
        (void)snprintf(type, size, "%s", known[shape % 16]);
        return;
    }
    if (shape % 16 > 7)
    {
        size_t letters = Byte() % 80;
        for (size_t i = 0; i < letters; i++)
        {
            type[i] = (char)('a' + (i + shape) % 26);
        }
        type[letters] = '\0';
        return;
    }
    size_t length = Byte() % (size - 1);
    for (size_t i = 0; i < length; i++)
    {
        uint8_t byte = Byte();
        type[i] = (char)(byte == 0 ? 'x' : byte);
    }
    type[length] = '\0';
}

// The types a clipboard offer keeps, for a data read to ask for: in the
// order announced, each of at most MWIN_CLIPBOARD_MIME bytes that still
// fits whole, with its NUL, in what the store has left. None is cut and
// nothing past the store is kept.
static void CheckKept(const mwinWaylandTypes* kept, char (*announced)[128], uint8_t count)
{
    static char expected[MWIN_WAYLAND_TYPE_BYTES];
    size_t used = 0;
    for (uint8_t i = 0; i < count; i++)
    {
        size_t length = strlen(announced[i]);
        if (length <= MWIN_CLIPBOARD_MIME && length + 1 <= sizeof(expected) - used)
        {
            memcpy(expected + used, announced[i], length + 1);
            used += length + 1;
        }
    }
    Expect(kept->length == used && memcmp(kept->bytes, expected, used) == 0);
}

// Makes an offer and announces its types through the device that made
// it: NULL where the run has no room for it.
static Proxy* Offer(bool primary)
{
    if (s_proxyCount + 8 > PROXIES)
    {
        return nullptr;
    }
    Proxy* offer = NewProxy(primary ? proxyPrimaryOffer : proxyDataOffer);
    uint8_t types = Byte() % 48;
    static char announced[48][128];
    char* type = nullptr;
    if (primary)
    {
        const struct zwp_primary_selection_device_v1_listener* device = s_primaryDevice->listener;
        device->data_offer(s_primaryDevice->data, (void*)s_primaryDevice, (void*)offer);
        for (uint8_t i = 0; i < types; i++)
        {
            type = announced[i];
            NextType(type, sizeof(announced[i]));
            const struct zwp_primary_selection_offer_v1_listener* listener = offer->listener;
            listener->offer(offer->data, (void*)offer, type);
        }
        return offer;
    }
    const struct wl_data_device_listener* device = s_device->listener;
    device->data_offer(s_device->data, (void*)s_device, (void*)offer);
    for (uint8_t i = 0; i < types; i++)
    {
        type = announced[i];
        NextType(type, sizeof(announced[i]));
        const struct wl_data_offer_listener* listener = offer->listener;
        listener->offer(offer->data, (void*)offer, type);
    }
    CheckKept(&s_platform.clipboard.incomingTypes, announced, types);
    return offer;
}

// The drag's events: an enter over a window's surface, another client's
// or none; motion; leave; and a drop, which may be followed by a leave.
static void Drag(uint8_t op)
{
    const struct wl_data_device_listener* device = s_device->listener;
    void* data = s_device->data;
    wl_fixed_t x = wl_fixed_from_int((int)(Byte() % 200));
    wl_fixed_t y = wl_fixed_from_int((int)(Byte() % 200));
    if (!s_dragging)
    {
        Proxy* offer = Offer(false);
        if (offer == nullptr)
        {
            return;
        }
        uint32_t surface = (op >> 4) % (WINDOWS + 1);
        void* over =
            surface < WINDOWS ? (void*)s_windows[s_ids[surface].index1 - 1].surface : nullptr;
        device->enter(data, (void*)s_device, 1, over, x, y, (void*)offer);
        s_dragging = true;
        return;
    }
    switch ((op >> 4) % 4)
    {
    case 0:
    case 1:
        device->motion(data, (void*)s_device, 0, x, y);
        break;
    case 2:
        device->leave(data, (void*)s_device);
        s_dragging = false;
        break;
    default:
        device->drop(data, (void*)s_device);
        if ((op & 0x80) != 0)
        {
            device->leave(data, (void*)s_device);
        }
        s_dragging = false;
        break;
    }
}

static void Selection(uint8_t op)
{
    bool primary = (op & 0x10) != 0;
    Proxy* offer = (op & 0x20) != 0 ? nullptr : Offer(primary);
    if (primary)
    {
        const struct zwp_primary_selection_device_v1_listener* device = s_primaryDevice->listener;
        device->selection(s_primaryDevice->data, (void*)s_primaryDevice, (void*)offer);
    }
    else
    {
        const struct wl_data_device_listener* device = s_device->listener;
        device->selection(s_device->data, (void*)s_device, (void*)offer);
    }
}

// A request of a window, from the input: the kind and the data's type.
static void Request(mwinContext* context, mwinWindowId window)
{
    static const char* const mimes[] = {"image/png", "application/x-maul", "text/html"};
    static const char text[] = "caf\xc3\xa9";
    uint8_t shape = Byte();
    const char* mime = mimes[((shape >> 3) & 3) % 3];
    mwinRequestId id = {0};
    context->backend = &s_ops;
    switch ((shape & 7) % 6)
    {
    case 0:
        Track(context, mwinRequestClipboardRead(context, window, &id), id,
              mwin_requestClipboardRead);
        break;
    case 1:
        Track(context, mwinRequestPrimaryRead(context, window, &id), id, mwin_requestPrimaryRead);
        break;
    case 2:
        Track(context, mwinRequestClipboardReadData(context, window, mime, strlen(mime), &id), id,
              mwin_requestClipboardReadData);
        break;
    case 3:
        Track(context, mwinRequestClipboardWrite(context, window, text, sizeof(text) - 1, &id), id,
              mwin_requestClipboardWrite);
        break;
    case 4:
    {
        mwinClipboardItem item = {mime, strlen(mime), text, sizeof(text) - 1};
        Track(context, mwinRequestClipboardWriteData(context, window, &item, 1, &id), id,
              mwin_requestClipboardWriteData);
        break;
    }
    default:
        Track(context, mwinRequestPrimaryWrite(context, window, text, sizeof(text) - 1, &id), id,
              mwin_requestPrimaryWrite);
        break;
    }
}

// Writes into a pipe the backend handed over: bytes of the input as they
// come, so a uri-list can be spelled, or a pattern repeated to a length
// up to twice the limit; or closes it, the end of what it sends.
static void Write(uint8_t op, bool end)
{
    Writer* writer = &s_writers[(op >> 4) % WRITERS];
    if (writer->role == roleNone || writer->closed)
    {
        return;
    }
    if (end)
    {
        (void)close(writer->fd);
        writer->fd = -1;
        writer->closed = true;
        return;
    }
    static char bytes[2 * LIMIT + 8];
    uint32_t length = 0;
    if ((op & 0x80) != 0)
    {
        length = Byte() % 64;
        for (uint32_t i = 0; i < length; i++)
        {
            bytes[i] = (char)Byte();
        }
    }
    else
    {
        uint8_t pattern[4] = {Byte(), Byte(), Byte(), Byte()};
        length = (Byte() | (uint32_t)Byte() << 8) % (uint32_t)sizeof(bytes);
        for (uint32_t i = 0; i < length; i++)
        {
            bytes[i] = (char)pattern[i % 4];
        }
    }
    ssize_t written = length > 0 ? write(writer->fd, bytes, length) : 0;
    writer->written += written > 0 ? (uint32_t)written : 0;
}

static void Pump(uint64_t nowNs)
{
    mwinWaylandPumpClipboard(&s_platform, nowNs);
    mwinWaylandPumpDrag(&s_platform, nowNs);
}

static void Start(mwinContext* context)
{
    s_proxyCount = 0;
    s_made = 0;
    s_dragging = false;
    s_dropOffer = nullptr;
    memset(s_windows, 0, sizeof(s_windows));
    memset(s_ids, 0, sizeof(s_ids));
    for (int i = 0; i < WRITERS; i++)
    {
        s_writers[i] = (Writer){.fd = -1};
    }
    s_platform = (mwinWaylandPlatform){.context = context, .windows = s_windows};
    s_platform.api.proxyMarshalFlags = MarshalFlags;
    s_platform.api.proxyAddListener = AddListener;
    s_platform.api.proxyGetVersion = GetVersion;
    s_platform.api.proxyDestroy = Destroy;
    s_platform.seat = (void*)NewProxy(proxyOther);
    s_platform.clipboard.manager = (void*)NewProxy(proxyOther);
    s_platform.clipboard.primaryManager = (void*)NewProxy(proxyOther);
    mwinWaylandInitClipboard(&s_platform.clipboard);
    mwinWaylandInitDrag(&s_platform.drag);
    mwinWaylandAttachClipboard(&s_platform);
    s_device = (Proxy*)s_platform.clipboard.device;
    s_primaryDevice = (Proxy*)s_platform.clipboard.primaryDevice;
    Expect(s_device != nullptr && s_device->listener != nullptr && s_primaryDevice != nullptr &&
           s_primaryDevice->listener != nullptr);
    s_test = context->backend;
    s_ops = *s_test;
    s_ops.submit = Submit;
    s_ops.now = Now;
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)user;
    Start(context);
    mwinWindowId* windows = s_ids;
    mwinWindowDef def = mwinDefaultWindowDef();
    for (uint32_t i = 0; i < WINDOWS; i++)
    {
        Expect(mwinCreateWindow(context, &def, &windows[i], nullptr) == mwin_success);
        s_windows[windows[i].index1 - 1].surface = (void*)&s_surfaces[windows[i].index1 - 1];
    }
    // An op makes a few objects at most: the script ends cleanly before the
    // stand-in could run out, whatever the input's length.
    while (s_size > 0 && s_proxyCount + 16 <= PROXIES)
    {
        uint8_t op = Byte();
        uint32_t i = (op >> 4) % WINDOWS;
        switch (op & 15)
        {
        case 0:
        case 1:
        case 2:
            Selection(op);
            break;
        case 3:
        case 4:
        case 5:
            Drag(op);
            break;
        case 6:
        case 7:
            Request(context, windows[i]);
            break;
        case 8:
        case 9:
            Write(op, false);
            break;
        case 10:
            Write(op, true);
            break;
        case 11:
        case 12:
            Pump(0);
            break;
        case 13:
            // Every read and drop past its deadline.
            Pump(UINT64_MAX);
            break;
        default:
            // A window goes while a drag or a read may be under way.
            s_windows[windows[i].index1 - 1].surface = nullptr;
            Expect(mwinDestroyWindow(context, windows[i]) == mwin_success &&
                   mwinCreateWindow(context, &def, &windows[i], nullptr) == mwin_success);
            s_windows[windows[i].index1 - 1].surface = (void*)&s_surfaces[windows[i].index1 - 1];
            break;
        }
        Drain(context);
    }
    // The seat goes: the drag ends, every offer goes, the read under way
    // fails, and the reads waiting are answered.
    mwinWaylandDetachClipboard(&s_platform);
    Drain(context);
    for (uint32_t r = 0; r < s_made; r++)
    {
        Expect(s_requests[r].completions == 1);
    }
    for (uint32_t p = 0; p < s_proxyCount; p++)
    {
        Expect(s_proxies[p].kind == proxyOther || s_proxies[p].destroyed);
    }
    for (int w = 0; w < WRITERS; w++)
    {
        CloseWriter(&s_writers[w]);
    }
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    (void)context;
    (void)user;
    return mwin_frameStop;
}

// The descriptors the process has open: every pipe a run opens is closed
// by its end.
static int OpenDescriptors(void)
{
    DIR* directory = opendir("/proc/self/fd");
    Expect(directory != nullptr);
    int count = 0;
    while (readdir(directory) != nullptr)
    {
        count++;
    }
    (void)closedir(directory);
    return count;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    // A reader the backend closed makes a write fail rather than end the
    // run.
    (void)signal(SIGPIPE, SIG_IGN);
    s_data = data;
    s_size = size;
    mwinAppDef def = mwinDefaultAppDef();
    def.context.backend = mwin_backendTest;
    def.context.limits.windows = SLOTS;
    def.context.limits.clipboardBytes = LIMIT;
    def.context.limits.dropBytes = LIMIT;
    def.context.limits.droppedFiles = FILES;
    def.init = Init;
    def.frame = Frame;
    int open = OpenDescriptors();
    Expect(mwinRun(&def) == mwin_success);
    Expect(OpenDescriptors() == open);
    return 0;
}
