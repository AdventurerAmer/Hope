#pragma once

#include "vulkan_types.h"

VkSampleCountFlagBits get_sample_count(U32 sample_count);
VkPresentModeKHR pick_present_mode(bool vsync, Vulkan_Swapchain_Support *swapchain_support);

VkFormat get_texture_format(Texture_Format texture_format);
VkImageLayout get_image_layout(Resource_State resource_state, Texture_Format format);
VkAccessFlags get_access_flags(Resource_State resource_state, Texture_Format format);
VkPipelineStageFlags get_pipeline_stage_flags(VkAccessFlags access_flags, bool compute_only);

void transtion_image_to_layout(VkCommandBuffer command_buffer, VkImage image, U32 base_mip_level, U32 mip_levels, U32 base_layer, U32 layer_count, VkImageLayout old_layout, VkImageLayout new_layout, bool compute_only = false);

void copy_data_to_image(Vulkan_Context *context, Vulkan_Command_Buffer *command_buffer, Vulkan_Image *image, const Texture_Descriptor &texture_descriptor, U32 mip_levels);

Vulkan_Thread_State *get_thread_state(Vulkan_Context *context);

Vulkan_Command_Buffer push_command_buffer(Command_Buffer_Usage usage, bool submit, Vulkan_Context *context, VkRenderPass render_pass = VK_NULL_HANDLE, VkFramebuffer frame_buffer = VK_NULL_HANDLE);
Vulkan_Command_Buffer get_commnad_buffer(Vulkan_Context *context);
Vulkan_Command_Buffer pop_command_buffer(Vulkan_Context *context, Upload_Request_Handle upload_request_handle = Resource_Pool< Upload_Request >::invalid_handle);