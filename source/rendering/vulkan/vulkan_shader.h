#pragma once

#include "vulkan_types.h"

bool
load_shader(Vulkan_Shader *shader, const char *path,
            Vulkan_Context *context, Memory_Arena *arena);

void
destroy_shader(Vulkan_Shader *shader, VkDevice logical_device);

bool
create_graphics_pipeline(Vulkan_Context *context,
                         const std::initializer_list<const Vulkan_Shader *> &shaders,
                         VkRenderPass render_pass,
                         Vulkan_Graphics_Pipeline *pipeline);

void
destroy_graphics_pipeline(VkDevice logical_device,
                          Vulkan_Graphics_Pipeline *graphics_pipeline);