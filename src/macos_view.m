// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The content view of a macOS window (macos.h): flipped, so its
// coordinates run down from the top left as the contract's do, backed
// by a CAMetalLayer for a GPU layer to present to, and the responder its
// window's keyboard, mouse and wheel input comes to. Keys come by
// virtual key code; text comes through the view's text input client,
// with Command held no key types text. Quick clicks are AppKit's click
// counts. Precise scrolling (touchpads, Magic Mouse) comes in points,
// ten to a detent; a wheel's in lines, one to a detent. The sign is what
// the user's scrolling direction makes it, as on the other platforms.

#include "macos.h"

#include <string.h>

// Precise scrolling's points per detent.
#define POINTS_PER_DETENT 10.0

@interface MwinMacView : NSView <NSTextInputClient>
{
  @public
    mwinMacPlatform* platform;
    uint32_t slot;
}
@end

static mwinMacWindow* WindowOf(const MwinMacView* view)
{
    return &view->platform->windows[view->slot];
}

static void Post(const MwinMacView* view, mwinEvent* event)
{
    event->timeNs = mwinMacNow();
    mwinPost(view->platform->context, view->slot, event);
}

static void PostKey(const MwinMacView* view, mwinEventType type, NSEvent* event, mwinKeyCode code)
{
    mwinEvent record = {.type = type};
    record.data.key = (mwinKeyEvent){code, mwinMacModifiersOf(event.modifierFlags),
                                     mwinMacMeaningOf(view->platform, event.keyCode, code),
                                     // AppKit raises for a modifier event's repeat.
                                     event.type == NSEventTypeKeyDown && event.ARepeat};
    Post(view, &record);
}

// A modifier key went down or up, or Caps Lock toggled, which posts a
// press and a release.
static void OnFlags(const MwinMacView* view, NSEvent* event)
{
    mwinKeyCode code = mwinMacCodeOf(event.keyCode);
    int change = mwinMacModifierChange(code, event.modifierFlags);
    if (change < 0)
    {
        return;
    }
    if (change != 0)
    {
        PostKey(view, mwin_eventKeyDown, event, code);
    }
    if (change != 1)
    {
        PostKey(view, mwin_eventKeyUp, event, code);
    }
}

// Typed text without control characters or AppKit's function key
// characters, which are keys.
static void PostText(const MwinMacView* view, NSString* string)
{
    NSMutableString* kept = [NSMutableString stringWithCapacity:string.length];
    for (NSUInteger i = 0; i < string.length; i++)
    {
        unichar unit = [string characterAtIndex:i];
        if (unit >= 0x20 && unit != 0x7F && (unit < 0xF700 || unit > 0xF8FF))
        {
            CFStringAppendCharacters((CFMutableStringRef)kept, &unit, 1);
        }
    }
    const char* bytes = kept.UTF8String;
    size_t length = bytes != nullptr ? strlen(bytes) : 0;
    if (length == 0 || length > UINT32_MAX)
    {
        return;
    }
    mwinEvent event = {.type = mwin_eventTextInput};
    event.data.text = (mwinTextEvent){bytes, (uint32_t)length};
    Post(view, &event);
}

static mwinMouseButton ButtonOf(NSEvent* event)
{
    switch (event.type)
    {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
        return mwin_buttonLeft;
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
        return mwin_buttonRight;
    default:
        break;
    }
    switch (event.buttonNumber)
    {
    case 2:
        return mwin_buttonMiddle;
    case 3:
        return mwin_buttonBack;
    case 4:
        return mwin_buttonForward;
    default:
        return 0;
    }
}

static void PostPointer(const MwinMacView* view, mwinEventType type, NSEvent* event,
                        mwinMouseButton button, uint8_t clicks)
{
    NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
    mwinEvent record = {.type = type};
    record.data.pointer = (mwinPointerEvent){{(float)point.x, (float)point.y},
                                             mwinMacModifiersOf(event.modifierFlags),
                                             WindowOf(view)->buttons,
                                             button,
                                             clicks};
    Post(view, &record);
}

static void OnButton(const MwinMacView* view, NSEvent* event, bool down)
{
    mwinMouseButton button = ButtonOf(event);
    if (button == 0)
    {
        return;
    }
    mwinMacWindow* window = WindowOf(view);
    uint8_t bit = (uint8_t)(1u << (button - 1));
    window->buttons = down ? (uint8_t)(window->buttons | bit) : (uint8_t)(window->buttons & ~bit);
    NSInteger clicks = event.clickCount;
    PostPointer(view, down ? mwin_eventButtonDown : mwin_eventButtonUp, event, button,
                (uint8_t)(clicks < 0           ? 0
                          : clicks > UINT8_MAX ? UINT8_MAX
                                               : clicks));
}

static void OnMove(const MwinMacView* view, NSEvent* event)
{
    mwinMacWindow* window = WindowOf(view);
    if (!window->pointerInside)
    {
        window->pointerInside = true;
        PostPointer(view, mwin_eventCursorEntered, event, 0, 0);
    }
    PostPointer(view, mwin_eventCursorMoved, event, 0, 0);
}

static void OnWheel(const MwinMacView* view, NSEvent* event)
{
    double per = event.hasPreciseScrollingDeltas ? POINTS_PER_DETENT : 1.0;
    // AppKit counts toward the left as positive x.
    mwinWheelEvent wheel = {(float)(-event.scrollingDeltaX / per),
                            (float)(event.scrollingDeltaY / per)};
    if (wheel.x == 0.0f && wheel.y == 0.0f)
    {
        return;
    }
    mwinEvent record = {.type = mwin_eventWheel};
    record.data.wheel = wheel;
    Post(view, &record);
}

@implementation MwinMacView
- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        self.wantsLayer = YES;
        NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                                        NSTrackingActiveAlways | NSTrackingInVisibleRect |
                                        NSTrackingEnabledDuringMouseDrag;
        NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                            options:options
                                                              owner:self
                                                           userInfo:nil];
        [self addTrackingArea:area];
        [area release];
    }
    return self;
}

- (CALayer*)makeBackingLayer
{
    return [CAMetalLayer layer];
}

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

// A click that activates the window is the program's too.
- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void)event;
    return YES;
}

- (void)keyDown:(NSEvent*)event
{
    mwinKeyCode code = mwinMacCodeOf(event.keyCode);
    if (code != mwin_codeUnknown)
    {
        PostKey(self, mwin_eventKeyDown, event, code);
    }
    if ((event.modifierFlags & NSEventModifierFlagCommand) == 0)
    {
        [self interpretKeyEvents:@[ event ]];
    }
}

- (void)keyUp:(NSEvent*)event
{
    mwinKeyCode code = mwinMacCodeOf(event.keyCode);
    if (code != mwin_codeUnknown)
    {
        PostKey(self, mwin_eventKeyUp, event, code);
    }
}

- (void)flagsChanged:(NSEvent*)event
{
    OnFlags(self, event);
}

- (void)mouseDown:(NSEvent*)event
{
    OnButton(self, event, true);
}

- (void)mouseUp:(NSEvent*)event
{
    OnButton(self, event, false);
}

- (void)rightMouseDown:(NSEvent*)event
{
    OnButton(self, event, true);
}

- (void)rightMouseUp:(NSEvent*)event
{
    OnButton(self, event, false);
}

- (void)otherMouseDown:(NSEvent*)event
{
    OnButton(self, event, true);
}

- (void)otherMouseUp:(NSEvent*)event
{
    OnButton(self, event, false);
}

- (void)mouseMoved:(NSEvent*)event
{
    OnMove(self, event);
}

- (void)mouseDragged:(NSEvent*)event
{
    OnMove(self, event);
}

- (void)rightMouseDragged:(NSEvent*)event
{
    OnMove(self, event);
}

- (void)otherMouseDragged:(NSEvent*)event
{
    OnMove(self, event);
}

- (void)mouseEntered:(NSEvent*)event
{
    OnMove(self, event);
}

- (void)mouseExited:(NSEvent*)event
{
    mwinMacWindow* window = WindowOf(self);
    if (window->pointerInside)
    {
        window->pointerInside = false;
        PostPointer(self, mwin_eventCursorLeft, event, 0, 0);
    }
}

- (void)scrollWheel:(NSEvent*)event
{
    OnWheel(self, event);
}

// The text input client. Compositions come in a later slice: marked
// text is not kept, and committed text is posted.
- (void)insertText:(id)string replacementRange:(NSRange)range
{
    (void)range;
    PostText(self, [string isKindOfClass:[NSAttributedString class]]
                       ? ((NSAttributedString*)string).string
                       : (NSString*)string);
}

// Keys that make commands (Enter, the arrows) are keys already; nothing
// beeps.
- (void)doCommandBySelector:(SEL)selector
{
    (void)selector;
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selected replacementRange:(NSRange)range
{
    (void)string;
    (void)selected;
    (void)range;
}

- (void)unmarkText
{
}

- (NSRange)selectedRange
{
    return NSMakeRange(NSNotFound, 0);
}

- (NSRange)markedRange
{
    return NSMakeRange(NSNotFound, 0);
}

- (BOOL)hasMarkedText
{
    return NO;
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                               actualRange:(NSRangePointer)actual
{
    (void)range;
    (void)actual;
    return nil;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
    return @[];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    (void)range;
    (void)actual;
    return [self.window convertRectToScreen:[self convertRect:NSZeroRect toView:nil]];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    (void)point;
    return NSNotFound;
}
@end

NSView* mwinMacCreateView(mwinMacPlatform* platform, uint32_t slot, NSRect frame)
{
    MwinMacView* view = [[MwinMacView alloc] initWithFrame:frame];
    if (view != nil)
    {
        view->platform = platform;
        view->slot = slot;
    }
    return view;
}
