#pragma once

#include "vulkan_types.h"

bool load_shader(Shader_Handle shader_handle, void *data, U64 size, Vulkan_Context *context);

void destroy_shader(Shader_Handle shader_shader, Vulkan_Context *context);

bool create_graphics_pipeline(Pipeline_State_Handle pipeline_state_handle,  const Pipeline_State_Descriptor &descriptor, Vulkan_Context *context);

void destroy_pipeline(Pipeline_State_Handle pipeline_state_handle, Vulkan_Context *context);