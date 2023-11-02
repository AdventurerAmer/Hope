#pragma once

#include "vulkan_types.h"

S32 find_memory_type_index(VkMemoryRequirements memory_requirements, VkMemoryPropertyFlags memory_property_flags, Vulkan_Context *context);
VkSampleCountFlagBits get_sample_count(U32 sample_count);
VkPresentModeKHR pick_present_mode(bool vsync, Vulkan_Swapchain_Support *swapchain_support);

VkFormat get_texture_format(Texture_Format texture_format);

void transtion_image_to_layout(VkCommandBuffer command_buffer, VkImage image, U32 mip_levels, U32 layer_count, VkImageLayout old_layout, VkImageLayout new_layout);

void copy_data_to_image(Vulkan_Context *context, Vulkan_Image *image, U32 width, U32 height, U32 mip_levels, U32 layer_count, VkFormat format, Array_View< void * > data, Allocation_Group *allocation_group);

void destroy_image(Vulkan_Image *image, Vulkan_Context *context);