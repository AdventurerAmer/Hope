#pragma once

#include "vulkan_types.h"

bool vulkan_renderer_init(struct Renderer_State *renderer_State,
                          struct Engine *engine,
                          struct Memory_Arena *arena);

void vulkan_renderer_deinit(struct Renderer_State *renderer_State);

void vulkan_renderer_on_resize(struct Renderer_State *renderer_State, U32 width, U32 height);

void vulkan_renderer_draw(struct Renderer_State *renderer_State, F32 delta_time);