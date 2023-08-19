#pragma once

#include "vulkan_types.h"

bool load_shader(Shader *shader, const char *path, Vulkan_Context *context);

void destroy_shader(Shader *shader, Vulkan_Context *context);

bool create_graphics_pipeline(Pipeline_State *pipeline,
                              const std::initializer_list< const Shader * > &shaders,
                              VkRenderPass render_pass,
                              Vulkan_Context *context);

void destroy_pipeline(Pipeline_State *pipeline_state, Vulkan_Context *context);