#include "vulkan_buffer.h"
#include "vulkan_renderer.h"

bool
create_buffer(Vulkan_Buffer *buffer, Vulkan_Context *context,
              U64 size, VkBufferUsageFlags usage_flags,
              VkMemoryPropertyFlags memory_property_flags)
{
    Assert(buffer);
    Assert(context);
    Assert(size);

    VkBufferCreateInfo buffer_create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_create_info.size = size;
    buffer_create_info.usage = usage_flags;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_create_info.flags = 0;

    CheckVkResult(vkCreateBuffer(context->logical_device, &buffer_create_info, nullptr, &buffer->handle));

    VkMemoryRequirements memory_requirements = {};
    vkGetBufferMemoryRequirements(context->logical_device, buffer->handle, &memory_requirements);

    S32 memory_type_index = find_memory_type_index(context, memory_requirements, memory_property_flags);
    Assert(memory_type_index != -1);

    VkMemoryAllocateInfo memory_allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memory_allocate_info.allocationSize = memory_requirements.size;
    memory_allocate_info.memoryTypeIndex = memory_type_index;

    CheckVkResult(vkAllocateMemory(context->logical_device, &memory_allocate_info,
                                   nullptr, &buffer->memory));

    CheckVkResult(vkBindBufferMemory(context->logical_device,
                                     buffer->handle, buffer->memory, 0));

    if ((memory_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        vkMapMemory(context->logical_device, buffer->memory, 0, size, 0, &buffer->data);
    }
    buffer->size = size;
    return true;
}

void
copy_data_to_buffer(Vulkan_Context *context, Vulkan_Buffer *buffer,
                    U64 offset, void *data, U64 size)
{
    Assert(context);
    Assert(buffer);
    Assert(data);
    Assert(size);
    Assert(size <= context->transfer_buffer.size && offset + size <= buffer->size);

    copy_memory(context->transfer_buffer.data, data, size);
    VkCommandBuffer command_buffer = context->transfer_command_buffer;
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info =
        { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer,
                         &command_buffer_begin_info);

    VkBufferCopy copy_region = {};
    copy_region.srcOffset = 0;
    copy_region.dstOffset = offset;
    copy_region.size = size;

    vkCmdCopyBuffer(command_buffer, context->transfer_buffer.handle, buffer->handle, 1, &copy_region);
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(context->transfer_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(context->transfer_queue);
}

void
destroy_buffer(Vulkan_Buffer *buffer,
               VkDevice logical_device)
{
    vkFreeMemory(logical_device, buffer->memory, nullptr);
    vkDestroyBuffer(logical_device, buffer->handle, nullptr);
}