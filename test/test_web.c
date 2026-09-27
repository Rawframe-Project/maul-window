// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend in headless Chrome (test/web_runner.cjs): a canvas the
// library makes and one of the page's, their CSS sizes and device
// pixels at the page's ratio and after it changes, a size, a title,
// focus, fullscreen refused without a user's gesture, hiding, the facts
// and locales, and closing: the page's canvas stays, the library's goes.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/system.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <string.h>

#define DEADLINE_MS 10000.0
#define MAX_RECORDS 128

typedef enum Phase
{
    phaseCreate,
    phaseScale,
    phaseResize,
    phaseTitle,
    phaseFocus,
    phaseScheme,
    phaseFullscreen,
    phaseHide,
    phaseDestroy,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    double startMs;
    mwinWindowId made;
    mwinWindowId page;
    char madeSelector[128];
    mwinEvent records[MAX_RECORDS];
    int count;
    bool timedOut;
} Program;

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        program->records[program->count++] = event;
    }
}

// The nth record of a type, of a window or of any (a null id).
static const mwinEvent* Find(const Program* program, mwinEventType type, mwinWindowId window,
                             int nth)
{
    for (int i = 0; i < program->count; i++)
    {
        const mwinEvent* event = &program->records[i];
        bool mine = window.index1 == 0 || (event->window.index1 == window.index1 &&
                                           event->window.generation == window.generation);
        if (event->type == type && mine && nth-- == 0)
        {
            return event;
        }
    }
    return nullptr;
}

static const mwinEvent* Last(const Program* program, mwinEventType type, mwinWindowId window)
{
    const mwinEvent* found = nullptr;
    for (int nth = 0; Find(program, type, window, nth) != nullptr; nth++)
    {
        found = Find(program, type, window, nth);
    }
    return found;
}

static bool SizeIs(const Program* program, mwinWindowId window, float width, float height,
                   uint32_t pixelWidth, uint32_t pixelHeight)
{
    const mwinEvent* size = Last(program, mwin_eventResized, window);
    const mwinEvent* pixels = Last(program, mwin_eventPixelSizeChanged, window);
    return size != nullptr && pixels != nullptr && size->data.size.width == width &&
           size->data.size.height == height && pixels->data.pixelSize.width == pixelWidth &&
           pixels->data.pixelSize.height == pixelHeight;
}

static const mwinWindowId s_any = {0, 0};

EM_JS_DEPS(test_web, "$UTF8ToString");

// What the page shows.
// clang-format off
EM_JS(bool, Finds, (const char* selector), {
    return document.querySelector(UTF8ToString(selector)) !== null;
});

EM_JS(bool, IsHidden, (const char* selector), {
    return document.querySelector(UTF8ToString(selector)).style.display === 'none';
});

EM_JS(bool, LanguagesAre, (const char* text, size_t length), {
    return navigator.languages.join(',') === UTF8ToString(text, length);
});

EM_JS(bool, TitleIs, (const char* title), {
    return document.title === UTF8ToString(title);
});
// clang-format on

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventRequestCompleted, s_any, 1) != nullptr;
    case phaseScale:
    {
        const mwinEvent* pixels = Last(program, mwin_eventPixelSizeChanged, program->made);
        return Find(program, mwin_eventScaleChanged, program->made, 0) != nullptr &&
               pixels != nullptr && pixels->data.pixelSize.width == 960 &&
               pixels->data.pixelSize.height == 600;
    }
    case phaseResize:
        return SizeIs(program, program->made, 400.0f, 300.0f, 1200, 900);
    case phaseFocus:
        return Find(program, mwin_eventFocusGained, program->made, 0) != nullptr;
    case phaseScheme:
        return Find(program, mwin_eventThemeChanged, s_any, 0) != nullptr;
    case phaseHide:
        return Find(program, mwin_eventHidden, program->made, 0) != nullptr;
    case phaseDestroy:
        return Find(program, mwin_eventWindowDestroyed, s_any, 1) != nullptr;
    default:
        return Find(program, mwin_eventRequestCompleted, s_any, 0) != nullptr;
    }
}

static void CheckCreated(Program* program, mwinContext* context)
{
    CHECK(SizeIs(program, program->made, 320.0f, 200.0f, 640, 400) &&
              SizeIs(program, program->page, 200.0f, 100.0f, 400, 200),
          "CSS sizes, and device pixels at a ratio of 2");
    CHECK(Find(program, mwin_eventScaleChanged, program->made, 0)->data.scale.scale == 2.0f,
          "the scale");
    mwinNativeHandles handles;
    CHECK(mwinGetNativeHandles(context, program->page, &handles) == mwin_success &&
              handles.platform == mwin_platformWeb && handles.handles.web.selectorLength == 12 &&
              memcmp(handles.handles.web.selector, "#page-canvas", 12) == 0,
          "the page's canvas by its selector");
    CHECK(mwinGetNativeHandles(context, program->made, &handles) == mwin_success &&
              handles.handles.web.selectorLength < sizeof(program->madeSelector),
          "the made canvas's selector");
    memcpy(program->madeSelector, handles.handles.web.selector, handles.handles.web.selectorLength);
    program->madeSelector[handles.handles.web.selectorLength] = '\0';
    CHECK(Finds(program->madeSelector), "the selector finds the canvas");
    mwinSystemFacts facts;
    CHECK(mwinGetSystemFacts(context, &facts) == mwin_success && facts.reducedMotion &&
              facts.theme == mwin_themeLight,
          "reduced motion and a light scheme, as the runner set them");
    char locales[64];
    size_t length = 0;
    CHECK(mwinGetPreferredLocales(context, locales, sizeof(locales), &length) == mwin_success &&
              length > 0 && LanguagesAre(locales, length),
          "the preferred languages");
}

static void AdvanceLate(Program* program, mwinContext* context)
{
    const mwinEvent* completed = Find(program, mwin_eventRequestCompleted, s_any, 0);
    switch (program->phase)
    {
    case phaseScheme:
    {
        mwinSystemFacts facts;
        CHECK(mwinGetSystemFacts(context, &facts) == mwin_success && facts.theme == mwin_themeDark,
              "a dark scheme");
        CHECK(mwinRequestMode(context, program->made, mwin_modeBorderlessFullscreen, nullptr) ==
                  mwin_success,
              "fullscreen");
        break;
    }
    case phaseFullscreen:
        CHECK(completed->data.completion.outcome == mwin_outcomeDenied,
              "fullscreen needs a user's gesture");
        CHECK(mwinRequestVisible(context, program->made, false, nullptr) == mwin_success, "hide");
        break;
    case phaseHide:
        CHECK(IsHidden(program->madeSelector), "hidden");
        CHECK(mwinDestroyWindow(context, program->page) == mwin_success &&
                  mwinDestroyWindow(context, program->made) == mwin_success,
              "destroy both");
        break;
    default:
        CHECK(Finds("#page-canvas") && !Finds(program->madeSelector),
              "the page's canvas stays, the made one goes");
        break;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseCreate:
        CheckCreated(program, context);
        (void)printf("mwin-test: scale 3\n");
        break;
    case phaseScale:
        CHECK(Last(program, mwin_eventScaleChanged, program->made)->data.scale.scale == 3.0f &&
                  Find(program, mwin_eventResized, program->made, 0) == nullptr,
              "a new ratio: a new scale and pixels, the same size");
        CHECK(mwinRequestSize(context, program->made, (mwinSize){400.0f, 300.0f}, nullptr) ==
                  mwin_success,
              "a size");
        break;
    case phaseResize:
        CHECK(mwinRequestTitle(context, program->made, "Retitled", 8, nullptr) == mwin_success,
              "a title");
        break;
    case phaseTitle:
        CHECK(TitleIs("Retitled"), "the page's title");
        CHECK(mwinRequestFocus(context, program->made, nullptr) == mwin_success, "focus");
        break;
    case phaseFocus:
        (void)printf("mwin-test: scheme dark\n");
        break;
    default:
        AdvanceLate(program, context);
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->startMs = emscripten_get_now();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){320.0f, 200.0f};
    CHECK(mwinCreateWindow(context, &def, &program->made, nullptr) == mwin_success, "a canvas");
    def.canvas = "#page-canvas";
    def.canvasLength = 12;
    program->startMs = emscripten_get_now();
    return mwinCreateWindow(context, &def, &program->page, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (emscripten_get_now() - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        for (int i = 0; i < program->count; i++)
        {
            (void)printf("  record %d of window %u\n", (int)program->records[i].type,
                         program->records[i].window.index1);
        }
        program->timedOut = true;
        return mwin_frameStop;
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    const Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(!program->timedOut && program->phase == phaseDone, "every phase ran in time");
    (void)printf("mwin-test: exit %d\n", s_failures == 0 ? 0 : 1);
}

int main(void)
{
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs");
    // Only when mwinRun returns, which it does when init fails.
    (void)printf("mwin-test: exit 1\n");
    return 1;
}
