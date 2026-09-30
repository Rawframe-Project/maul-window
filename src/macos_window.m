// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// macOS windows (macos.h): an NSWindow whose content view is the
// library's, layer-backed by a CAMetalLayer, and a delegate that reports
// what AppKit did to it. Sizes and places are logical, in points; a
// place is the content's top left from the top left of the primary
// screen. Closing asks the program. Borderless full screen is the
// window's own full-screen space, which answers once AppKit has entered
// or left it; the other requests answer at once.

#include "macos.h"

#include <math.h>
#include <string.h>

// The content view: flipped, so its coordinates run down from the top
// left as the contract's do, and backed by a CAMetalLayer for a GPU
// layer to present to.
@interface MwinMacView : NSView
@end

@implementation MwinMacView
- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        self.wantsLayer = YES;
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
@end

// A window's delegate: its platform and slot.
@interface MwinMacDelegate : NSObject <NSWindowDelegate>
{
  @public
    mwinMacPlatform* platform;
    uint32_t slot;
}
@end

static void Post(mwinMacPlatform* platform, uint32_t slot, mwinEvent* event)
{
    event->timeNs = mwinMacNow();
    mwinPost(platform->context, slot, event);
}

static void PostType(mwinMacPlatform* platform, uint32_t slot, mwinEventType type)
{
    mwinEvent event = {.type = type};
    Post(platform, slot, &event);
}

// The content's size in points.
static NSSize PointsOf(const mwinMacWindow* window)
{
    return window->view.bounds.size;
}

// Posts the scale, then the size in points and pixels, where they
// changed; the layer's scale follows the window's.
static void PostSize(mwinMacPlatform* platform, uint32_t slot)
{
    mwinMacWindow* window = &platform->windows[slot];
    NSSize points = PointsOf(window);
    float scale = (float)window->window.backingScaleFactor;
    mwinSize size = {(float)points.width, (float)points.height};
    uint32_t width = (uint32_t)lround(points.width * (CGFloat)scale);
    uint32_t height = (uint32_t)lround(points.height * (CGFloat)scale);
    mwinEvent event = {0};
    if (scale != window->scale)
    {
        window->scale = scale;
        window->layer.contentsScale = (CGFloat)scale;
        event.type = mwin_eventScaleChanged;
        event.data.scale = (mwinScaleChange){scale, size};
        Post(platform, slot, &event);
    }
    if (width != window->width || height != window->height)
    {
        window->width = width;
        window->height = height;
        event.type = mwin_eventResized;
        event.data.size = size;
        Post(platform, slot, &event);
        event.type = mwin_eventPixelSizeChanged;
        event.data.pixelSize = (mwinPixelSize){width, height};
        Post(platform, slot, &event);
    }
}

// The content's top left, from the top left of the primary screen.
static mwinPosition PositionOf(const mwinMacWindow* window)
{
    NSRect content = [window->window contentRectForFrameRect:window->window.frame];
    return (mwinPosition){(float)NSMinX(content),
                          (float)(mwinMacPrimaryHeight() - NSMaxY(content))};
}

static void PostMove(mwinMacPlatform* platform, uint32_t slot)
{
    mwinMacWindow* window = &platform->windows[slot];
    mwinPosition position = PositionOf(window);
    if (position.x == window->position.x && position.y == window->position.y)
    {
        return;
    }
    window->position = position;
    mwinEvent event = {.type = mwin_eventMoved};
    event.data.position = position;
    Post(platform, slot, &event);
}

// Reports the monitor the window is on when it changed.
static void PostMonitor(mwinMacPlatform* platform, uint32_t slot)
{
    mwinMacWindow* window = &platform->windows[slot];
    int32_t monitor = mwinMacMonitorOf(platform, window->window.screen);
    if (monitor < 0 || monitor == window->monitor)
    {
        return;
    }
    window->monitor = monitor;
    mwinEvent event = {.type = mwin_eventDisplayChanged};
    event.data.monitor = mwinMonitorIdOf(platform->context, (uint32_t)monitor);
    Post(platform, slot, &event);
}

static void PostMode(mwinMacPlatform* platform, uint32_t slot, mwinWindowMode mode)
{
    mwinMacWindow* window = &platform->windows[slot];
    if (mode == window->mode)
    {
        return;
    }
    window->mode = mode;
    mwinEvent event = {.type = mwin_eventModeChanged};
    event.data.mode = mode;
    Post(platform, slot, &event);
}

// Answers a mode request that waits for AppKit, if one does.
static void AnswerMode(mwinMacPlatform* platform, uint32_t slot, mwinOutcome outcome)
{
    mwinContext* context = platform->context;
    int32_t request = mwinFindActiveRequest(&context->windows[slot],
                                            context->limits.requestsPerWindow, mwin_requestMode);
    if (request >= 0)
    {
        mwinComplete(context, slot, (uint32_t)request, outcome);
    }
}

static bool IsFullscreen(const mwinMacWindow* window)
{
    return (window->window.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

@implementation MwinMacDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender
{
    (void)sender;
    PostType(platform, slot, mwin_eventCloseRequested);
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification
{
    (void)notification;
    PostSize(platform, slot);
    PostMove(platform, slot);
}

- (void)windowDidMove:(NSNotification*)notification
{
    (void)notification;
    PostMove(platform, slot);
    PostMonitor(platform, slot);
}

- (void)windowDidChangeScreen:(NSNotification*)notification
{
    (void)notification;
    PostMonitor(platform, slot);
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification
{
    (void)notification;
    PostSize(platform, slot);
}

- (void)windowDidBecomeKey:(NSNotification*)notification
{
    (void)notification;
    PostType(platform, slot, mwin_eventFocusGained);
}

- (void)windowDidResignKey:(NSNotification*)notification
{
    (void)notification;
    PostType(platform, slot, mwin_eventFocusLost);
}

- (void)windowDidMiniaturize:(NSNotification*)notification
{
    (void)notification;
    PostMode(platform, slot, mwin_modeMinimized);
}

- (void)windowDidDeminiaturize:(NSNotification*)notification
{
    (void)notification;
    mwinMacWindow* window = &platform->windows[slot];
    PostMode(platform, slot, window->window.zoomed ? mwin_modeMaximized : mwin_modeWindowed);
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification
{
    (void)notification;
    PostMode(platform, slot, mwin_modeBorderlessFullscreen);
    AnswerMode(platform, slot, mwin_outcomeDone);
}

- (void)windowDidExitFullScreen:(NSNotification*)notification
{
    (void)notification;
    mwinMacWindow* window = &platform->windows[slot];
    PostMode(platform, slot, window->window.zoomed ? mwin_modeMaximized : mwin_modeWindowed);
    AnswerMode(platform, slot, mwin_outcomeDone);
}

- (void)windowDidFailToEnterFullScreen:(NSWindow*)window
{
    (void)window;
    AnswerMode(platform, slot, mwin_outcomeFailed);
}

- (void)windowDidFailToExitFullScreen:(NSWindow*)window
{
    (void)window;
    AnswerMode(platform, slot, mwin_outcomeFailed);
}

- (void)windowDidChangeOcclusionState:(NSNotification*)notification
{
    (void)notification;
    mwinMacWindow* window = &platform->windows[slot];
    bool visible = (window->window.occlusionState & NSWindowOcclusionStateVisible) != 0;
    PostType(platform, slot, visible ? mwin_eventRevealed : mwin_eventOccluded);
}
@end

static NSWindowStyleMask StyleMaskOf(mwinWindowStyle style)
{
    NSWindowStyleMask mask = NSWindowStyleMaskBorderless;
    if ((style & mwin_styleDecorated) != 0)
    {
        mask |=
            NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    }
    if ((style & mwin_styleResizable) != 0)
    {
        mask |= NSWindowStyleMaskResizable;
    }
    return mask;
}

static NSString* StringOf(const char* bytes, size_t length)
{
    return [[[NSString alloc] initWithBytes:bytes length:length
                                   encoding:NSUTF8StringEncoding] autorelease];
}

// Reports a new window as the contract's creation does: made, on its
// monitor, at its scale, its size, windowed, at its place.
static void Establish(mwinMacPlatform* platform, uint32_t slot)
{
    PostType(platform, slot, mwin_eventWindowCreated);
    PostMonitor(platform, slot);
    platform->windows[slot].mode = (mwinWindowMode)0xFF;
    PostSize(platform, slot);
    PostMode(platform, slot, mwin_modeWindowed);
    PostMove(platform, slot);
}

void mwinMacCreateWindow(mwinContext* context, uint32_t slot)
{
    mwinMacPlatform* platform = mwinMacPlatformOf(context);
    const mwinWindow* core = &context->windows[slot];
    mwinMacWindow* window = &platform->windows[slot];
    *window = (mwinMacWindow){.monitor = -1, .position = {NAN, NAN}};
    int32_t request =
        mwinFindActiveRequest(core, context->limits.requestsPerWindow, mwin_requestCreate);
    @autoreleasepool
    {
        NSRect content =
            NSMakeRect(0.0, 0.0, (CGFloat)core->def.size.width, (CGFloat)core->def.size.height);
        NSWindow* made = [[NSWindow alloc] initWithContentRect:content
                                                     styleMask:StyleMaskOf(core->def.style)
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
        if (made == nil)
        {
            mwinComplete(context, slot, (uint32_t)request, mwin_outcomeFailed);
            return;
        }
        made.releasedWhenClosed = NO;
        made.collectionBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;
        if ((core->def.style & mwin_styleAlwaysOnTop) != 0)
        {
            made.level = NSFloatingWindowLevel;
        }
        MwinMacView* view = [[MwinMacView alloc] initWithFrame:content];
        made.contentView = view;
        MwinMacDelegate* delegate = [[MwinMacDelegate alloc] init];
        delegate->platform = platform;
        delegate->slot = slot;
        made.delegate = delegate;
        made.title = StringOf(core->title, core->titleLength);
        [made center];
        window->window = made;
        window->view = view;
        window->layer = (CAMetalLayer*)view.layer;
        window->delegate = delegate;
        [view release];
        Establish(platform, slot);
        if (core->def.visible)
        {
            [made makeKeyAndOrderFront:nil];
            PostType(platform, slot, mwin_eventShown);
        }
        if (core->def.mode == mwin_modeMaximized)
        {
            [made zoom:nil];
            PostMode(platform, slot, mwin_modeMaximized);
        }
        else if (core->def.mode == mwin_modeMinimized)
        {
            [made miniaturize:nil];
        }
        else if (core->def.mode == mwin_modeBorderlessFullscreen)
        {
            [made toggleFullScreen:nil];
        }
    }
    mwinComplete(context, slot, (uint32_t)request, mwin_outcomeDone);
}

void mwinMacDestroyWindow(mwinContext* context, uint32_t slot)
{
    mwinMacWindow* window = &mwinMacPlatformOf(context)->windows[slot];
    @autoreleasepool
    {
        if (window->window != nil)
        {
            window->window.delegate = nil;
            [window->window orderOut:nil];
            [window->window close];
            [window->window release];
        }
        [window->delegate release];
    }
    *window = (mwinMacWindow){.monitor = -1};
}

// A mode request's outcome, or -1 when AppKit answers later.
static int SetMode(mwinMacPlatform* platform, uint32_t slot, mwinWindowMode mode)
{
    mwinMacWindow* window = &platform->windows[slot];
    NSWindow* made = window->window;
    bool fullscreen = IsFullscreen(window);
    if (mode == mwin_modeBorderlessFullscreen)
    {
        if (fullscreen)
        {
            return mwin_outcomeDone;
        }
        [made toggleFullScreen:nil];
        return -1;
    }
    if (fullscreen)
    {
        // Leaving the space first; its end answers.
        [made toggleFullScreen:nil];
        return -1;
    }
    switch (mode)
    {
    case mwin_modeMinimized:
        [made miniaturize:nil];
        break;
    case mwin_modeMaximized:
        if (made.miniaturized)
        {
            [made deminiaturize:nil];
        }
        if (!made.zoomed)
        {
            [made zoom:nil];
        }
        PostMode(platform, slot, mwin_modeMaximized);
        break;
    default:
        if (made.miniaturized)
        {
            [made deminiaturize:nil];
        }
        if (made.zoomed)
        {
            [made zoom:nil];
        }
        PostMode(platform, slot, mwin_modeWindowed);
        break;
    }
    return mwin_outcomeDone;
}

// Moves the content's top left to a place from the top left of the
// primary screen.
static void Place(const mwinMacWindow* window, mwinPosition position)
{
    NSRect current = [window->window contentRectForFrameRect:window->window.frame];
    NSRect content = NSMakeRect((CGFloat)position.x,
                                mwinMacPrimaryHeight() - (CGFloat)position.y - NSHeight(current),
                                NSWidth(current), NSHeight(current));
    [window->window setFrameOrigin:[window->window frameRectForContentRect:content].origin];
}

// Carries out a request now: its outcome, or -1 when AppKit answers
// later.
static int CarryOut(mwinContext* context, uint32_t slot, const mwinRequest* request)
{
    mwinMacPlatform* platform = mwinMacPlatformOf(context);
    mwinMacWindow* window = &platform->windows[slot];
    mwinWindow* core = &context->windows[slot];
    switch (request->kind)
    {
    case mwin_requestTitle:
        window->window.title = StringOf(core->pendingTitle, core->pendingTitleLength);
        memmove(core->title, core->pendingTitle, core->pendingTitleLength);
        core->titleLength = core->pendingTitleLength;
        return mwin_outcomeDone;
    case mwin_requestSize:
        [window->window setContentSize:NSMakeSize((CGFloat)request->value.size.width,
                                                  (CGFloat)request->value.size.height)];
        return mwin_outcomeDone;
    case mwin_requestPosition:
        Place(window, request->value.position);
        return mwin_outcomeDone;
    case mwin_requestMode:
        return SetMode(platform, slot, request->value.mode);
    case mwin_requestVisible:
        if (request->value.visible)
        {
            [window->window orderFront:nil];
        }
        else
        {
            [window->window orderOut:nil];
        }
        PostType(platform, slot, request->value.visible ? mwin_eventShown : mwin_eventHidden);
        return mwin_outcomeDone;
    case mwin_requestFocus:
        [window->window makeKeyAndOrderFront:nil];
        return mwin_outcomeDone;
    default:
        return mwin_outcomeUnsupported;
    }
}

void mwinMacSubmit(mwinContext* context, uint32_t slot, uint32_t request)
{
    @autoreleasepool
    {
        int outcome = CarryOut(context, slot, &context->windows[slot].requests[request]);
        if (outcome >= 0)
        {
            mwinComplete(context, slot, request, (mwinOutcome)outcome);
        }
    }
}
