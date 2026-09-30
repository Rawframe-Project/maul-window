// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// macOS drag and drop against AppKit (a CI runner's session): a drag
// that carries only an image refused; one that carries a file and text
// entering with both, a rest that has not moved posting nothing, a move,
// leaving; then entering again and dropped, with the file's path and the
// text read back. No drag can be made on the runner, so the test hands
// the view a drag of its own: a private pasteboard and a place, what the
// backend asks a drag for.

#include "test_harness.h"

#include "maul-window/drop.h"
#include "maul-window/event.h"
#include "maul-window/native.h"

#import <AppKit/AppKit.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 10000000000ull
#define MAX_RECORDS 8
#define TEXT        "Maul drop"

// A drag: its pasteboard and where it is in the window.
@interface FakeDrag : NSObject
{
  @public
    NSPasteboard* pasteboard;
    NSPoint location;
}
@end

@implementation FakeDrag
- (NSPasteboard*)draggingPasteboard
{
    return pasteboard;
}

- (NSPoint)draggingLocation
{
    return location;
}
@end

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    mwinEvent records[MAX_RECORDS];
    int count;
    char path[1024];
    bool timedOut;
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
        bool drag = event.type >= mwin_eventDragEntered && event.type <= mwin_eventDropped;
        if (drag && program->count < MAX_RECORDS)
        {
            program->records[program->count++] = event;
        }
    }
}

static bool Is(const Program* program, int index, mwinEventType type, float x, float y)
{
    const mwinEvent* event = &program->records[index];
    mwinPosition at =
        type == mwin_eventDropped ? event->data.drop.position : event->data.drag.position;
    bool contents =
        type == mwin_eventDropped || event->data.drag.contents == (mwin_dragFiles | mwin_dragText);
    return index < program->count && event->type == type && at.x == x && at.y == y && contents;
}

// Moves the drag to a place in the view, from its top left.
static id<NSDraggingInfo> At(FakeDrag* drag, NSView* view, CGFloat x, CGFloat y)
{
    drag->location = [view convertPoint:NSMakePoint(x, y) toView:nil];
    return (id<NSDraggingInfo>)drag;
}

// A file and text, each an item of the pasteboard.
static NSPasteboard* FileAndText(const char* path)
{
    NSPasteboard* pasteboard = [NSPasteboard pasteboardWithUniqueName];
    NSPasteboardItem* file = [[NSPasteboardItem alloc] init];
    NSPasteboardItem* text = [[NSPasteboardItem alloc] init];
    NSURL* address = [NSURL fileURLWithPath:@(path)];
    [file setString:address.absoluteString forType:NSPasteboardTypeFileURL];
    [text setString:@TEXT forType:NSPasteboardTypeString];
    [pasteboard clearContents];
    [pasteboard writeObjects:@[ file, text ]];
    [file release];
    [text release];
    return pasteboard;
}

static void Drag(Program* program, NSView<NSDraggingDestination>* view)
{
    FakeDrag* drag = [[FakeDrag alloc] init];
    drag->pasteboard = [NSPasteboard pasteboardWithUniqueName];
    [drag->pasteboard clearContents];
    [drag->pasteboard setData:[NSData dataWithBytes:"\x89PNG" length:4]
                      forType:NSPasteboardTypePNG];
    CHECK([view draggingEntered:At(drag, view, 20.0, 10.0)] == NSDragOperationNone,
          "a drag of only an image refused");
    [drag->pasteboard releaseGlobally];
    drag->pasteboard = FileAndText(program->path);
    CHECK([view draggingEntered:At(drag, view, 20.0, 10.0)] == NSDragOperationCopy &&
              [view draggingUpdated:At(drag, view, 20.0, 10.0)] == NSDragOperationCopy &&
              [view draggingUpdated:At(drag, view, 25.0, 15.0)] == NSDragOperationCopy,
          "a drag of a file and text taken as a copy");
    [view draggingExited:At(drag, view, 25.0, 15.0)];
    (void)[view draggingEntered:At(drag, view, 30.0, 20.0)];
    CHECK([view performDragOperation:At(drag, view, 30.0, 20.0)], "and dropped");
    [drag->pasteboard releaseGlobally];
    [drag release];
}

static void CheckDrop(const Program* program, mwinContext* context)
{
    CHECK(program->count == 5, "five records, and none for the refused drag or the rest");
    CHECK(Is(program, 0, mwin_eventDragEntered, 20.0f, 10.0f) &&
              Is(program, 1, mwin_eventDragMoved, 25.0f, 15.0f) &&
              Is(program, 2, mwin_eventDragLeft, 25.0f, 15.0f) &&
              Is(program, 3, mwin_eventDragEntered, 30.0f, 20.0f) &&
              Is(program, 4, mwin_eventDropped, 30.0f, 20.0f),
          "entered with files and text, moved, left, entered, dropped, each at its place");
    const mwinDropEvent* drop = &program->records[4].data.drop;
    CHECK(drop->fileCount == 1 && drop->textLength == sizeof(TEXT) - 1 && !drop->truncated,
          "a file and the text");
    char files[1100];
    char text[32];
    size_t length = 0;
    size_t textLength = 0;
    CHECK(mwinGetDroppedFiles(context, drop->drop, files, sizeof(files), &length) == mwin_success &&
              length == strlen(program->path) + 1 && strcmp(files, program->path) == 0,
          "the file's path");
    CHECK(mwinGetDroppedText(context, drop->drop, text, sizeof(text), &textLength) ==
                  mwin_success &&
              textLength == sizeof(TEXT) - 1 && memcmp(text, TEXT, textLength) == 0,
          "the text");
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul macOS drop";
    def.titleLength = 15;
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    mwinNativeHandles handles;
    if (program->shown && mwinGetNativeHandles(context, program->window, &handles) == mwin_success)
    {
        @autoreleasepool
        {
            if (program->phase == 0)
            {
                program->count = 0;
                Drag(program, (NSView<NSDraggingDestination>*)handles.handles.apple.view);
            }
            else
            {
                CheckDrop(program, context);
            }
        }
        program->phase += 1;
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        program->timedOut = true;
        return mwin_frameStop;
    }
    return program->phase == 2 ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    Program program = {0};
    setvbuf(stdout, nullptr, _IONBF, 0);
    @autoreleasepool
    {
        // A file that exists, which the pasteboard names by its URL.
        NSString* path = [NSTemporaryDirectory() stringByAppendingPathComponent:@"maul drop.txt"];
        [[NSData data] writeToFile:path atomically:NO];
        (void)strlcpy(program.path, path.UTF8String, sizeof(program.path));
    }
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on macOS");
    CHECK(!program.timedOut, "the window shows in time");
    CHECK(program.phase == 2, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
