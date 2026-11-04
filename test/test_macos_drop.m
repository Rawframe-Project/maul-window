// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// macOS drag and drop against AppKit (a CI runner's session): a drag
// that carries only an image refused; one that carries a file and text
// entering with both, a rest that has not moved posting nothing, a move,
// leaving; then entering again and dropped, with the file's path and the
// text read back, a U+0000 within it kept. Then a drop of text with a lone
// surrogate, from a pasteboard of the test's that answers what the
// backend asks: the text arrives with U+FFFD in its place, not truncated. Then a drop of a file,
// two texts and an image: the file and the first text, the second text and the image left out
// and the drop marked truncated. No drag can be made on the runner, so the test hands the view a
// drag of its own: a private pasteboard and a place, what the backend asks a drag for.

#include "test_harness.h"

#include "maul-window/drop.h"
#include "maul-window/event.h"
#include "maul-window/native.h"

#import <AppKit/AppKit.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 10000000000ull
#define MAX_RECORDS 8
// Text with a U+0000 within it, which ended it when read as UTF-8.
#define TEXT "Maul\0drop"

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

// A pasteboard of only text, which no real one stores as it is: "a", a
// lone surrogate, "b". It answers the messages the backend sends, as its
// own one item.
@interface FakeTextPasteboard : NSObject
@end

@implementation FakeTextPasteboard
- (NSArray*)pasteboardItems
{
    return @[ self ];
}

- (NSPasteboardType)availableTypeFromArray:(NSArray<NSPasteboardType>*)types
{
    return [types containsObject:NSPasteboardTypeString] ? NSPasteboardTypeString : nil;
}

- (NSString*)stringForType:(NSPasteboardType)type
{
    static const unichar units[] = {'a', 0xD800, 'b'};
    return [type isEqualToString:NSPasteboardTypeString]
               ? [NSString stringWithCharacters:units length:3]
               : nil;
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
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.kind == mwin_eventShown;
        bool drag = event.kind >= mwin_eventDragEntered && event.kind <= mwin_eventDropped;
        if (drag && program->count < MAX_RECORDS)
        {
            program->records[program->count++] = event;
        }
    }
}

static bool Is(const Program* program, int index, mwinEventKind type, float x, float y)
{
    const mwinEvent* event = &program->records[index];
    mwinPosition at =
        type == mwin_eventDropped ? event->data.drop.position : event->data.drag.position;
    bool contents =
        type == mwin_eventDropped || event->data.drag.contents == (mwin_dragFiles | mwin_dragText);
    return index < program->count && event->kind == type && at.x == x && at.y == y && contents;
}

// Moves the drag to a place in the view, from its top left.
static id<NSDraggingInfo> At(FakeDrag* drag, NSView* view, CGFloat x, CGFloat y)
{
    drag->location = [view convertPoint:NSMakePoint(x, y) toView:nil];
    return (id<NSDraggingInfo>)drag;
}

// A file and text, each an item of the pasteboard; with more, a second
// text and an image after them.
static NSPasteboard* FileAndText(const char* path, bool more)
{
    NSPasteboard* pasteboard = [NSPasteboard pasteboardWithUniqueName];
    NSPasteboardItem* file = [[[NSPasteboardItem alloc] init] autorelease];
    NSPasteboardItem* text = [[[NSPasteboardItem alloc] init] autorelease];
    NSPasteboardItem* second = [[[NSPasteboardItem alloc] init] autorelease];
    NSPasteboardItem* image = [[[NSPasteboardItem alloc] init] autorelease];
    NSURL* address = [NSURL fileURLWithPath:@(path)];
    [file setString:address.absoluteString forType:NSPasteboardTypeFileURL];
    NSString* string = [[NSString alloc] initWithBytes:TEXT
                                                length:sizeof(TEXT) - 1
                                              encoding:NSUTF8StringEncoding];
    [text setString:string forType:NSPasteboardTypeString];
    [string release];
    [second setString:@"more text" forType:NSPasteboardTypeString];
    [image setData:[NSData dataWithBytes:"\x89PNG" length:4] forType:NSPasteboardTypePNG];
    [pasteboard clearContents];
    [pasteboard writeObjects:more ? @[ file, text, second, image ] : @[ file, text ]];
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
    drag->pasteboard = FileAndText(program->path, false);
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
    CHECK(mwinDrop_GetFiles(context, drop->drop, files, sizeof(files), &length) == mwin_success &&
              length == strlen(program->path) + 1 && strcmp(files, program->path) == 0,
          "the file's path");
    CHECK(mwinDrop_GetText(context, drop->drop, text, sizeof(text), &textLength) == mwin_success &&
              textLength == sizeof(TEXT) - 1 && memcmp(text, TEXT, textLength) == 0,
          "the text");
}

// Drops the text with a lone surrogate.
static void DropSurrogate(NSView<NSDraggingDestination>* view)
{
    FakeDrag* drag = [[FakeDrag alloc] init];
    FakeTextPasteboard* pasteboard = [[FakeTextPasteboard alloc] init];
    drag->pasteboard = (NSPasteboard*)pasteboard;
    CHECK([view draggingEntered:At(drag, view, 10.0, 10.0)] == NSDragOperationCopy &&
              [view performDragOperation:At(drag, view, 10.0, 10.0)],
          "a drag of text with a lone surrogate taken and dropped");
    [pasteboard release];
    [drag release];
}

static void CheckSurrogate(const Program* program, mwinContext* context)
{
    const mwinDropEvent* drop = program->count == 2 && program->records[1].kind == mwin_eventDropped
                                    ? &program->records[1].data.drop
                                    : nullptr;
    char text[16];
    size_t length = 0;
    CHECK(drop != nullptr && drop->fileCount == 0 && drop->textLength == 5 && !drop->truncated &&
              mwinDrop_GetText(context, drop->drop, text, sizeof(text), &length) == mwin_success &&
              length == 5 &&
              memcmp(text,
                     "a\xEF\xBF\xBD"
                     "b",
                     5) == 0,
          "the text with U+FFFD for the lone surrogate, not truncated");
}

// Drops a file, two texts and an image.
static void DropMore(const Program* program, NSView<NSDraggingDestination>* view)
{
    FakeDrag* drag = [[FakeDrag alloc] init];
    drag->pasteboard = FileAndText(program->path, true);
    CHECK([view draggingEntered:At(drag, view, 10.0, 10.0)] == NSDragOperationCopy &&
              [view performDragOperation:At(drag, view, 10.0, 10.0)],
          "a drag of a file, two texts and an image taken and dropped");
    [drag->pasteboard releaseGlobally];
    [drag release];
}

static void CheckMore(const Program* program, mwinContext* context)
{
    const mwinDropEvent* drop = program->count == 2 && program->records[1].kind == mwin_eventDropped
                                    ? &program->records[1].data.drop
                                    : nullptr;
    char text[32];
    size_t length = 0;
    CHECK(drop != nullptr && drop->fileCount == 1 && drop->textLength == sizeof(TEXT) - 1 &&
              drop->truncated &&
              mwinDrop_GetText(context, drop->drop, text, sizeof(text), &length) == mwin_success &&
              length == sizeof(TEXT) - 1 && memcmp(text, TEXT, length) == 0,
          "the file and the first text, the second text and the image left out and the drop "
          "marked truncated");
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
    if (program->shown &&
        mwinWindow_GetNativeHandles(context, program->window, &handles) == mwin_success)
    {
        @autoreleasepool
        {
            if (program->phase == 0)
            {
                program->count = 0;
                Drag(program, (NSView<NSDraggingDestination>*)handles.handles.apple.view);
            }
            else if (program->phase == 1)
            {
                CheckDrop(program, context);
                program->count = 0;
                DropSurrogate((NSView<NSDraggingDestination>*)handles.handles.apple.view);
            }
            else if (program->phase == 2)
            {
                CheckSurrogate(program, context);
                program->count = 0;
                DropMore(program, (NSView<NSDraggingDestination>*)handles.handles.apple.view);
            }
            else
            {
                CheckMore(program, context);
            }
        }
        program->phase += 1;
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        program->timedOut = true;
        return mwin_frameStop;
    }
    return program->phase == 4 ? mwin_frameStop : mwin_frameContinue;
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
    CHECK(program.phase == 4, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
