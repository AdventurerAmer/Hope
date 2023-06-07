#pragma once

#include "vulkan_types.h"

bool
create_buffer(Vulkan_Buffer *buffer, Vulkan_Context *context,
              U64 size, VkBufferUsageFlags usage_flags,
              VkMemoryPropertyFlags memory_property_flags);

void
copy_data_to_buffer(Vulkan_Context *context, Vulkan_Buffer *buffer,
                    U64 offset, void *data, U64 size);

void
destroy_buffer(Vulkan_Buffer *buffer,
               VkDevice logical_device);