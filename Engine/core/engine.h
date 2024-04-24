#pragma once

#include "core/defines.h"
#include "core/memory.h"
#include "core/logging.h"
#include "core/platform.h"
#include "core/input.h"

#include "rendering/renderer.h"

struct Engine
{
    String name;
    String app_name;

    bool is_running;
    bool is_minimized;
    bool show_cursor;
    bool lock_cursor;
    Window window;

    Input input;
};

bool startup(Engine *engine);
void on_event(Engine *engine, Event event);
void game_loop(Engine *engine, F32 delta_time);
void shutdown(Engine *engine);