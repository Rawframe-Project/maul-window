// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// macOS drag and drop (macos.h): each window's view is a dragging
// destination for file URLs and text, taking a drag that carries either
// with the copy operation and reporting a drag over that has moved. Each
// item of a drop's pasteboard that carries a file URL is a file, its
// path; the first other item that carries a string is its text. Any
// other item, text after the first or neither, is left out and the drop
// marked truncated.

#include "apple_text.h"
#include "macos.h"

#include <string.h>

static mwinDragContents ContentsOf(NSPasteboard* pasteboard)
{
    mwinDragContents contents = 0;
    for (NSPasteboardItem* item in pasteboard.pasteboardItems)
    {
        if ([item availableTypeFromArray:@[ NSPasteboardTypeFileURL ]] != nil)
        {
            contents |= mwin_dragFiles;
        }
        else if ([item availableTypeFromArray:@[ NSPasteboardTypeString ]] != nil)
        {
            contents |= mwin_dragText;
        }
    }
    return contents;
}

static mwinPosition PositionOf(const mwinMacWindow* window, id<NSDraggingInfo> drag)
{
    NSPoint point = [window->view convertPoint:drag.draggingLocation fromView:nil];
    return (mwinPosition){(float)point.x, (float)point.y};
}

static void PostDrag(mwinMacPlatform* platform, uint32_t slot, mwinEventKind type)
{
    mwinMacWindow* window = &platform->windows[slot];
    mwinEvent event = {.kind = type, .timeNs = mwinMacNow()};
    event.data.drag = (mwinDragEvent){window->dragPosition, window->dragContents};
    mwinPost(platform->context, slot, &event);
}

NSDragOperation mwinMacDragEntered(mwinMacPlatform* platform, uint32_t slot,
                                   id<NSDraggingInfo> drag)
{
    mwinMacWindow* window = &platform->windows[slot];
    window->dragContents = ContentsOf(drag.draggingPasteboard);
    if (window->dragContents == 0)
    {
        return NSDragOperationNone;
    }
    window->dragPosition = PositionOf(window, drag);
    PostDrag(platform, slot, mwin_eventDragEntered);
    return NSDragOperationCopy;
}

// AppKit calls it again and again while the drag rests.
NSDragOperation mwinMacDragUpdated(mwinMacPlatform* platform, uint32_t slot,
                                   id<NSDraggingInfo> drag)
{
    mwinMacWindow* window = &platform->windows[slot];
    if (window->dragContents == 0)
    {
        return NSDragOperationNone;
    }
    mwinPosition position = PositionOf(window, drag);
    if (position.x != window->dragPosition.x || position.y != window->dragPosition.y)
    {
        window->dragPosition = position;
        PostDrag(platform, slot, mwin_eventDragMoved);
    }
    return NSDragOperationCopy;
}

void mwinMacDragExited(mwinMacPlatform* platform, uint32_t slot)
{
    mwinMacWindow* window = &platform->windows[slot];
    if (window->dragContents != 0)
    {
        PostDrag(platform, slot, mwin_eventDragLeft);
    }
    window->dragContents = 0;
}

bool mwinMacDrop(mwinMacPlatform* platform, uint32_t slot, id<NSDraggingInfo> drag)
{
    mwinMacWindow* window = &platform->windows[slot];
    mwinContext* context = platform->context;
    if (window->dragContents == 0)
    {
        return false;
    }
    window->dragContents = 0;
    mwinBeginDrop(context);
    bool texted = false;
    for (NSPasteboardItem* item in drag.draggingPasteboard.pasteboardItems)
    {
        NSString* address = [item stringForType:NSPasteboardTypeFileURL];
        NSString* text =
            address == nil && !texted ? [item stringForType:NSPasteboardTypeString] : nil;
        if (address != nil)
        {
            // Another program's URL may not be a file's, or name a path
            // that is not UTF-8. A file reference URL gives its path.
            NSURL* file = [NSURL URLWithString:address];
            const char* path = file.isFileURL ? file.filePathURL.path.UTF8String : nullptr;
            if (path != nullptr)
            {
                mwinAddDroppedFile(context, path, strlen(path));
            }
            else
            {
                context->dropping.truncated = true;
            }
        }
        else if (text != nil)
        {
            mwinAppleSetDroppedText(context, text);
            texted = true;
        }
        else
        {
            context->dropping.truncated = true;
        }
    }
    mwinFinishDrop(context, slot, PositionOf(window, drag), mwinMacNow());
    return true;
}
