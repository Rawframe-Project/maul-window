// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window.tests;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.system.ErrnoException;
import android.system.Os;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;

/**
 * A provider that stalls mid-document, for test_android_drop: its one
 * document, opened, sends its first bytes through a pipe and then
 * nothing, the pipe's other end held open while the process runs.
 */
public final class Stalls extends ContentProvider {
    /** The bytes the document sends before it stalls. */
    static final int SENT = 1000;

    // The pipes' ends the provider writes to, never closed.
    private static final ArrayList<ParcelFileDescriptor> held = new ArrayList<>();

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        try {
            ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
            Os.write(pipe[1].getFileDescriptor(), new byte[SENT], 0, SENT);
            synchronized (held) {
                held.add(pipe[1]);
            }
            return pipe[0];
        } catch (IOException | ErrnoException e) {
            throw new FileNotFoundException(e.toString());
        }
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] arguments,
            String order) {
        MatrixCursor cursor = new MatrixCursor(new String[] {OpenableColumns.DISPLAY_NAME});
        cursor.addRow(new Object[] {"stalled.bin"});
        return cursor;
    }

    @Override
    public String getType(Uri uri) {
        return "application/octet-stream";
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] arguments) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] arguments) {
        return 0;
    }
}
