// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drag and drop on iOS, in the simulator (tools/run_ios_app.sh). The
// test hands the view's drop interaction what UIKit would, a drop
// session of its own over real item providers: a file and a string. It
// enters, moves, leaves, enters again and drops; each record comes at
// its place, and the drop, once its items loaded, gives the file's copy
// with its contents and the text, the file under the name its source
// suggests. The file's extension is one the system does not know, whose
// provider can also give its address as a string: it is a file still. The simulator cannot drag
// itself.

#include "test_harness.h"

#include "maul-window/drop.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#import <UIKit/UIKit.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull

@interface FakeSession : NSObject <UIDropSession>
{
  @public
    NSArray<UIDragItem*>* dragged;
    CGPoint place;
}
@end

@implementation FakeSession
@synthesize progressIndicatorStyle;

- (NSArray<UIDragItem*>*)items
{
    return dragged;
}

- (CGPoint)locationInView:(UIView*)view
{
    (void)view;
    return place;
}

- (BOOL)allowsMoveOperation
{
    return NO;
}

- (BOOL)isRestrictedToDraggingApplication
{
    return NO;
}

- (BOOL)hasItemsConformingToTypeIdentifiers:(NSArray<NSString*>*)types
{
    (void)types;
    return YES;
}

- (BOOL)canLoadObjectsOfClass:(Class<NSItemProviderReading>)type
{
    (void)type;
    return YES;
}

- (id<UIDragSession>)localDragSession
{
    return nil;
}

- (NSProgress*)loadObjectsOfClass:(Class<NSItemProviderReading>)type
                       completion:(void (^)(NSArray<__kindof id<NSItemProviderReading>>*))completion
{
    (void)type;
    (void)completion;
    return nil;
}

- (NSProgress*)progress
{
    return nil;
}
@end

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    int count;
    mwinEvent records[8];
    FakeSession* session;
    id<UIDropInteractionDelegate> dropper;
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
        if (drag && program->count < 8)
        {
            program->records[program->count++] = event;
        }
    }
}

static UIView* ViewOf(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (UIView*)handles.handles.apple.view
               : nil;
}

static id<UIDropInteractionDelegate> DropperOf(UIView* view)
{
    for (id<UIInteraction> interaction in view.interactions)
    {
        if ([(id)interaction isKindOfClass:[UIDropInteraction class]])
        {
            return ((UIDropInteraction*)interaction).delegate;
        }
    }
    return nil;
}

// A session carrying a file of three bytes and a string.
static FakeSession* SessionOf(void)
{
    NSString* path = [NSTemporaryDirectory() stringByAppendingPathComponent:@"drop-test.dat"];
    [@"abc" writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nullptr];
    NSItemProvider* file =
        [[[NSItemProvider alloc] initWithContentsOfURL:[NSURL fileURLWithPath:path]] autorelease];
    // As the Files application names what it drags.
    file.suggestedName = @"drop-test.dat";
    NSItemProvider* text = [[[NSItemProvider alloc] initWithObject:@"dropped text"] autorelease];
    FakeSession* session = [[FakeSession alloc] init];
    session->dragged = [@[
        [[[UIDragItem alloc] initWithItemProvider:file] autorelease],
        [[[UIDragItem alloc] initWithItemProvider:text] autorelease]
    ] retain];
    return session;
}

static void Drag(Program* program)
{
    UIDropInteraction* none = nil;
    FakeSession* session = program->session;
    id<UIDropInteractionDelegate> dropper = program->dropper;
    CHECK([dropper dropInteraction:none canHandleSession:session], "text and a file taken");
    session->place = CGPointMake(10.0, 10.0);
    [dropper dropInteraction:none sessionDidEnter:session];
    session->place = CGPointMake(20.0, 30.0);
    UIDropProposal* proposal = [dropper dropInteraction:none sessionDidUpdate:session];
    CHECK(proposal.operation == UIDropOperationCopy, "copied");
    [dropper dropInteraction:none sessionDidExit:session];
    [dropper dropInteraction:none sessionDidEnter:session];
    session->place = CGPointMake(40.0, 50.0);
    [dropper dropInteraction:none performDrop:session];
}

static bool DragIs(const mwinEvent* event, mwinEventType type, float x, float y)
{
    return event->type == type && event->data.drag.position.x == x &&
           event->data.drag.position.y == y &&
           event->data.drag.contents == (mwin_dragFiles | mwin_dragText);
}

static void CheckDrop(const Program* program, mwinContext* context)
{
    const mwinEvent* r = program->records;
    CHECK(program->count == 5, "five records");
    if (program->count != 5)
    {
        return;
    }
    CHECK(DragIs(&r[0], mwin_eventDragEntered, 10.0f, 10.0f) &&
              DragIs(&r[1], mwin_eventDragMoved, 20.0f, 30.0f) &&
              DragIs(&r[2], mwin_eventDragLeft, 20.0f, 30.0f) &&
              DragIs(&r[3], mwin_eventDragEntered, 20.0f, 30.0f),
          "entered, moved, left, entered again, each at its place");
    const mwinDropEvent* drop = &r[4].data.drop;
    CHECK(r[4].type == mwin_eventDropped && drop->position.x == 40.0f &&
              drop->position.y == 50.0f && drop->fileCount == 1 && drop->textLength == 12,
          "dropped where it was let go, a file and text");
    char files[1024];
    size_t length = 0;
    CHECK(mwinGetDroppedFiles(context, drop->drop, files, sizeof(files), &length) == mwin_success &&
              length > 14 && strcmp(files + length - 14, "drop-test.dat") == 0,
          "the file's copy, by its name");
    NSString* copied = [NSString stringWithContentsOfFile:@(files)
                                                 encoding:NSUTF8StringEncoding
                                                    error:nullptr];
    CHECK([copied isEqual:@"abc"], "with its contents");
    char text[32];
    CHECK(mwinGetDroppedText(context, drop->drop, text, sizeof(text), &length) == mwin_success &&
              length == 12 && memcmp(text, "dropped text", 12) == 0,
          "the text");
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
    UIView* view = ViewOf(context, program->window);
    if (program->phase == 0 && program->shown && view != nil)
    {
        printf("phase 0\n");
        program->dropper = DropperOf(view);
        CHECK(program->dropper != nil, "a drop interaction on the view");
        program->session = SessionOf();
        if (program->dropper != nil)
        {
            @autoreleasepool
            {
                Drag(program);
            }
        }
        program->phase = 1;
    }
    else if (program->phase == 1 && program->count >= 5)
    {
        printf("phase 1\n");
        CheckDrop(program, context);
        program->phase = 2;
        return mwin_frameStop;
    }
    if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d, %d records\n", program->phase, program->count);
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
    CHECK(program->phase == 2, "every phase ran");
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
