// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window.tests;

import android.graphics.Rect;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import maul.window.Explorer;

/**
 * A test program's accessibility tree: the window, with "Alpha" over its
 * left half and "Beta" over its right. It remembers which nodes clients
 * read.
 */
public final class Tree extends AccessibilityNodeProvider implements Explorer {
    private static final String[] NAMES = {"Alpha", "Beta"};
    private final View host;
    /** The nodes read, as a bit each: 1 the window, 2 Alpha, 4 Beta. */
    public static volatile int read;

    public Tree(View host) {
        this.host = host;
    }

    @Override
    public AccessibilityNodeInfo createAccessibilityNodeInfo(int id) {
        if (id == View.NO_ID) {
            read |= 1;
            AccessibilityNodeInfo node = new AccessibilityNodeInfo(host);
            host.onInitializeAccessibilityNodeInfo(node);
            node.addChild(host, 1);
            node.addChild(host, 2);
            return node;
        }
        if (id != 1 && id != 2) {
            return null;
        }
        read |= 1 << id;
        AccessibilityNodeInfo node = new AccessibilityNodeInfo(host, id);
        node.setParent(host);
        node.setPackageName(host.getContext().getPackageName());
        node.setClassName("android.widget.Button");
        node.setText(NAMES[id - 1]);
        node.setEnabled(true);
        node.setVisibleToUser(true);
        int[] place = new int[2];
        host.getLocationOnScreen(place);
        int half = host.getWidth() / 2;
        Rect bounds = new Rect((id - 1) * half, 0, id * half, host.getHeight());
        bounds.offset(place[0], place[1]);
        node.setBoundsInScreen(bounds);
        return node;
    }

    @Override
    public int virtualViewAt(float x, float y) {
        return x < host.getWidth() / 2.0f ? 1 : 2;
    }
}
