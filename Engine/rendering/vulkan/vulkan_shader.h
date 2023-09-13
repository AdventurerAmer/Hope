#pragma once

#include "vulkan_types.h"

bool load_shader(Shader_Handle shader_handle, const char *path, Vulkan_Context *context);

void destroy_shader(Shader_Handle shader_shader, Vulkan_Context *context);

bool create_graphics_pipeline(Pipeline_State_Handle pipeline_state_handle,
                              const std::initializer_list< Shader_Handle > &shaders,
                              VkRenderPass render_pass,
                              Vulkan_Context *context);

void destroy_pipeline(Pipeline_State_Handle pipeline_state_handle, Vulkan_Context *context);