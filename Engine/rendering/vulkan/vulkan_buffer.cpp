#include "vulkan_buffer.h"
#include "vulkan_renderer.h"

bool
create_buffer(Vulkan_Buffer *buffer, Vulkan_Context *context,
              U64 size, VkBufferUsageFlags usage_flags,
              VkMemoryPropertyFlags memory_property_flags)
{
    HOPE_Assert(buffer);
    HOPE_Assert(context);
    HOPE_Assert(size);

    VkBufferCreateInfo buffer_create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_create_info.size = size;
    buffer_create_info.usage = usage_flags;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_create_info.flags = 0;

    HOPE_CheckVkResult(vkCreateBuffer(context->logical_device, &buffer_create_info, nullptr, &buffer->handle));

    VkMemoryRequirements memory_requirements = {};
    vkGetBufferMemoryRequirements(context->logical_device, buffer->handle, &memory_requirements);

    S32 memory_type_index = find_memory_type_index(context, memory_requirements, memory_property_flags);
    HOPE_Assert(memory_type_index != -1);

    VkMemoryAllocateInfo memory_allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memory_allocate_info.allocationSize = memory_requirements.size;
    memory_allocate_info.memoryTypeIndex = memory_type_index;

    HOPE_CheckVkResult(vkAllocateMemory(context->logical_device, &memory_allocate_info,
                                        nullptr, &buffer->memory));

    HOPE_CheckVkResult(vkBindBufferMemory(context->logical_device,
                                          buffer->handle, buffer->memory, 0));

    if ((memory_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        vkMapMemory(context->logical_device, buffer->memory, 0, size, 0, &buffer->data);
    }
    buffer->size = size;
    return true;
}

void copy_data_to_buffer_from_buffer(Vulkan_Context *context,
                                     Vulkan_Buffer *dst, U64 dst_offset,
                                     Vulkan_Buffer *src, U64 src_offset,
                                     U64 size)
{
    HOPE_Assert(context);
    HOPE_Assert(dst);
    HOPE_Assert(src);
    HOPE_Assert(size);

    VkCommandBufferAllocateInfo command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    command_buffer_allocate_info.commandPool = context->transfer_command_pool;
    command_buffer_allocate_info.commandBufferCount = 1;
    command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VkCommandBuffer command_buffer = {};
    vkAllocateCommandBuffers(context->logical_device, &command_buffer_allocate_info, &command_buffer);
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info =
        { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer,
                         &command_buffer_begin_info);

    VkBufferCopy copy_region = {};
    copy_region.srcOffset = src_offset;
    copy_region.dstOffset = dst_offset;
    copy_region.size = size;

    vkCmdCopyBuffer(command_buffer, src->handle, dst->handle, 1, &copy_region);
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(context->transfer_queue, 1, &submit_info, VK_NULL_HANDLE);
}

void
destroy_buffer(Vulkan_Buffer *buffer,
               VkDevice logical_device)
{
    vkFreeMemory(logical_device, buffer->memory, nullptr);
    vkDestroyBuffer(logical_device, buffer->handle, nullptr);
}