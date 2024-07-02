#pragma once

#include "vulkan_types.h"

VkSampleCountFlagBits get_sample_count(U32 sample_count);
VkPresentModeKHR pick_present_mode(bool vsync, Vulkan_Swapchain_Support *swapchain_support);

VkFormat get_texture_format(Texture_Format texture_format);
VkImageLayout get_image_layout(Resource_State resource_state, Texture_Format format);
VkAccessFlags get_access_flags(Resource_State resource_state, Texture_Format format);
VkPipelineStageFlags get_pipeline_stage_flags(VkAccessFlags access_flags);

void transtion_image_to_layout(VkCommandBuffer command_buffer, VkImage image, U32 base_mip_level, U32 mip_levels, U32 base_layer, U32 layer_count, VkImageLayout old_layout, VkImageLayout new_layout);

void copy_data_to_image(Vulkan_Context *context, Vulkan_Image *image, U32 width, U32 height, U32 mip_levels, U32 layer_count, VkFormat format, Array_View< void * > data, Upload_Request_Handle upload_request_handle);

Vulkan_Thread_State *get_thread_state(Vulkan_Context *context);

Vulkan_Command_Buffer begin_one_use_command_buffer(Vulkan_Context *context);
void end_one_use_command_buffer(Vulkan_Context *context, Vulkan_Command_Buffer *command_buffer);