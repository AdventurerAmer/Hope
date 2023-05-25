#pragma once

#include "vulkan_types.h"

S32
find_memory_type_index(Vulkan_Context *context,
                       VkMemoryRequirements memory_requirements,
                       VkMemoryPropertyFlags memory_property_flags);

bool vulkan_renderer_init(struct Renderer_State *renderer_State,
                          struct Engine *engine,
                          struct Memory_Arena *arena);

void vulkan_renderer_deinit(struct Renderer_State *renderer_State);

void vulkan_renderer_on_resize(struct Renderer_State *renderer_State, U32 width, U32 height);

void vulkan_renderer_draw(struct Renderer_State *renderer_State, F32 delta_time);