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
 * A provider whose documents never end, for test_android_drop: opened,
 * each sends its first bytes through a pipe, the pipe's other end held
 * open while the process runs. "stalled" then sends nothing; "trickles"
 * goes on sending a byte a second, so its copy never waits five seconds.
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
            if ("trickles".equals(uri.getLastPathSegment())) {
                trickle(pipe[1]);
            }
            return pipe[0];
        } catch (IOException | ErrnoException e) {
            throw new FileNotFoundException(e.toString());
        }
    }

    // A byte a second into the pipe, from a thread of its own, until the
    // process ends or the copy closes its end.
    private static void trickle(ParcelFileDescriptor end) {
        Thread thread = new Thread(() -> {
            try {
                while (true) {
                    Thread.sleep(1000);
                    Os.write(end.getFileDescriptor(), new byte[1], 0, 1);
                }
            } catch (InterruptedException | ErrnoException | IOException e) {
                // The copy's end closed.
            }
        });
        thread.setDaemon(true);
        thread.start();
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] arguments,
            String order) {
        MatrixCursor cursor = new MatrixCursor(new String[] {OpenableColumns.DISPLAY_NAME});
        cursor.addRow(new Object[] {uri.getLastPathSegment() + ".bin"});
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
