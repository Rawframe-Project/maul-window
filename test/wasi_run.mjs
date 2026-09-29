// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runs a test built for wasm32-wasi under Node's WASI
// (cmake/wasm32-wasi.cmake). A test that needs a page runs in a browser
// instead (web_runner.cjs). Here the web backend is told there is no
// page, as it is under Node with Emscripten, and its other imports fail,
// since nothing reaches them without one.
//
// usage: node wasi_run.mjs <test.wasm> [arguments]

import {readFile} from 'node:fs/promises';
import {WASI} from 'node:wasi';
import {argv, env, exit} from 'node:process';

const wasi = new WASI({version: 'preview1', args: argv.slice(2), env, returnOnExit: true});
const module = await WebAssembly.compile(await readFile(argv[2]));
const page = new Proxy({mwinWebHasDocument: () => 0}, {
    get: (target, name) => target[name] || (() => {
        throw new Error(`${String(name)} needs a page`);
    }),
});
const instance = await WebAssembly.instantiate(module, {wasi_snapshot_preview1: wasi.wasiImport, env: page});
exit(wasi.start(instance));
