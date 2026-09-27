// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A program for contract tests: its frame function runs one step per
// frame of a test, and the test backend pumps between frames.

#ifndef MAUL_WINDOW_TEST_PROGRAM_H
#define MAUL_WINDOW_TEST_PROGRAM_H

#include "test_harness.h"

#include "maul-window/test.h"

#define MAX_EVENTS 64

typedef struct Program Program;
typedef void StepFn(Program* program, mwinContext* context, int step);

struct Program
{
    StepFn* step;
    int frame;
    bool done;
    mwinWindowId windows[16];
    mwinRequestId requests[40];
    mwinEvent events[MAX_EVENTS];
    int eventCount;
    mwinResult initStatus;
    mwinResult quitStatus;
    int quitCalls;
};

static inline mwinResult Init(mwinContext* context, void* user)
{
    (void)context;
    return ((Program*)user)->initStatus;
}

static inline mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    program->step(program, context, program->frame++);
    return program->done || program->frame > 50 ? mwin_frameStop : mwin_frameContinue;
}

static inline void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    program->quitStatus = status;
    program->quitCalls += 1;
}

// Runs a program on the test backend with a context def.
static inline mwinResult RunWith(Program* program, mwinContextDef contextDef)
{
    mwinAppDef def = mwinDefaultAppDef();
    def.context = contextDef;
    def.context.backend = mwin_backendTest;
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = program;
    return mwinRun(&def);
}

static inline mwinResult Run(Program* program)
{
    return RunWith(program, mwinDefaultContextDef());
}

// Drains the stream into the program's list.
static inline void Drain(Program* program, mwinContext* context)
{
    program->eventCount = 0;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->eventCount < MAX_EVENTS)
    {
        program->events[program->eventCount++] = event;
    }
}

static inline bool SameId(mwinRequestId a, mwinRequestId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static inline bool SameWindow(mwinWindowId a, mwinWindowId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// Whether the drained records have exactly these types, in order.
static inline bool Types(const Program* program, const mwinEventType* types, int count)
{
    if (program->eventCount != count)
    {
        return false;
    }
    for (int i = 0; i < count; i++)
    {
        if (program->events[i].type != types[i])
        {
            return false;
        }
    }
    return true;
}

static inline mwinWindowId Create(mwinContext* context, mwinRequestId* requestOut)
{
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul";
    def.titleLength = 4;
    def.size = (mwinSize){640.0f, 480.0f};
    mwinWindowId window = {0};
    CHECK(mwinCreateWindow(context, &def, &window, requestOut) == mwin_success, "create");
    return window;
}

#endif // MAUL_WINDOW_TEST_PROGRAM_H
