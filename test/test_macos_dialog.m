// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// macOS file dialogs and the message box against AppKit (a CI runner's
// session). A message box before any context, answered No and then
// Yes; one shown inside a frame, while frames stay out of it. A save
// panel as a sheet with its title, folder, name and filters, its menu
// changing the allowed types, confirmed, and the path read back; an
// open panel cancelled; a folder panel whose window is destroyed,
// closed with it, its request cancelled. No one can click on the runner, so the test works the
// panels and ends the boxes as AppKit lets a program.

#include "test_harness.h"

#include "maul-window/dialog.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/services.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS 10000000000ull

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    NSWindow* host;
    bool shown;
    bool destroyed;
    int depth;
    int deepest;
    bool completed;
    mwinOutcome outcome;
    mwinRequestId request;
    NSSavePanel* closed;
    char name[64];
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// Shows a message box that a timer in the modal loop answers.
static bool Answered(NSModalResponse answer, mwinMessageButtons buttons)
{
    NSTimer* timer = [NSTimer timerWithTimeInterval:0.3
                                            repeats:NO
                                              block:^(NSTimer* fired) {
                                                (void)fired;
                                                [NSApp stopModalWithCode:answer];
                                              }];
    [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSModalPanelRunLoopMode];
    mwinMessageBoxDef def = mwinDefaultMessageBoxDef();
    def.title = "Maul";
    def.titleLength = 4;
    def.message = "Go on?";
    def.messageLength = 6;
    def.kind = mwin_messageWarning;
    def.buttons = buttons;
    bool accepted = true;
    CHECK(mwinShowMessageBox(&def, &accepted) == mwin_success, "a message box shows");
    return accepted;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.type == mwin_eventShown;
        program->destroyed |= event.type == mwin_eventWindowDestroyed;
        if (event.type == mwin_eventRequestCompleted &&
            event.data.completion.kind == mwin_requestFileDialog)
        {
            program->completed = true;
            program->outcome = event.data.completion.outcome;
        }
    }
}

static mwinResult Ask(Program* program, mwinContext* context, mwinDialogKind kind)
{
    static const mwinFileFilter filters[] = {{"Text", 4, "txt;md", 6}, {"Images", 6, "png", 3}};
    mwinFileDialogDef def = mwinDefaultFileDialogDef();
    def.kind = kind;
    def.title = "Save it";
    def.titleLength = 7;
    def.folder = "/tmp";
    def.folderLength = 4;
    def.name = program->name;
    def.nameLength = strlen(program->name);
    def.filters = filters;
    def.filterCount = kind == mwin_dialogSave ? 2 : 0;
    program->completed = false;
    return mwinRequestFileDialog(context, program->window, &def, &program->request);
}

static NSArray* TypesOf(NSArray<NSString*>* extensions)
{
    NSMutableArray* types = [NSMutableArray array];
    for (NSString* extension in extensions)
    {
        [types addObject:[UTType typeWithFilenameExtension:extension]];
    }
    return types;
}

static void CheckSheet(NSWindow* host)
{
    NSSavePanel* panel = (NSSavePanel*)host.attachedSheet;
    NSPopUpButton* menu = (NSPopUpButton*)panel.accessoryView;
    CHECK([panel isKindOfClass:[NSSavePanel class]] && [panel.message isEqual:@"Save it"],
          "a save panel as a sheet, with its title");
    CHECK([menu isKindOfClass:[NSPopUpButton class]] && menu.numberOfItems == 2 &&
              [[menu itemTitleAtIndex:1] isEqual:@"Images"],
          "the filters' menu");
    CHECK([panel.allowedContentTypes isEqual:TypesOf(@[ @"txt", @"md" ])],
          "the first filter's types at first");
    [menu selectItemAtIndex:1];
    [menu sendAction:menu.action to:menu.target];
    CHECK([panel.allowedContentTypes isEqual:TypesOf(@[ @"png" ])], "the menu changes them");
    [menu selectItemAtIndex:0];
    [menu sendAction:menu.action to:menu.target];
    // The panel is another process's, whose own buttons raise when sent
    // from here; its window ends it as a click would, after the frame.
    dispatch_async(dispatch_get_main_queue(), ^{
      [host endSheet:panel returnCode:NSModalResponseOK];
    });
}

static void CheckSaved(const Program* program, mwinContext* context)
{
    char paths[1024];
    size_t length = 0;
    uint32_t count = 0;
    size_t name = strlen(program->name);
    CHECK(program->outcome == mwin_outcomeDone &&
              mwinGetDialogFiles(context, program->request, paths, sizeof(paths), &length,
                                 &count) == mwin_success &&
              count == 1 && length > name + 1 &&
              memcmp(paths + length - name - 1, program->name, name) == 0 && paths[0] == '/',
          "the save panel's path, in the folder asked for");
}

// Each phase acts once what it waits for came.
static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case 0:
        return program->shown;
    case 1:
    case 3:
    case 5:
        return program->host.attachedSheet != nil;
    case 2:
    case 4:
        return program->completed;
    default:
        return program->destroyed;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
        CHECK(Answered(NSAlertFirstButtonReturn, mwin_buttonsOkCancel),
              "inside a frame too, with no frame inside it");
        CHECK(Ask(program, context, mwin_dialogSave) == mwin_success, "a save dialog");
        break;
    case 1:
        CheckSheet(program->host);
        break;
    case 2:
        CheckSaved(program, context);
        CHECK(Ask(program, context, mwin_dialogOpen) == mwin_success, "an open dialog");
        break;
    case 3:
    {
        NSWindow* host = program->host;
        NSWindow* panel = host.attachedSheet;
        dispatch_async(dispatch_get_main_queue(), ^{
          [host endSheet:panel returnCode:NSModalResponseCancel];
        });
        break;
    }
    case 4:
        CHECK(program->outcome == mwin_outcomeCancelled, "cancelled");
        CHECK(Ask(program, context, mwin_dialogFolder) == mwin_success, "a folder dialog");
        break;
    case 5:
        program->closed = [(NSSavePanel*)program->host.attachedSheet retain];
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success, "its window destroyed");
        break;
    default:
        CHECK(!program->closed.visible && program->completed &&
                  program->outcome == mwin_outcomeCancelled,
              "the panel closed, its request cancelled with the window");
        [program->closed release];
        break;
    }
    program->phase += 1;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul macOS dialogs";
    def.titleLength = 18;
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    program->depth += 1;
    program->deepest = program->depth > program->deepest ? program->depth : program->deepest;
    Collect(program, context);
    mwinNativeHandles handles;
    if (program->host == nil &&
        mwinGetNativeHandles(context, program->window, &handles) == mwin_success)
    {
        program->host = ((NSView*)handles.handles.apple.view).window;
    }
    if (Ready(program))
    {
        @autoreleasepool
        {
            Advance(program, context);
        }
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        program->timedOut = true;
        printf("timed out in phase %d\n", program->phase);
        program->depth -= 1;
        return mwin_frameStop;
    }
    program->depth -= 1;
    return program->phase == 7 ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    Program program = {0};
    setvbuf(stdout, nullptr, _IONBF, 0);
    (void)snprintf(program.name, sizeof(program.name), "maul-save-%d.txt", (int)getpid());
    @autoreleasepool
    {
        CHECK(!Answered(NSAlertSecondButtonReturn, mwin_buttonsYesNo), "No, before any context");
        CHECK(Answered(NSAlertFirstButtonReturn, mwin_buttonsYesNo), "Yes");
    }
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on macOS");
    CHECK(!program.timedOut, "every phase comes in time");
    CHECK(program.phase == 7, "every phase ran");
    CHECK(program.deepest == 1, "no frame inside a frame");
    return s_failures == 0 ? 0 : 1;
}
