// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window.tests;

import android.accessibilityservice.AccessibilityService;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;

/**
 * An accessibility client the test runs in its own application, enabled
 * by the runner, that asks for touch exploration (res/xml/client.xml):
 * it reads the active window's tree through the system, as a screen
 * reader does, keeps the texts of the nodes hovers entered, in order, and of
 * the last one left, and
 * counts the content changes its own application announces.
 */
public final class Client extends AccessibilityService {
    private static volatile Client running;
    /** The texts of the nodes hovers entered, each with a comma. */
    public static volatile String entered = "";
    /** The text of the last node a hover left, or null. */
    public static volatile String exited;
    /** The content changes heard from the test's application. */
    public static volatile int changes;

    @Override
    protected void onServiceConnected() {
        running = this;
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
        if (event.getEventType() == AccessibilityEvent.TYPE_VIEW_HOVER_ENTER
                && !event.getText().isEmpty()) {
            entered = entered + event.getText().get(0) + ",";
        }
        if (event.getEventType() == AccessibilityEvent.TYPE_VIEW_HOVER_EXIT
                && !event.getText().isEmpty()) {
            exited = event.getText().get(0).toString();
        }
        if (event.getEventType() == AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED
                && getPackageName().contentEquals(event.getPackageName())) {
            changes++;
        }
    }

    @Override
    public void onInterrupt() {
    }

    /** Whether the client runs and touch exploration is on. */
    public static boolean exploring() {
        Client client = running;
        return client != null
                && client.getSystemService(AccessibilityManager.class).isTouchExplorationEnabled();
    }

    /**
     * The active window's nodes: their count, a colon, and their texts with
     * commas. Not on the main thread: the window answers there.
     */
    public static String describe() {
        Client client = running;
        AccessibilityNodeInfo root = client != null ? client.getRootInActiveWindow() : null;
        StringBuilder texts = new StringBuilder();
        int count = collect(root, texts);
        return count + ":" + texts;
    }

    private static int collect(AccessibilityNodeInfo node, StringBuilder texts) {
        if (node == null) {
            return 0;
        }
        if (node.getText() != null) {
            texts.append(node.getText()).append(',');
        }
        int count = 1;
        for (int i = 0; i < node.getChildCount(); i++) {
            count += collect(node.getChild(i), texts);
        }
        return count;
    }
}
