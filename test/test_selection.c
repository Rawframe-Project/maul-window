// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reads of the clipboard and the primary selection that the X11 and
// Wayland backends run, on every host: a read's type copied with its
// NUL, a waiting read started from any request slot and completed when
// its start answers at once, and a finished read answering its request.

#include "selection_reads.h"
#include "test_program.h"

#include <string.h>

static void TestBegin(void)
{
    mwinRequest request = {0};
    request.kind = mwin_requestClipboardReadData;
    request.value.text.bytes = (char*)"image/png";
    request.value.text.length = 9;
    mwinSelectionRead read;
    memset(&read, 'x', sizeof(read));
    mwinBeginSelectionRead(&read, &request);
    CHECK(read.kind == mwin_requestClipboardReadData && read.mimeLength == 9 &&
              strcmp(read.mime, "image/png") == 0,
          "the type, ended by its NUL");
}

// A start that answers at once, counting its calls.
static int StartDone(void* backend, const mwinRequest* request)
{
    (void)request;
    *(int*)backend += 1;
    return mwin_outcomeDone;
}

static void ReadStep(Program* program, mwinContext* context, int step)
{
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step != 1)
    {
        program->done = true;
        return;
    }
    // Held, the platform answers nothing; the backend's loop starts the
    // reads, here the first request slot's.
    CHECK(mwinTestHold(context, true) == mwin_success &&
              mwinRequestPrimaryRead(context, program->windows[0], &program->requests[0]) ==
                  mwin_success &&
              program->requests[0].index1 == 1,
          "a read in the first request slot");
    int starts = 0;
    mwinStartWaitingReads(context, StartDone, &starts);
    Drain(program, context);
    CHECK(starts == 1 && program->eventCount == 1 &&
              SameId(program->events[0].data.completion.request, program->requests[0]) &&
              program->events[0].data.completion.outcome == mwin_outcomeDone,
          "started, and done at once");
    CHECK(mwinRequestClipboardReadData(context, program->windows[0], "IMAGE/PNG", 9,
                                       &program->requests[1]) == mwin_success,
          "a data read");
    mwinRequest asked = {0};
    asked.kind = mwin_requestClipboardReadData;
    asked.value.text.bytes = (char*)"image/png";
    asked.value.text.length = 9;
    mwinSelectionRead read;
    mwinBeginSelectionRead(&read, &asked);
    mwinFinishSelectionRead(context, &read, mwin_outcomeFailed);
    Drain(program, context);
    CHECK(program->eventCount == 1 &&
              SameId(program->events[0].data.completion.request, program->requests[1]) &&
              program->events[0].data.completion.outcome == mwin_outcomeFailed,
          "a finished read answers the request for its type, whatever the case");
    CHECK(mwinTestHold(context, false) == mwin_success, "release");
}

static void TestStartAndFinish(void)
{
    Program program = {.step = ReadStep};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
}

int main(void)
{
    TestBegin();
    TestStartAndFinish();
    return s_failures == 0 ? 0 : 1;
}
