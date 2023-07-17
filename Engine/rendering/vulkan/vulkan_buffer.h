#pragma once

#include "vulkan_types.h"

bool
create_buffer(Vulkan_Buffer *buffer, Vulkan_Context *context,
              U64 size, VkBufferUsageFlags usage_flags,
              VkMemoryPropertyFlags memory_property_flags);

void copy_data_to_buffer_from_buffer(Vulkan_Context *context,
                                     Vulkan_Buffer *dst, U64 dst_offset,
                                     Vulkan_Buffer *src, U64 src_offset,
                                     U64 size);


void
destroy_buffer(Vulkan_Buffer *buffer,
               VkDevice logical_device);