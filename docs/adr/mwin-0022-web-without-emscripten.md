# mwin-0022. The web without Emscripten

Status: Accepted

## Context

The web backend (mwin-0008) is built with Emscripten: its JavaScript is
in the C files as `EM_JS` functions, which Emscripten compiles into the
program, and its loop is Emscripten's main loop, which unwinds the stack
so `mwinRun` never returns.

Programs built for the web with a plain WebAssembly toolchain (Clang's
`wasm32-wasi` and wasi-libc, as Rawframe's engine is) have neither. Such
a program is a module the page instantiates, often a reactor whose
exports the page calls; nothing can compile JavaScript into it, and
nothing can unwind its stack.

## Decision

- **One set of JavaScript.** The `EM_JS` functions stay the only copy of
  the page's side. `src/web_js.h`, the one file that includes
  Emscripten's headers, defines `EM_JS` for a build without Emscripten
  as the declaration of a function imported under its own name from
  the module `env`: Emscripten's module for its own, and the one the
  linker gives every function a file only declares, so the files that
  call one agree on where it comes from. `tools/gen_web_glue.py` reads
  the same C files and writes `maul-window.mjs`, whose one export,
  `maulWindowImports(exports)`, returns those functions for the page
  to put among its `env` imports; `exports` is a function that returns
  the instance's exports, which exist only once the imports have been
  given. Every function's name starts with `mwin`, so it meets no
  other library's there. The build writes the file beside the library.
- **The runtime the functions use** is a few lines at the top of the
  generated file: the views of memory (`HEAPU8`, `HEAP32`, `HEAPF32`
  and the rest, made again when memory grows), `UTF8ToString`,
  `stringToUTF8` and `lengthBytesUTF8` as Emscripten defines them,
  `getWasmTableEntry` over the exported table, and `Module`, an object
  of the file's own. A program links with `--export-table`, so the
  page can call the backend back.
- **The loop.** Without Emscripten, `mwinRun` starts the program, hands
  its frames to the page's `requestAnimationFrame`, and returns
  `mwin_success` while the program runs on, since it cannot keep the
  stack. Its last frame ends the program as Emscripten's does: quit,
  then the backend stops and the context is freed. The public contract
  already puts a program's cleanup in quit because on the web
  `mwinRun` may not return; it may now also return before the program
  ends, and the documentation says so.
- **Time** comes from `performance.now()` through an `EM_JS` function in
  both builds, not `emscripten_get_now`.
- **Tests.** The web tests build both ways. Without Emscripten they are
  reactors whose `main` the runner calls; `test/web_runner.cjs` gives
  such a test a page of its own with a minimal WASI (standard output
  to the console, the clocks, random bytes and exit), the generated
  imports of the library and the test, and the same commands.

## Consequences

- The JavaScript cannot drift between the two builds: there is one
  copy, and the generator is what makes the second form of it. A
  construct the generator does not understand fails the build of the
  glue, not the page.
- The page must load `maul-window.mjs` and export the table: two lines
  in a program's own loader, which a plain WebAssembly program has
  anyway.
- A program that relied on `mwinRun` never returning on the web must
  not do anything after it but return; the tests now do exactly that.
