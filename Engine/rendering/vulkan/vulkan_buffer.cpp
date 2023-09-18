#include "vulkan_buffer.h"
#include "vulkan_renderer.h"

bool create_buffer(Vulkan_Buffer *buffer, Vulkan_Context *context,
                   U64 size, VkBufferUsageFlags usage_flags,
                   VkMemoryPropertyFlags memory_property_flags)
{
    HE_ASSERT(buffer);
    HE_ASSERT(context);
    HE_ASSERT(size);

    VkBufferCreateInfo buffer_create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_create_info.size = size;
    buffer_create_info.usage = usage_flags;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_create_info.flags = 0;

    HE_CHECK_VKRESULT(vkCreateBuffer(context->logical_device, &buffer_create_info, nullptr, &buffer->handle));

    VkMemoryRequirements memory_requirements = {};
    vkGetBufferMemoryRequirements(context->logical_device, buffer->handle, &memory_requirements);

    S32 memory_type_index = find_memory_type_index(memory_requirements, memory_property_flags);
    HE_ASSERT(memory_type_index != -1);

    VkMemoryAllocateInfo memory_allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memory_allocate_info.allocationSize = memory_requirements.size;
    memory_allocate_info.memoryTypeIndex = memory_type_index;

    HE_CHECK_VKRESULT(vkAllocateMemory(context->logical_device, &memory_allocate_info, nullptr, &buffer->memory));
    HE_CHECK_VKRESULT(vkBindBufferMemory(context->logical_device, buffer->handle, buffer->memory, 0));

    if ((memory_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        vkMapMemory(context->logical_device, buffer->memory, 0, size, 0, &buffer->data);
    }
    buffer->size = size;
    return true;
}

void destroy_buffer(Vulkan_Buffer *buffer, VkDevice logical_device)
{
    vkFreeMemory(logical_device, buffer->memory, nullptr);
    vkDestroyBuffer(logical_device, buffer->handle, nullptr);
}