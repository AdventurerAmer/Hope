#pragma once

#include "vulkan_types.h"

void transtion_image_to_layout(VkCommandBuffer command_buffer, VkImage image, U32 mip_levels, VkImageLayout old_layout, VkImageLayout new_layout);

bool create_image(Vulkan_Image *image, Vulkan_Context *context, U32 width, U32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkImageAspectFlags aspect_flags,
                  VkMemoryPropertyFlags memory_property_flags, bool mipmapping = false, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, Texture_Handle alias = Resource_Pool< Texture >::invalid_handle);

void copy_data_to_image_from_buffer(Vulkan_Context *context, Vulkan_Image *image, U32 width, U32 height, Vulkan_Buffer *buffer, U64 offset, U64 size, Allocation_Group *allocation_group);

void destroy_image(Vulkan_Image *image, Vulkan_Context *context);
