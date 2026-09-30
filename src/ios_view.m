// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// iOS windows' views (ios.h): a view backed by a CAMetalLayer, which
// fills its window through the view controller that shows it. A layer
// of the view's own class draws at a scale of one unless told: the view
// takes its screen's scale whenever UIKit lays it out, and the window
// then posts what changed. The status bar is hidden, the window being
// the program's to draw; the safe area says what the system still
// covers. Touches, the pointer's hover and its scrolling come to the
// view (ios_pointer.m).

#include "ios.h"

@interface MwinIOSView : UIView
{
  @public
    mwinIOSPlatform* platform;
    uint32_t slot;
}
@end

@implementation MwinIOSView
+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    if (platform == nullptr)
    {
        return;
    }
    UIScreen* screen = self.window.windowScene.screen;
    if (screen != nil && self.contentScaleFactor != screen.scale)
    {
        self.contentScaleFactor = screen.scale;
    }
    mwinIOSLayout(platform, slot);
}

static void Touches(MwinIOSView* view, NSSet<UITouch*>* touches, UIEvent* event,
                    mwinIOSTouchPhase phase)
{
    if (view->platform != nullptr)
    {
        mwinIOSTouches(view->platform, view->slot, touches, event, phase);
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    Touches(self, touches, event, mwin_iosTouchBegan);
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    Touches(self, touches, event, mwin_iosTouchMoved);
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    Touches(self, touches, event, mwin_iosTouchEnded);
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    Touches(self, touches, event, mwin_iosTouchCancelled);
}

// The recognizers' actions.
- (void)mwinHover:(UIHoverGestureRecognizer*)hover
{
    if (platform != nullptr)
    {
        mwinIOSHover(platform, slot, hover);
    }
}

- (void)mwinScroll:(UIPanGestureRecognizer*)pan
{
    if (platform != nullptr)
    {
        mwinIOSScroll(platform, slot, pan);
    }
}

- (void)safeAreaInsetsDidChange
{
    [super safeAreaInsetsDidChange];
    if (platform != nullptr)
    {
        mwinIOSLayout(platform, slot);
    }
}
@end

@interface MwinIOSController : UIViewController
@end

@implementation MwinIOSController
- (BOOL)prefersStatusBarHidden
{
    return YES;
}
@end

UIView* mwinIOSCreateView(mwinIOSPlatform* platform, uint32_t slot, CGRect frame)
{
    MwinIOSView* view = [[MwinIOSView alloc] initWithFrame:frame];
    view->platform = platform;
    view->slot = slot;
    view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    view.multipleTouchEnabled = YES;
    // The pointer's motion without a button, and its scrolling alone; the
    // touches still reach the view.
    UIHoverGestureRecognizer* hover =
        [[UIHoverGestureRecognizer alloc] initWithTarget:view action:@selector(mwinHover:)];
    hover.allowedTouchTypes = @[ @(UITouchTypeIndirectPointer) ];
    hover.cancelsTouchesInView = NO;
    [view addGestureRecognizer:hover];
    [hover release];
    UIPanGestureRecognizer* pan =
        [[UIPanGestureRecognizer alloc] initWithTarget:view action:@selector(mwinScroll:)];
    pan.allowedScrollTypesMask = UIScrollTypeMaskAll;
    pan.allowedTouchTypes = @[];
    pan.cancelsTouchesInView = NO;
    [view addGestureRecognizer:pan];
    [pan release];
    return view;
}

UIViewController* mwinIOSCreateController(UIView* view)
{
    MwinIOSController* controller = [[MwinIOSController alloc] init];
    controller.view = view;
    return controller;
}

void mwinIOSForgetView(UIView* view)
{
    ((MwinIOSView*)view)->platform = nullptr;
}
