// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// iOS file dialogs, in the simulator (tools/run_ios_app.sh). The test
// finds the document picker shown over the window and tells its delegate
// what the picker would, the user's choice being out of its reach: a
// file chosen to open, of the filters' types, read back; a pick of
// several cancelled; a save, whose picker exports an empty file of the
// offered name, answered with the place chosen; and a folder's picker
// taken away when its window is destroyed. Each is answered once its
// presentation completed, and the next asked for once it is gone.
// Frames go on meanwhile.

#include "test_harness.h"

#include "maul-window/dialog.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#import <UIKit/UIKit.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    UIView* view;
    bool shown;
    int completions;
    mwinOutcome outcome;
    mwinRequestId request;
    UIDocumentPickerViewController* folderPicker;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.type == mwin_eventShown;
        if (event.type == mwin_eventRequestCompleted)
        {
            program->completions += 1;
            program->outcome = event.data.completion.outcome;
        }
    }
}

// The picker shown over the window's view, or nil.
static UIDocumentPickerViewController* PickerOver(UIView* view)
{
    UIViewController* top = view.window.rootViewController;
    while (top.presentedViewController != nil)
    {
        top = top.presentedViewController;
    }
    return [top isKindOfClass:[UIDocumentPickerViewController class]]
               ? (UIDocumentPickerViewController*)top
               : nil;
}

static NSURL* FileAt(NSString* name)
{
    NSString* path = [NSTemporaryDirectory() stringByAppendingPathComponent:name];
    [@"picked" writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nullptr];
    return [NSURL fileURLWithPath:path];
}

static bool Ask(Program* program, mwinContext* context, mwinDialogKind kind)
{
    static const mwinFileFilter filters[] = {{"Images", 6, "png;jpg", 7}, {"Text", 4, "txt", 3}};
    mwinFileDialogDef def = mwinDefaultFileDialogDef();
    def.kind = kind;
    def.filters = filters;
    def.filterCount = 2;
    def.name = "report.txt";
    def.nameLength = 10;
    program->completions = 0;
    return mwinRequestFileDialog(context, program->window, &def, &program->request) == mwin_success;
}

static bool PathIs(const Program* program, mwinContext* context, const char* expected)
{
    char paths[1024];
    size_t length = 0;
    uint32_t count = 0;
    return mwinGetDialogFiles(context, program->request, paths, sizeof(paths), &length, &count) ==
               mwin_success &&
           count == 1 && strcmp(paths, expected) == 0;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case 0:
        return program->shown && program->view != nil;
    case 1:
    case 3:
    case 5:
    case 7:
    {
        // A picker shows once its presentation completed.
        UIDocumentPickerViewController* picker = PickerOver(program->view);
        return picker != nil && !picker.isBeingPresented;
    }
    case 2:
    case 4:
    case 6:
        // Answered, and the picker gone.
        return program->completions >= 1 && PickerOver(program->view) == nil;
    default:
        return program->folderPicker.presentingViewController == nil;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    UIDocumentPickerViewController* picker = PickerOver(program->view);
    switch (program->phase)
    {
    case 0:
        CHECK(Ask(program, context, mwin_dialogOpen), "a file to open");
        break;
    case 1:
    {
        CHECK(!picker.allowsMultipleSelection, "one file");
        NSURL* file = FileAt(@"picked.png");
        [picker.delegate documentPicker:picker didPickDocumentsAtURLs:@[ file ]];
        break;
    }
    case 2:
        CHECK(program->outcome == mwin_outcomeDone &&
                  PathIs(program, context,
                         [NSTemporaryDirectory() stringByAppendingPathComponent:@"picked.png"]
                             .UTF8String),
              "the file chosen");
        CHECK(Ask(program, context, mwin_dialogOpenMany), "files to open");
        break;
    case 3:
        CHECK(picker.allowsMultipleSelection, "several files");
        [picker.delegate documentPickerWasCancelled:picker];
        break;
    case 4:
        CHECK(program->outcome == mwin_outcomeCancelled, "cancelled");
        CHECK(Ask(program, context, mwin_dialogSave), "a file to save");
        break;
    case 5:
    {
        NSURL* place = FileAt(@"report.txt");
        [picker.delegate documentPicker:picker didPickDocumentsAtURLs:@[ place ]];
        break;
    }
    case 6:
    {
        NSString* place = [NSTemporaryDirectory() stringByAppendingPathComponent:@"report.txt"];
        CHECK(program->outcome == mwin_outcomeDone && PathIs(program, context, place.UTF8String),
              "the place chosen to save to");
        CHECK(Ask(program, context, mwin_dialogFolder), "a folder");
        break;
    }
    case 7:
        program->folderPicker = [picker retain];
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success, "its window destroyed");
        break;
    default:
        break;
    }
    program->phase += 1;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    mwinNativeHandles handles;
    if (program->view == nil &&
        mwinGetNativeHandles(context, program->window, &handles) == mwin_success)
    {
        // Held: the test reads it after its window is gone.
        program->view = [(UIView*)handles.handles.apple.view retain];
    }
    if (Ready(program))
    {
        printf("phase %d\n", program->phase);
        @autoreleasepool
        {
            Advance(program, context);
        }
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d\n", program->phase);
        s_failures += 1;
        return mwin_frameStop;
    }
    return program->phase == 9 ? mwin_frameStop : mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->phase == 9, "every phase ran, the folder's picker gone with its window");
    printf("result: %d failures\n", s_failures);
}

int main(void)
{
    static Program program;
    // The runner names the file to write to (tools/run_ios_app.sh).
    const char* out = getenv("MWIN_TEST_OUT");
    if (out != nullptr && freopen(out, "w", stdout) == nullptr)
    {
        return 1;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    // UIKit keeps the thread: this returns only if the program never ran.
    mwinResult result = mwinRun(&def);
    printf("result: mwinRun returned %d\n", (int)result);
    return 1;
}
