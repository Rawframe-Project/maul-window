// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Keys, text and the on-screen keyboard on iOS, in the simulator
// (tools/run_ios_app.sh). The test hands the view's controller what a
// hardware keyboard would, its own presses: Shift+A, the left arrow,
// Return with the newline UIKit then types; it types into the view as
// the on-screen keyboard would, text with a carriage return and line
// feed and a tab, a deletion, and text with a lone surrogate; it asks
// for the on-screen keyboard for a number, and tells the view of the
// keyboard's frame as UIKit would, then hides it. The view is the first
// responder while its window shows, its input view empty unless the
// keyboard is asked for.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#import <UIKit/UIKit.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define MAX_RECORDS 32

@interface FakeKey : UIKey
{
  @public
    UIKeyboardHIDUsage code;
    NSString* typed;
    NSString* plain;
    UIKeyModifierFlags flags;
}
@end

@implementation FakeKey
- (UIKeyboardHIDUsage)keyCode
{
    return code;
}

- (NSString*)characters
{
    return typed;
}

- (NSString*)charactersIgnoringModifiers
{
    return plain;
}

- (UIKeyModifierFlags)modifierFlags
{
    return flags;
}
@end

@interface FakePress : UIPress
{
  @public
    FakeKey* fake;
}
@end

@implementation FakePress
- (UIKey*)key
{
    return fake;
}
@end

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    int completions;
    int count;
    mwinEvent records[MAX_RECORDS];
    // The first two texts, in order.
    char texts[2][32];
    int textCount;
    bool covered;
    mwinRect rect;
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
        program->completions += event.kind == mwin_eventRequestCompleted &&
                                event.data.completion.outcome == mwin_outcomeDone;
        // UIKit may say more of the keyboard: the test's own frame is
        // looked for.
        if (event.kind == mwin_eventVirtualKeyboardChanged && event.data.rect.height == 300.0f)
        {
            program->covered = true;
            program->rect = event.data.rect;
        }
        if (event.kind == mwin_eventTextInput && program->textCount < 2 &&
            event.data.text.length < sizeof(program->texts[0]))
        {
            memcpy(program->texts[program->textCount++], event.data.text.text,
                   event.data.text.length);
        }
        bool key = event.kind == mwin_eventKeyDown || event.kind == mwin_eventKeyUp ||
                   event.kind == mwin_eventTextInput;
        if (key && program->count < MAX_RECORDS)
        {
            program->records[program->count++] = event;
        }
    }
}

static UIView* ViewOf(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinWindow_GetNativeHandles(context, window, &handles) == mwin_success
               ? (UIView*)handles.handles.apple.view
               : nil;
}

// A press and release of a key, through the view's controller.
static void Press(UIView* view, UIKeyboardHIDUsage code, NSString* typed, NSString* plain,
                  UIKeyModifierFlags flags, NSString* text)
{
    FakeKey* key = [[[FakeKey alloc] init] autorelease];
    key->code = code;
    key->typed = typed;
    key->plain = plain;
    key->flags = flags;
    FakePress* press = [[[FakePress alloc] init] autorelease];
    press->fake = key;
    NSSet* presses = [NSSet setWithObject:press];
    UIResponder* controller = view.nextResponder;
    [controller pressesBegan:presses withEvent:nil];
    if (text != nil)
    {
        [(id<UIKeyInput>)view insertText:text];
    }
    [controller pressesEnded:presses withEvent:nil];
}

static void Type(UIView* view)
{
    Press(view, UIKeyboardHIDUsageKeyboardA, @"A", @"a", UIKeyModifierShift, nil);
    Press(view, UIKeyboardHIDUsageKeyboardLeftArrow, @"UIKeyInputLeftArrow", @"UIKeyInputLeftArrow",
          0, nil);
    Press(view, UIKeyboardHIDUsageKeyboardReturnOrEnter, @"\r", @"\r", 0, @"\n");
    [(id<UIKeyInput>)view insertText:@"héllo\r\n\t"];
    [(id<UIKeyInput>)view deleteBackward];
    [(id<UIKeyInput>)view
        insertText:[NSString stringWithCharacters:(const unichar[]){'x', 0xD800, 'y'} length:3]];
}

static bool KeyIs(const mwinEvent* event, mwinEventKind type, mwinKeyCode code, mwinKey key,
                  mwinModifiers modifiers)
{
    return event->kind == type && event->data.key.code == code && event->data.key.key == key &&
           event->data.key.modifiers == modifiers && !event->data.key.repeat;
}

static void CheckKeys(const Program* program, mwinContext* context)
{
    const mwinEvent* r = program->records;
    CHECK(program->count == 14, "fourteen key and text records");
    if (program->count != 14)
    {
        return;
    }
    CHECK(KeyIs(&r[0], mwin_eventKeyDown, 4, 'a', mwin_modShift) &&
              KeyIs(&r[1], mwin_eventKeyUp, 4, 'a', mwin_modShift),
          "Shift+A: the A key, meaning a");
    CHECK(KeyIs(&r[2], mwin_eventKeyDown, 80, MWIN_KEY_NAMED | 80, 0) &&
              KeyIs(&r[3], mwin_eventKeyUp, 80, MWIN_KEY_NAMED | 80, 0),
          "the left arrow, named");
    CHECK(KeyIs(&r[4], mwin_eventKeyDown, 40, MWIN_KEY_NAMED | 40, 0) &&
              KeyIs(&r[5], mwin_eventKeyUp, 40, MWIN_KEY_NAMED | 40, 0),
          "Return once: its newline gives no second press");
    CHECK(r[6].kind == mwin_eventTextInput && strcmp(program->texts[0], "h\xC3\xA9llo") == 0,
          "the text before the line break");
    CHECK(KeyIs(&r[7], mwin_eventKeyDown, 40, MWIN_KEY_NAMED | 40, 0) &&
              KeyIs(&r[8], mwin_eventKeyUp, 40, MWIN_KEY_NAMED | 40, 0),
          "the typed CR LF as one Enter");
    CHECK(KeyIs(&r[9], mwin_eventKeyDown, 43, MWIN_KEY_NAMED | 43, 0) &&
              KeyIs(&r[10], mwin_eventKeyUp, 43, MWIN_KEY_NAMED | 43, 0),
          "the typed tab as the Tab key");
    CHECK(KeyIs(&r[11], mwin_eventKeyDown, 42, MWIN_KEY_NAMED | 42, 0) &&
              KeyIs(&r[12], mwin_eventKeyUp, 42, MWIN_KEY_NAMED | 42, 0),
          "a deletion as Backspace");
    CHECK(r[13].kind == mwin_eventTextInput && strcmp(program->texts[1], "x\xEF\xBF\xBDy") == 0,
          "typed text with a lone surrogate, U+FFFD in its place");
    CHECK(mwinContext_MapKeyCode(context, 4) == 'a' && mwinContext_MapKeyCode(context, 5) == 0 &&
              mwinContext_MapKeyCode(context, 80) == (MWIN_KEY_NAMED | 80),
          "A known once pressed, B not yet, the arrow named");
}

// The keyboard's frame as UIKit tells it: the bottom 300 points.
static void KeyboardAt(UIView* view)
{
    CGRect screen = view.window.windowScene.screen.bounds;
    CGRect frame = CGRectMake(0.0, screen.size.height - 300.0, screen.size.width, 300.0);
    [[NSNotificationCenter defaultCenter]
        postNotificationName:UIKeyboardDidChangeFrameNotification
                      object:nil
                    userInfo:@{UIKeyboardFrameEndUserInfoKey : [NSValue valueWithCGRect:frame]}];
}

static bool Ready(const Program* program, UIView* view)
{
    switch (program->phase)
    {
    case 0:
        return program->shown && view != nil && view.isFirstResponder;
    case 1:
        return program->completions >= 2;
    case 2:
        return program->covered;
    default:
        return program->completions >= 1;
    }
}

// Keyboard records coalesce: the frame the test gives is read before
// the keyboard is hidden, whose frame UIKit then tells.
static void Advance(Program* program, mwinContext* context, UIView* view)
{
    mwinWindowId window = program->window;
    switch (program->phase)
    {
    case 0:
        CHECK(view.inputView != nil, "no on-screen keyboard unless asked for");
        Type(view);
        CHECK(mwinWindow_RequestTextInput(context, window, true,
                                          (mwinRect){10.0f, 20.0f, 1.0f, 16.0f},
                                          nullptr) == mwin_success &&
                  mwinWindow_RequestVirtualKeyboard(context, window, true, mwin_purposeNumber,
                                                    nullptr) == mwin_success,
              "text, and the keyboard for a number");
        break;
    case 1:
        CheckKeys(program, context);
        CHECK(view.inputView == nil &&
                  ((id<UITextInputTraits>)view).keyboardType == UIKeyboardTypeDecimalPad,
              "the keyboard asked for, for a number");
        KeyboardAt(view);
        break;
    case 2:
        CHECK(program->rect.y == (float)(view.bounds.size.height - 300.0) &&
                  program->rect.width == (float)view.bounds.size.width,
              "the part of the window the keyboard covers");
        CHECK(mwinWindow_RequestVirtualKeyboard(context, window, false, mwin_purposeText,
                                                nullptr) == mwin_success,
              "the keyboard hidden");
        break;
    default:
    {
        CHECK(view.inputView != nil, "no keyboard again");
        char layout[64];
        size_t length = 0;
        CHECK(mwinContext_GetKeyboardLayout(context, layout, sizeof(layout), &length) ==
                  mwin_success,
              "the input mode's language");
        break;
    }
    }
    program->phase += 1;
    program->completions = 0;
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
    UIView* view = ViewOf(context, program->window);
    if (Ready(program, view))
    {
        printf("phase %d\n", program->phase);
        @autoreleasepool
        {
            Advance(program, context, view);
        }
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d\n", program->phase);
        s_failures += 1;
        return mwin_frameStop;
    }
    return program->phase == 4 ? mwin_frameStop : mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->phase == 4, "every phase ran");
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
