// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window.tests;

import android.view.View;
import maul.window.Explorer;

/** The test's tree, explored through the library's Explorer. */
public final class ExplorerTree extends Tree implements Explorer {
    public ExplorerTree(View host) {
        super(host);
    }
}
