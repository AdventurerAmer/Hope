#pragma once

#include "vulkan_types.h"

bool create_shader(Shader_Handle shader_handle, const Shader_Descriptor &descriptor, Vulkan_Context *context);
void destroy_shader(Vulkan_Shader *vulkan_shader, Vulkan_Context *context);

bool create_graphics_pipeline(Pipeline_State_Handle pipeline_state_handle,  const Pipeline_State_Descriptor &descriptor, Vulkan_Context *context);
bool create_compute_pipeline(Pipeline_State_Handle pipeline_state_handle,  const Pipeline_State_Descriptor &descriptor, Vulkan_Context *context);

void destroy_pipeline(Vulkan_Pipeline_State *pipeline_state, Vulkan_Context *context);