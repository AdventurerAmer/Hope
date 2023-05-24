#pragma once

#include "vulkan_types.h"

//
// buffers
//

bool
create_buffer(Vulkan_Buffer *buffer, Vulkan_Context *context,
              U64 size, VkBufferUsageFlags usage_flags,
              VkMemoryPropertyFlags memory_property_flags);

void
copy_buffer(Vulkan_Context *context, Vulkan_Buffer *src_buffer,
            Vulkan_Buffer *dst_buffer, void *data, U64 size);

void
destroy_buffer(Vulkan_Buffer *buffer,
               VkDevice logical_device);

//
// images
//

bool create_image(Vulkan_Image *image, Vulkan_Context *context,
                  U32 width, U32 height, VkFormat format,
                  VkImageTiling tiling, VkImageUsageFlags usage,
                  VkImageAspectFlags aspect_flags,
                  VkMemoryPropertyFlags memory_property_flags);

void
copy_buffer_to_image(Vulkan_Context *context, Vulkan_Buffer *buffer,
                     Vulkan_Image *image, void *data, U64 size);

void
destroy_image(Vulkan_Image *image, Vulkan_Context *context);