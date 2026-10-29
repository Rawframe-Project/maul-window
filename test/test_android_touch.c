// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A touch screen, a pen and a mouse in the emulator
// (tools/run_android_app.sh), made over USB by Android's hid tool through
// the kernel's uhid, so that the kernel's drivers make them as for real
// ones: a touch screen of two contacts (hid-multitouch), a pen digitizer
// with a barrel button and an eraser end, and a mouse. Android puts the
// touch screen and the pen on the built-in display, as it does an
// external touch screen when there is no external display. After a frame
// shown (Android 14 and later send no touch to a window whose surface
// never showed one), each in turn:
//
// Two fingers: one down, a second down while the first is held (a
// pointer's own press, which the system's input command cannot make), the
// first moved, the second lifted (its own release), then the first. They
// are two touches with their own ids, each lifted as Android says.
//
// The pen: hovering, its tip down with half its pressure, its barrel
// button held and let go, lifted, out of range; then turned over, the
// eraser end hovering, down and lifted. The pen hovers without contact,
// touches with its pressure, tells its barrel button, and its eraser end
// is the eraser. Its place is checked against the fingers', the two
// devices spanning the display alike, so that no window offset or scale
// enters.
//
// The mouse: moved, its left button clicked. It is the cursor and a
// button, not a touch.

#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/native_window.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// The first phase waits for the devices too.
#define DEADLINE_NS 30000000000ull
#define FILES       "/data/data/" MWIN_TEST_PACKAGE "/files/"
#define OUT_PATH    FILES "out"
#define RECORDS     256
#define LAST_PHASE  3

// The touch screen: two fingers, each its tip switch, its contact id and
// its place from 0 to 4095, then the count of contacts in the report;
// the most contacts as a feature report the driver reads.
static const uint8_t s_touchDescriptor[] = {
    0x05, 0x0D, 0x09, 0x04, 0xA1, 0x01, 0x85, 0x01,
    // The first finger.
    0x09, 0x22, 0xA1, 0x02, 0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x07, 0x81, 0x03, 0x09, 0x51, 0x25, 0x7F, 0x75, 0x08, 0x81, 0x02, 0x05, 0x01, 0x26, 0xFF,
    0x0F, 0x75, 0x10, 0x35, 0x00, 0x46, 0xFF, 0x0F, 0x09, 0x30, 0x09, 0x31, 0x95, 0x02, 0x81, 0x02,
    0x05, 0x0D, 0xC0,
    // The second finger.
    0x09, 0x22, 0xA1, 0x02, 0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x07, 0x81, 0x03, 0x09, 0x51, 0x25, 0x7F, 0x75, 0x08, 0x81, 0x02, 0x05, 0x01, 0x26, 0xFF,
    0x0F, 0x75, 0x10, 0x35, 0x00, 0x46, 0xFF, 0x0F, 0x09, 0x30, 0x09, 0x31, 0x95, 0x02, 0x81, 0x02,
    0x05, 0x0D, 0xC0,
    // The contact count, and the most contacts.
    0x09, 0x54, 0x15, 0x00, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0x85, 0x02, 0x09, 0x55,
    0x25, 0x02, 0x75, 0x08, 0x95, 0x01, 0xB1, 0x02, 0xC0};

// The pen: its tip, barrel, invert, eraser and in range switches, its
// place from 0 to 4095 and its tip's pressure from 0 to 1023.
static const uint8_t s_penDescriptor[] = {
    0x05, 0x0D, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x20, 0xA1, 0x00, 0x09, 0x42,
    0x09, 0x44, 0x09, 0x3C, 0x09, 0x45, 0x09, 0x32, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x05, 0x81, 0x02, 0x95, 0x03, 0x81, 0x03, 0x05, 0x01, 0x26, 0xFF, 0x0F, 0x75,
    0x10, 0x95, 0x01, 0x35, 0x00, 0x46, 0xFF, 0x0F, 0x09, 0x30, 0x81, 0x02, 0x09, 0x31,
    0x81, 0x02, 0x05, 0x0D, 0x09, 0x30, 0x26, 0xFF, 0x03, 0x81, 0x02, 0xC0, 0xC0};

// The mouse: three buttons and a relative move.
static const uint8_t s_mouseDescriptor[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19,
    0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15,
    0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0};

// The pen's switches.
enum
{
    TIP = 1,
    BARREL = 2,
    INVERT = 4,
    ERASER = 8,
    IN_RANGE = 16,
};

// Where the fingers and the pen go, in the devices' units.
enum
{
    FIRST_X = 1000,
    FIRST_Y = 1500,
    SECOND_X = 3000,
    SECOND_Y = 2500,
    PEN_X = 2000,
    PEN_Y = 2000,
    PEN_PRESSURE = 512,
};

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    // Whether a surface came back since the last frame drawn.
    bool restored;
    // The input records since the phase began.
    mwinEvent records[RECORDS];
    int recordCount;
    // Whether another test's devices kept this run from making its own.
    bool blocked;
    // Where the two fingers went down, in logical units.
    mwinPosition first;
    mwinPosition second;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void PutBytes(FILE* file, const uint8_t* bytes, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        fprintf(file, "%s%u", i == 0 ? "" : ",", (unsigned)bytes[i]);
    }
}

static void PutRegister(FILE* file, int id, const char* name, const uint8_t* descriptor,
                        size_t count, bool feature)
{
    fprintf(file,
            "{\"id\":%d,\"command\":\"register\",\"name\":\"%s\",\"vid\":4617,\"pid\":%d,"
            "\"bus\":\"usb\",\"descriptor\":[",
            id, name, id);
    PutBytes(file, descriptor, count);
    fprintf(file, "]%s}\n", feature ? ",\"feature_reports\":[{\"id\":2,\"data\":[2,2]}]" : "");
}

static void PutReport(FILE* file, int id, const uint8_t* report, size_t count)
{
    fprintf(file, "{\"id\":%d,\"command\":\"report\",\"report\":[", id);
    PutBytes(file, report, count);
    fprintf(file, "]}\n{\"id\":%d,\"command\":\"delay\",\"duration\":50}\n", id);
}

// A touch screen's report: each finger touching or not, at its place.
static void PutFingers(FILE* file, bool first, int firstX, bool second, int secondY)
{
    uint8_t report[] = {
        1,
        first,
        0,
        (uint8_t)(firstX & 0xFF),
        (uint8_t)(firstX >> 8),
        FIRST_Y & 0xFF,
        FIRST_Y >> 8,
        second,
        1,
        SECOND_X & 0xFF,
        SECOND_X >> 8,
        (uint8_t)(secondY & 0xFF),
        (uint8_t)(secondY >> 8),
        2,
    };
    PutReport(file, 1, report, sizeof(report));
}

static void PutPen(FILE* file, uint8_t switches, int pressure)
{
    uint8_t report[] = {
        1,
        switches,
        PEN_X & 0xFF,
        PEN_X >> 8,
        PEN_Y & 0xFF,
        PEN_Y >> 8,
        (uint8_t)(pressure & 0xFF),
        (uint8_t)(pressure >> 8),
    };
    PutReport(file, 2, report, sizeof(report));
}

static void PutMouse(FILE* file, uint8_t buttons, int8_t x, int8_t y)
{
    uint8_t report[] = {buttons, (uint8_t)x, (uint8_t)y};
    PutReport(file, 3, report, sizeof(report));
}

// This run's number, in its devices' names and in the file touch-run, so
// that a script an earlier run left tells its own devices from this
// run's, and stops once a later run has begun.
static int s_run;

// The script's tests: whether its run is the latest, whether the test
// asked for a file, and a bounded wait for one.
static void PutScriptFunctions(FILE* script)
{
    fprintf(script,
            "mine() {\n"
            "    [ \"$(run-as " MWIN_TEST_PACKAGE " cat files/touch-run 2> /dev/null)\" = %d ]\n"
            "}\n"
            "asked() {\n"
            "    mine && run-as " MWIN_TEST_PACKAGE " ls files/$1 > /dev/null 2>&1\n"
            "}\n"
            "wait_for() {\n"
            "    i=0\n"
            "    while [ $i -lt 600 ] && mine && ! asked $1; do\n"
            "        sleep 0.1\n"
            "        i=$((i + 1))\n"
            "    done\n"
            "    asked $1\n"
            "}\n",
            s_run);
}

// Writes the hid tool's commands and the script that feeds them: the
// devices, then, once the input service lists all three, the fingers;
// the pen once the test asks for it (the file touch-pen), and the mouse
// once it asks for that (touch-mouse). The devices stay until the test
// ends (touch-done) or a later run begins; the hid tool removes them
// when its input closes.
static bool WriteScript(void)
{
    const char* const names[5] = {"touch-add.json", "touch-fingers.json", "touch-pen.json",
                                  "touch-mouse.json", "touch.sh"};
    FILE* files[5] = {0};
    bool opened = true;
    for (int i = 0; i < 5; i++)
    {
        char path[128];
        (void)snprintf(path, sizeof(path), FILES "%s", names[i]);
        files[i] = fopen(path, "w");
        opened = opened && files[i] != nullptr;
    }
    if (opened)
    {
        char touch[32];
        char pen[32];
        char mouse[32];
        (void)snprintf(touch, sizeof(touch), "Maul Touch %d", s_run);
        (void)snprintf(pen, sizeof(pen), "Maul Pen %d", s_run);
        (void)snprintf(mouse, sizeof(mouse), "Maul Mouse %d", s_run);
        PutRegister(files[0], 1, touch, s_touchDescriptor, sizeof(s_touchDescriptor), true);
        PutRegister(files[0], 2, pen, s_penDescriptor, sizeof(s_penDescriptor), false);
        PutRegister(files[0], 3, mouse, s_mouseDescriptor, sizeof(s_mouseDescriptor), false);
        PutFingers(files[1], true, FIRST_X, false, SECOND_Y);
        PutFingers(files[1], true, FIRST_X, true, SECOND_Y);
        PutFingers(files[1], true, FIRST_X + 100, true, SECOND_Y + 100);
        PutFingers(files[1], true, FIRST_X + 100, false, SECOND_Y + 100);
        PutFingers(files[1], false, FIRST_X + 100, false, SECOND_Y + 100);
        PutPen(files[2], IN_RANGE, 0);
        PutPen(files[2], IN_RANGE | TIP, PEN_PRESSURE);
        PutPen(files[2], IN_RANGE | TIP | BARREL, PEN_PRESSURE);
        PutPen(files[2], IN_RANGE | TIP, PEN_PRESSURE);
        PutPen(files[2], IN_RANGE, 0);
        PutPen(files[2], 0, 0);
        PutPen(files[2], IN_RANGE | INVERT, 0);
        PutPen(files[2], IN_RANGE | INVERT | ERASER, PEN_PRESSURE);
        PutPen(files[2], IN_RANGE | INVERT, 0);
        PutPen(files[2], 0, 0);
        PutMouse(files[3], 0, 20, 0);
        PutMouse(files[3], 0, 0, 20);
        PutMouse(files[3], 1, 0, 0);
        PutMouse(files[3], 0, 0, 0);
        FILE* script = files[4];
        PutScriptFunctions(script);
        // Android takes devices of the same vendor, product and place for
        // one: an earlier run's devices, still there, would merge with
        // this run's, and two pens' hovering in one device stops the
        // system (its input dispatcher aborts). They go once their script
        // sees this run begun, or once the runner uninstalls the test.
        // Another test's devices change what reaches the window too (a
        // gamepad's touchpad is a mouse to Android), so this run waits for
        // every Maul device to go. When they stay 15 seconds, well within
        // the phase's deadline, it makes none and says whose they are (the
        // file touch-blocked).
        fprintf(script, "others() {\n"
                        "    dumpsys input | grep -E '^ +[0-9]+: Maul '\n"
                        "}\n"
                        "began=$SECONDS\n"
                        "while [ $((SECONDS - began)) -lt 15 ] && mine && others > /dev/null; do\n"
                        "    sleep 0.1\n"
                        "done\n"
                        "if ! mine; then\n"
                        "    exit 0\n"
                        "fi\n"
                        "if others > /dev/null; then\n"
                        "    others | run-as " MWIN_TEST_PACKAGE " sh -c '\n"
                        "        cat > files/touch-blocked.part && mv files/touch-blocked.part "
                        "files/touch-blocked'\n"
                        "    exit 0\n"
                        "fi\n");
        fprintf(script, "(run-as " MWIN_TEST_PACKAGE " cat files/touch-add.json\n");
        // Android reads a device's reports only once its input hub has
        // opened the device's node: until then they would be lost. Only
        // this run's devices count.
        fprintf(script,
                "i=0\n"
                "while [ $i -lt 300 ] && mine && [ \"$(dumpsys input | "
                "grep -cE '^ +[0-9]+: Maul (Touch|Pen|Mouse) %d$')\" -lt 3 ]; do\n"
                "    sleep 0.1\n"
                "    i=$((i + 1))\n"
                "done\n"
                "mine && run-as " MWIN_TEST_PACKAGE " cat files/touch-fingers.json\n"
                "wait_for touch-pen && run-as " MWIN_TEST_PACKAGE " cat files/touch-pen.json &&\n"
                "    wait_for touch-mouse && run-as " MWIN_TEST_PACKAGE
                " cat files/touch-mouse.json\n"
                "wait_for touch-done) | hid -\n",
                s_run);
    }
    bool closed = true;
    for (int i = 0; i < 5; i++)
    {
        closed = (files[i] == nullptr || fclose(files[i]) == 0) && closed;
    }
    return opened && closed;
}

// Says whether the script found another test's devices still there
// and made none, and prints which.
static bool Blocked(void)
{
    FILE* file = fopen(FILES "touch-blocked", "r");
    if (file == nullptr)
    {
        return false;
    }
    printf("another test's devices are still there, so this run made none:\n");
    char line[128];
    while (fgets(line, sizeof(line), file) != nullptr)
    {
        printf("  %s", line);
    }
    (void)fclose(file);
    return true;
}

// Puts a file the script waits for.
static bool Flag(const char* name)
{
    char path[128];
    (void)snprintf(path, sizeof(path), FILES "%s", name);
    FILE* file = fopen(path, "w");
    return file != nullptr && fclose(file) == 0;
}

static bool IsInput(mwinEventType type)
{
    return (type >= mwin_eventKeyDown && type <= mwin_eventPenButtonUp);
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.type == mwin_eventShown;
        program->restored |= event.type == mwin_eventSurfaceRestored;
        if (IsInput(event.type) && program->recordCount < RECORDS)
        {
            program->records[program->recordCount++] = event;
        }
    }
}

// The index of the first record of a type at or after an index, of a
// pen with the flags given (any record when the type is no pen's), or -1.
static int FindFrom(const Program* program, int from, mwinEventType type, mwinPenFlags mask,
                    mwinPenFlags flags)
{
    bool pen = type >= mwin_eventPenMoved && type <= mwin_eventPenButtonUp;
    for (int i = from < 0 ? 0 : from; i < program->recordCount; i++)
    {
        const mwinEvent* record = &program->records[i];
        if (record->type == type && (!pen || (record->data.pen.flags & mask) == flags))
        {
            return i;
        }
    }
    return -1;
}

static int Find(const Program* program, mwinEventType type)
{
    return FindFrom(program, 0, type, 0, 0);
}

static int Count(const Program* program, mwinEventType type)
{
    int count = 0;
    for (int i = 0; i < program->recordCount; i++)
    {
        count += program->records[i].type == type;
    }
    return count;
}

// The index of a touch's record of a type, or -1.
static int TouchOf(const Program* program, mwinEventType type, uint64_t id)
{
    for (int i = 0; i < program->recordCount; i++)
    {
        if (program->records[i].type == type && program->records[i].data.touch.id == id)
        {
            return i;
        }
    }
    return -1;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case 0:
        return program->shown;
    case 1:
        return Count(program, mwin_eventTouchUp) >= 2;
    case 2:
        return FindFrom(program, 0, mwin_eventPenUp, mwin_penEraser, mwin_penEraser) >= 0;
    case 3:
        return Find(program, mwin_eventButtonUp) >= 0;
    default:
        return true;
    }
}

// The fingers: two touches, the second down while the first is held and
// lifted first, the first moved, each lifted where it last was.
static void CheckFingers(Program* program)
{
    int firstDown = Find(program, mwin_eventTouchDown);
    int secondDown = FindFrom(program, firstDown + 1, mwin_eventTouchDown, 0, 0);
    CHECK(firstDown >= 0 && secondDown > firstDown && Count(program, mwin_eventTouchDown) == 2,
          "two fingers down, one after the other");
    if (firstDown < 0 || secondDown < 0)
    {
        return;
    }
    const mwinTouchEvent* first = &program->records[firstDown].data.touch;
    const mwinTouchEvent* second = &program->records[secondDown].data.touch;
    program->first = first->position;
    program->second = second->position;
    int firstUp = TouchOf(program, mwin_eventTouchUp, first->id);
    int secondUp = TouchOf(program, mwin_eventTouchUp, second->id);
    CHECK(first->id != second->id && secondUp > secondDown && firstUp > secondUp &&
              TouchOf(program, mwin_eventTouchDown, first->id) == firstDown,
          "each finger its own touch, the second lifted while the first is held");
    CHECK(second->position.x > first->position.x + 10.0f &&
              second->position.y > first->position.y + 10.0f,
          "the second finger right of and below the first");
    int moved = TouchOf(program, mwin_eventTouchMoved, first->id);
    CHECK(moved > secondDown && moved < firstUp &&
              program->records[moved].data.touch.position.x > first->position.x &&
              program->records[firstUp].data.touch.position.x > first->position.x,
          "the first finger moved right while both are down, and lifted there");
    CHECK(Find(program, mwin_eventPenDown) < 0 && Find(program, mwin_eventButtonDown) < 0,
          "fingers are no pen and no button");
}

// Where a place falls between the fingers, from 0 at the first to 1 at
// the second, across and down.
static mwinPosition Between(const Program* program, mwinPosition position)
{
    return (mwinPosition){(position.x - program->first.x) / (program->second.x - program->first.x),
                          (position.y - program->first.y) / (program->second.y - program->first.y)};
}

// The pen, then its eraser end.
static void CheckPen(const Program* program)
{
    int hover = FindFrom(program, 0, mwin_eventPenMoved, mwin_penContact | mwin_penEraser, 0);
    int down = FindFrom(program, 0, mwin_eventPenDown, mwin_penEraser, 0);
    int barrel = Find(program, mwin_eventPenButtonDown);
    int barrelUp = Find(program, mwin_eventPenButtonUp);
    int up = FindFrom(program, 0, mwin_eventPenUp, mwin_penEraser, 0);
    CHECK(hover >= 0 && down > hover, "the pen hovering, then down");
    if (down < 0)
    {
        return;
    }
    const mwinPenEvent* pen = &program->records[down].data.pen;
    mwinPosition between = Between(program, pen->position);
    printf("  the pen down at %.3f %.3f between the fingers, pressure %.3f\n", (double)between.x,
           (double)between.y, (double)pen->pressure);
    CHECK((pen->flags & mwin_penContact) != 0 &&
              fabsf(pen->pressure - (float)PEN_PRESSURE / 1023.0f) < 0.05f,
          "the pen touching, with half its pressure");
    CHECK(fabsf(between.x - 0.5f) < 0.02f && fabsf(between.y - 0.5f) < 0.02f,
          "the pen halfway between the fingers");
    CHECK(barrel > down && program->records[barrel].data.pen.button == 1 && barrelUp > barrel &&
              up > barrelUp,
          "its barrel button held and let go while it touches");
    CHECK(up > down && (program->records[up].data.pen.flags & mwin_penContact) == 0,
          "the pen lifted");
    int eraserHover =
        FindFrom(program, up, mwin_eventPenMoved, mwin_penContact | mwin_penEraser, mwin_penEraser);
    int eraserDown = FindFrom(program, up, mwin_eventPenDown, mwin_penEraser, mwin_penEraser);
    int eraserUp = FindFrom(program, up, mwin_eventPenUp, mwin_penEraser, mwin_penEraser);
    CHECK(eraserHover > up && eraserDown > eraserHover && eraserUp > eraserDown,
          "the eraser end hovering, down and lifted, as the eraser");
    CHECK(Find(program, mwin_eventTouchDown) < 0 && Find(program, mwin_eventButtonDown) < 0,
          "a pen is no touch and no button");
}

static void CheckMouse(const Program* program)
{
    int moved = Find(program, mwin_eventCursorMoved);
    int down = Find(program, mwin_eventButtonDown);
    int up = Find(program, mwin_eventButtonUp);
    CHECK(moved >= 0 && down > moved, "the mouse moved the cursor, then pressed");
    CHECK(down >= 0 && program->records[down].data.pointer.button == mwin_buttonLeft && up > down &&
              program->records[up].data.pointer.button == mwin_buttonLeft,
          "its left button down and up");
    CHECK(Find(program, mwin_eventTouchDown) < 0 && Find(program, mwin_eventPenDown) < 0,
          "a mouse is no touch and no pen");
}

// Shows a frame, drawn on the CPU: Android 14 and later send no touch to
// a window whose surface never showed one.
static void Paint(const Program* program, mwinContext* context)
{
    mwinNativeHandles handles = {0};
    if (mwinGetNativeHandles(context, program->window, &handles) != mwin_success)
    {
        return;
    }
    ANativeWindow* window = handles.handles.android.window;
    ANativeWindow_Buffer buffer;
    (void)ANativeWindow_setBuffersGeometry(window, 0, 0, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
    if (ANativeWindow_lock(window, &buffer, nullptr) == 0)
    {
        for (int32_t row = 0; row < buffer.height; row++)
        {
            memset((uint8_t*)buffer.bits + (size_t)row * (size_t)buffer.stride * 4u, 0x40,
                   (size_t)buffer.width * 4u);
        }
        (void)ANativeWindow_unlockAndPost(window);
    }
}

// Prints a phase's records by type, so that a failed
// check shows what came.
static void PrintRecords(const Program* program)
{
    printf("phase %d's %d records:", program->phase, program->recordCount);
    for (int i = 0; i < program->recordCount; i++)
    {
        printf(" %d", (int)program->records[i].type);
    }
    printf("\n");
}

static void Advance(Program* program, mwinContext* context)
{
    int failures = s_failures;
    switch (program->phase)
    {
    case 0:
        Paint(program, context);
        CHECK(WriteScript(), "the hid script written");
        printf("adb: sh -c 'nohup sh -c \"$(run-as " MWIN_TEST_PACKAGE
               " cat files/touch.sh)\" > /data/local/tmp/mwin-touch.out 2>&1 &'\n");
        break;
    case 1:
        CheckFingers(program);
        CHECK(Flag("touch-pen"), "the pen asked for");
        break;
    case 2:
        CheckPen(program);
        CHECK(Flag("touch-mouse"), "the mouse asked for");
        break;
    case 3:
        CheckMouse(program);
        break;
    default:
        break;
    }
    if (s_failures > failures)
    {
        PrintRecords(program);
    }
    program->phase += 1;
    program->recordCount = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startNs = NowNs();
    // The flags a run before this one may have left; then this run is the
    // latest, which ends an earlier run's script and its devices.
    (void)remove(FILES "touch-pen");
    (void)remove(FILES "touch-mouse");
    (void)remove(FILES "touch-done");
    (void)remove(FILES "touch-blocked");
    s_run = (int)getpid();
    FILE* run = fopen(FILES "touch-run", "w");
    bool written = run != nullptr && fprintf(run, "%d", s_run) > 0;
    CHECK(run != nullptr && fclose(run) == 0 && written, "the run's number written");
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    // Android makes the activity anew when the system's resources change,
    // and touches only a surface that has shown a frame.
    if (program->restored)
    {
        program->restored = false;
        Paint(program, context);
    }
    if (program->phase > LAST_PHASE)
    {
        return mwin_frameStop;
    }
    if (program->phase == 1 && Blocked())
    {
        program->blocked = true;
        s_failures += 1;
        return mwin_frameStop;
    }
    if (Ready(program))
    {
        printf("phase %d\n", program->phase);
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d after %d records:", program->phase, program->recordCount);
        for (int i = 0; i < program->recordCount; i++)
        {
            printf(" %d", (int)program->records[i].type);
        }
        printf("\n");
        s_failures += 1;
        return mwin_frameStop;
    }
    return mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->blocked || program->phase > LAST_PHASE, "every phase ran");
    CHECK(Flag("touch-done"), "the devices let go");
    printf("result: %d failures\n", s_failures);
}

mwinAppDef mwinAndroidMain(void)
{
    static Program program;
    // The runner reads the file with run-as.
    if (freopen(OUT_PATH, "w", stdout) != nullptr)
    {
        setvbuf(stdout, nullptr, _IONBF, 0);
    }
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    return def;
}
