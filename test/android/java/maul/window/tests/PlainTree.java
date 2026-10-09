// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window.tests;

import android.view.View;

/**
 * The test's tree with Explorer's method but not Explorer, as a provider
 * of another library has it; the library finds the method by reflection.
 */
public final class PlainTree extends Tree {
    public PlainTree(View host) {
        super(host);
    }

    public int virtualViewAt(float x, float y) {
        return nodeAt(x, y);
    }
}
