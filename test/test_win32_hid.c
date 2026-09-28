// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 generic gamepads (src/win32_hid.c) against a stand-in for
// Raw Input and the HID parser, driven on a context of the test
// backend:
// - of the devices present, a PlayStation 4 pad and an unknown stick are
//   gamepads; XInput's pad, a keyboard and a vendor's device are not,
//   and a device found again is one;
// - the pad is mapped by the database's Windows entry, its product
//   string its name, its axes numbered by usage, not by the order its
//   descriptor gives them;
// - its report: buttons, sticks, triggers from whole axes, and the hat
//   as the d-pad;
// - the stick is raw: its buttons in usage order, a signed axis, and a
//   centred hat as two axes after the others;
// - a removed device is gone, and its reports read nothing.

#include "test_program.h"
#include "win32_hid.h"

#include <string.h>
#include <wchar.h>

#define PAD      ((HANDLE)0x11)
#define STICK    ((HANDLE)0x22)
#define XINPUT   ((HANDLE)0x33)
#define KEYBOARD ((HANDLE)0x44)
#define VENDOR   ((HANDLE)0x55)

// A report of the stand-in's: the Button page's usages down, as bits
// from usage 1, and each Generic Desktop value by usage from X.
typedef struct Report
{
    uint64_t down;
    ULONG values[10];
} Report;

typedef struct Device
{
    HANDLE handle;
    const wchar_t* path;
    USHORT page;
    USHORT usage;
    DWORD vendor;
    DWORD product;
} Device;

#define DEVICES 5

static const Device s_devices[DEVICES] = {
    {PAD, L"\\\\?\\HID#VID_054C&PID_05C4#pad", HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_GAMEPAD,
     0x054C, 0x05C4},
    {STICK, L"\\\\?\\HID#VID_1234&PID_5678#stick", HID_USAGE_PAGE_GENERIC,
     HID_USAGE_GENERIC_JOYSTICK, 0x1234, 0x5678},
    {XINPUT, L"\\\\?\\HID#VID_045E&PID_028E&IG_00#xinput", HID_USAGE_PAGE_GENERIC,
     HID_USAGE_GENERIC_GAMEPAD, 0x045E, 0x028E},
    {KEYBOARD, L"\\\\?\\HID#VID_1111&PID_2222#keyboard", HID_USAGE_PAGE_GENERIC,
     HID_USAGE_GENERIC_KEYBOARD, 0x1111, 0x2222},
    // A gamepad's usage on a vendor's page.
    {VENDOR, L"\\\\?\\HID#VID_3333&PID_4444#vendor", 0xFF00, HID_USAGE_GENERIC_GAMEPAD, 0x3333,
     0x4444},
};

static mwinWin32Hid s_hid;
static Report s_report;
static HANDLE s_sender;

static const Device* DeviceOf(HANDLE handle)
{
    for (size_t i = 0; i < DEVICES; i++)
    {
        if (s_devices[i].handle == handle)
        {
            return &s_devices[i];
        }
    }
    return nullptr;
}

static UINT WINAPI DeviceList(PRAWINPUTDEVICELIST list, PUINT count, UINT size)
{
    (void)size;
    for (size_t i = 0; i < DEVICES && i < *count; i++)
    {
        list[i] = (RAWINPUTDEVICELIST){s_devices[i].handle, RIM_TYPEHID};
    }
    // Found again: once is all.
    list[DEVICES] = (RAWINPUTDEVICELIST){PAD, RIM_TYPEHID};
    return DEVICES + 1;
}

// The preparsed data names its device.
static UINT WINAPI DeviceInfo(HANDLE handle, UINT command, LPVOID data, PUINT size)
{
    const Device* device = DeviceOf(handle);
    if (command == RIDI_DEVICEINFO)
    {
        RID_DEVICE_INFO* info = data;
        info->dwType = RIM_TYPEHID;
        info->hid = (RID_DEVICE_INFO_HID){device->vendor, device->product, 0x0100, device->page,
                                          device->usage};
        return sizeof(*info);
    }
    if (command == RIDI_DEVICENAME)
    {
        UINT units = (UINT)wcslen(device->path) + 1;
        memcpy(data, device->path, units * sizeof(wchar_t));
        return units;
    }
    if (data == nullptr)
    {
        *size = sizeof(HANDLE);
        return 0;
    }
    memcpy(data, &handle, sizeof(HANDLE));
    return sizeof(HANDLE);
}

static HANDLE Owner(PHIDP_PREPARSED_DATA data)
{
    HANDLE handle;
    memcpy(&handle, data, sizeof(handle));
    return handle;
}

static HIDP_VALUE_CAPS Value(USAGE page, USAGE usage, USAGE last, USHORT bits, LONG minimum,
                             LONG maximum)
{
    HIDP_VALUE_CAPS caps = {
        .UsagePage = page, .BitSize = bits, .LogicalMin = minimum, .LogicalMax = maximum};
    caps.IsRange = last != usage;
    if (caps.IsRange)
    {
        caps.Range.UsageMin = usage;
        caps.Range.UsageMax = last;
    }
    else
    {
        caps.NotRange.Usage = usage;
    }
    return caps;
}

static HIDP_BUTTON_CAPS Buttons(USAGE first, USAGE last)
{
    HIDP_BUTTON_CAPS caps = {.UsagePage = HID_USAGE_PAGE_BUTTON, .IsRange = first != last};
    if (caps.IsRange)
    {
        caps.Range.UsageMin = first;
        caps.Range.UsageMax = last;
    }
    else
    {
        caps.NotRange.Usage = first;
    }
    return caps;
}

// The pad's controls in its descriptor's order: X, Y, Z and Rz, the hat,
// then Rx and Ry, and one of a vendor's page; the stick's buttons 3,
// then 1 and 2, and a signed X and Y.
static USHORT ValueCaps(HANDLE owner, HIDP_VALUE_CAPS* caps)
{
    if (owner == PAD)
    {
        const HIDP_VALUE_CAPS pad[8] = {
            Value(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_X, HID_USAGE_GENERIC_X, 8, 0, 255),
            Value(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_Y, HID_USAGE_GENERIC_Y, 8, 0, 255),
            Value(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_Z, HID_USAGE_GENERIC_Z, 8, 0, 255),
            Value(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_RZ, HID_USAGE_GENERIC_RZ, 8, 0, 255),
            Value(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_HATSWITCH, HID_USAGE_GENERIC_HATSWITCH,
                  4, 0, 7),
            Value(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_RX, HID_USAGE_GENERIC_RX, 8, 0, 255),
            Value(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_RY, HID_USAGE_GENERIC_RY, 8, 0, 255),
            Value(0xFF00, 0x20, 0x20, 8, 0, 255),
        };
        memcpy(caps, pad, sizeof(pad));
        return 8;
    }
    caps[0] = Value(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_X, HID_USAGE_GENERIC_Y, 8, -128, 127);
    caps[1] = Value(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_HATSWITCH,
                    HID_USAGE_GENERIC_HATSWITCH, 4, 0, 7);
    return 2;
}

static NTSTATUS NTAPI GetCaps(PHIDP_PREPARSED_DATA data, PHIDP_CAPS caps)
{
    HIDP_VALUE_CAPS values[8];
    *caps = (HIDP_CAPS){.NumberInputButtonCaps = Owner(data) == PAD ? 1 : 2,
                        .NumberInputValueCaps = ValueCaps(Owner(data), values)};
    return HIDP_STATUS_SUCCESS;
}

static NTSTATUS NTAPI ButtonCaps(HIDP_REPORT_TYPE type, PHIDP_BUTTON_CAPS caps, PUSHORT count,
                                 PHIDP_PREPARSED_DATA data)
{
    (void)type;
    if (Owner(data) == PAD)
    {
        caps[0] = Buttons(1, 14);
        *count = 1;
        return HIDP_STATUS_SUCCESS;
    }
    caps[0] = Buttons(3, 3);
    caps[1] = Buttons(1, 2);
    *count = 2;
    return HIDP_STATUS_SUCCESS;
}

static NTSTATUS NTAPI ValueCapsOf(HIDP_REPORT_TYPE type, PHIDP_VALUE_CAPS caps, PUSHORT count,
                                  PHIDP_PREPARSED_DATA data)
{
    (void)type;
    *count = ValueCaps(Owner(data), caps);
    return HIDP_STATUS_SUCCESS;
}

static NTSTATUS NTAPI Usages(HIDP_REPORT_TYPE type, USAGE page, USHORT link, PUSAGE list,
                             PULONG length, PHIDP_PREPARSED_DATA data, PCHAR report,
                             ULONG reportLength)
{
    (void)type;
    (void)link;
    (void)data;
    Report read;
    memcpy(&read, report, reportLength < sizeof(read) ? reportLength : sizeof(read));
    ULONG count = 0;
    for (USAGE usage = 1; page == HID_USAGE_PAGE_BUTTON && usage <= 64; usage++)
    {
        if ((read.down >> (usage - 1) & 1u) != 0 && count < *length)
        {
            list[count++] = usage;
        }
    }
    *length = count;
    return HIDP_STATUS_SUCCESS;
}

static NTSTATUS NTAPI UsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                 PULONG value, PHIDP_PREPARSED_DATA data, PCHAR report,
                                 ULONG reportLength)
{
    (void)type;
    (void)link;
    (void)data;
    Report read;
    memcpy(&read, report, reportLength < sizeof(read) ? reportLength : sizeof(read));
    if (page != HID_USAGE_PAGE_GENERIC || usage < HID_USAGE_GENERIC_X ||
        usage >= HID_USAGE_GENERIC_X + 10)
    {
        return HIDP_STATUS_USAGE_NOT_FOUND;
    }
    *value = read.values[usage - HID_USAGE_GENERIC_X];
    return HIDP_STATUS_SUCCESS;
}

// The pad's file opens and names it; the stick's names nothing.
static HANDLE WINAPI OpenDevice(LPCWSTR path, DWORD access, DWORD share,
                                LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags,
                                HANDLE model)
{
    (void)access;
    (void)share;
    (void)security;
    (void)creation;
    (void)flags;
    (void)model;
    return wcscmp(path, s_devices[0].path) == 0 ? PAD : INVALID_HANDLE_VALUE;
}

static BOOL WINAPI CloseFile(HANDLE file)
{
    return file == PAD;
}

static BOOLEAN NTAPI ProductString(HANDLE file, PVOID buffer, ULONG length)
{
    static const wchar_t name[] = L"Wireless Controller é";
    if (file != PAD || length < sizeof(name))
    {
        return FALSE;
    }
    memcpy(buffer, name, sizeof(name));
    return TRUE;
}

// A report from the device that sent it last.
static UINT WINAPI InputData(HRAWINPUT input, UINT command, LPVOID data, PUINT size, UINT header)
{
    (void)input;
    (void)command;
    (void)header;
    RAWINPUT* raw = data;
    raw->header = (RAWINPUTHEADER){RIM_TYPEHID, 0, s_sender, 0};
    raw->data.hid.dwSizeHid = sizeof(Report);
    raw->data.hid.dwCount = 1;
    memcpy(raw->data.hid.bRawData, &s_report, sizeof(Report));
    *size = (UINT)(offsetof(RAWINPUT, data.hid.bRawData) + sizeof(Report));
    return *size;
}

static bool Near(float a, float b)
{
    return a - b < 1e-4f && b - a < 1e-4f;
}

static void Send(HANDLE device, uint64_t down, const ULONG values[10])
{
    s_sender = device;
    s_report.down = down;
    memcpy(s_report.values, values, sizeof(s_report.values));
    mwinWin32HidInput(&s_hid, (HRAWINPUT)0x99, 2);
}

// The first two gamepads added: the pad's and the stick's ids.
static void Ids(const Program* program, mwinGamepadId ids[2])
{
    int found = 0;
    for (int i = 0; i < program->eventCount && found < 2; i++)
    {
        if (program->events[i].type == mwin_eventGamepadAdded)
        {
            ids[found++] = program->events[i].data.gamepad;
        }
    }
}

static void CheckFound(const mwinContext* context, const mwinGamepadId ids[2], int added)
{
    mwinGamepadInfo pad;
    mwinGamepadInfo stick;
    CHECK(added == 2 && mwinGetGamepadInfo(context, ids[0], &pad) == mwin_success &&
              mwinGetGamepadInfo(context, ids[1], &stick) == mwin_success,
          "of the devices, the pad and the stick gamepads, once");
    CHECK(pad.mapped && pad.vendor == 0x054C && pad.product == 0x05C4 && pad.nameLength == 22 &&
              memcmp(pad.name, "Wireless Controller \xC3\xA9", 22) == 0 && pad.battery == -1 &&
              pad.capabilities == 0,
          "the pad mapped, named by its product string, without rumble or battery");
    CHECK(!stick.mapped && stick.rawButtons == 3 && stick.rawAxes == 4 && stick.nameLength == 11 &&
              memcmp(stick.name, "HID gamepad", 11) == 0,
          "the stick raw, with a plain name");
}

static void CheckPad(const mwinContext* context, mwinGamepadId id)
{
    mwinGamepadState state;
    CHECK(mwinGetGamepadState(context, id, &state) == mwin_success &&
              state.buttons ==
                  (1u << mwin_padFaceSouth | 1u << mwin_padGuide | 1u << mwin_padDpadRight),
          "the pad's buttons, and its hat as the d-pad");
    CHECK(Near(state.axes[mwin_padStickLeftX], 1.0f) &&
              Near(state.axes[mwin_padStickLeftY], -1.0f) &&
              Near(state.axes[mwin_padStickRightX], 1.0f / 255.0f) &&
              Near(state.axes[mwin_padStickRightY], -1.0f),
          "its sticks, Rz numbered after Rx and Ry");
    CHECK(Near(state.axes[mwin_padTriggerLeft], 1.0f) &&
              Near(state.axes[mwin_padTriggerRight], (1.0f + 1.0f / 255.0f) / 2.0f),
          "its triggers from whole axes");
}

static void CheckStick(const mwinContext* context, mwinGamepadId id)
{
    mwinGamepadState state;
    CHECK(mwinGetGamepadState(context, id, &state) == mwin_success && state.buttons == 0x5u &&
              Near(state.axes[0], -1.0f) && Near(state.axes[1], 1.0f) &&
              Near(state.axes[2], 0.0f) && Near(state.axes[3], 0.0f),
          "the stick's buttons in usage order, its signed axes, and its centred hat");
}

static void Step(Program* program, mwinContext* context, int step)
{
    static mwinGamepadId ids[2];
    Drain(program, context);
    int added = 0;
    for (int i = 0; i < program->eventCount; i++)
    {
        added += program->events[i].type == mwin_eventGamepadAdded;
    }
    switch (step)
    {
    case 0:
        s_hid = (mwinWin32Hid){.context = context};
        s_hid.api =
            (mwinHidApi){nullptr, DeviceList, DeviceInfo,  InputData, OpenDevice, CloseFile,
                         GetCaps, ButtonCaps, ValueCapsOf, Usages,    UsageValue, ProductString};
        mwinWin32HidFind(&s_hid, 1);
        mwinWin32HidArrive(&s_hid, STICK, 1);
        break;
    case 1:
        Ids(program, ids);
        CheckFound(context, ids, added);
        // Buttons 2 (b1) and 13 (b12); X right, Y up, Z centred, Rx
        // full, Ry half, Rz up; the hat right.
        Send(PAD, 1u << 1 | 1u << 12, (const ULONG[10]){255, 0, 0x80, 255, 0x80, 0, 0, 0, 0, 2});
        // Buttons 1 and 3; X at -128 and Y at 127 as 8-bit values; the
        // hat outside its range.
        Send(STICK, 1u << 0 | 1u << 2, (const ULONG[10]){0x80, 0x7F, 0, 0, 0, 0, 0, 0, 0, 8});
        break;
    case 2:
        CheckPad(context, ids[0]);
        CheckStick(context, ids[1]);
        mwinWin32HidRemove(&s_hid, STICK, 3);
        mwinWin32HidRemove(&s_hid, KEYBOARD, 3);
        Send(STICK, 0, (const ULONG[10]){0});
        break;
    default:
    {
        mwinGamepadState state;
        CHECK(program->eventCount == 1 && program->events[0].type == mwin_eventGamepadRemoved &&
                  mwinGetGamepadState(context, ids[1], &state) == mwin_errorStale,
              "a removed device gone, its reports reading nothing");
        mwinWin32HidStop(&s_hid);
        program->done = true;
        break;
    }
    }
}

int main(void)
{
    Program program = {.step = Step};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
