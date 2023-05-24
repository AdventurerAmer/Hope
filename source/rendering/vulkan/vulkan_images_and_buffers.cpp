#include "vulkan_images_and_buffers.h"
#include "vulkan.h"

internal_function S32
find_memory_type_index(Vulkan_Context *context,
                       VkMemoryRequirements memory_requirements,
                       VkMemoryPropertyFlags memory_property_flags)
{
    S32 result = -1;

    for (U32 memory_type_index = 0;
        memory_type_index < context->physical_device_memory_properties.memoryTypeCount;
        memory_type_index++)
    {
        if (((1 << memory_type_index) & memory_requirements.memoryTypeBits))
        {
            const VkMemoryType* memory_type =
                &context->physical_device_memory_properties.memoryTypes[memory_type_index];
            if ((memory_type->propertyFlags & memory_property_flags) == memory_property_flags)
            {
                result = (S32)memory_type_index;
            }
        }
    }

    return result;
}

//
// buffers
//

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
copy_buffer(Vulkan_Context *context, Vulkan_Buffer *src_buffer, Vulkan_Buffer *dst_buffer, void *data, U64 size)
{
    Assert(context);
    Assert(src_buffer);
    Assert(dst_buffer);
    Assert(data);
    Assert(size);
    Assert(size <= src_buffer->size && size <= dst_buffer->size);

    copy_memory(src_buffer->data, data, size);

    // todo(amer): check if graphics queue families always does transfer
    VkCommandBuffer command_buffer = context->graphics_command_buffers[0];
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer,
                         &command_buffer_begin_info);

    VkBufferCopy copy_region = {};
    copy_region.srcOffset = 0;
    copy_region.dstOffset = 0;
    copy_region.size = size;

    vkCmdCopyBuffer(command_buffer, src_buffer->handle, dst_buffer->handle, 1, &copy_region);
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(context->graphics_queue);
}

void
destroy_buffer(Vulkan_Buffer *buffer,
               VkDevice logical_device)
{
    vkFreeMemory(logical_device, buffer->memory, nullptr);
    vkDestroyBuffer(logical_device, buffer->handle, nullptr);
}

//
// images
//

internal_function void
transtion_image_to_layout(VkImage image,
                          VkCommandBuffer command_buffer,
                          VkImageLayout old_layout,
                          VkImageLayout new_layout)
{
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags destination_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
             new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else
    {
        Assert(false);
    }

    vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool
create_image(Vulkan_Image *image, Vulkan_Context *context,
             U32 width, U32 height, VkFormat format,
             VkImageTiling tiling, VkImageUsageFlags usage,
             VkImageAspectFlags aspect_flags,
             VkMemoryPropertyFlags properties)
{
    VkImageCreateInfo image_create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.extent.width = width;
    image_create_info.extent.height = height;
    image_create_info.extent.depth = 1;
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.format = format;
    image_create_info.tiling = tiling;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create_info.usage = usage;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.flags = 0;

    CheckVkResult(vkCreateImage(context->logical_device, &image_create_info, nullptr, &image->handle));

    VkMemoryRequirements memory_requirements = {};
    vkGetImageMemoryRequirements(context->logical_device, image->handle, &memory_requirements);

    VkMemoryAllocateInfo memory_allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memory_allocate_info.allocationSize = memory_requirements.size;
    memory_allocate_info.memoryTypeIndex = find_memory_type_index(context, memory_requirements, properties);

    CheckVkResult(vkAllocateMemory(context->logical_device, &memory_allocate_info, nullptr, &image->memory));
    vkBindImageMemory(context->logical_device, image->handle, image->memory, 0);
    image->size = memory_requirements.size;
    image->data = nullptr;
    image->width = width;
    image->height = height;

    VkImageViewCreateInfo image_view_create_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    image_view_create_info.image = image->handle;
    image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_create_info.format = format;
    image_view_create_info.subresourceRange.aspectMask = aspect_flags;
    image_view_create_info.subresourceRange.baseMipLevel = 0;
    image_view_create_info.subresourceRange.levelCount = 1;
    image_view_create_info.subresourceRange.baseArrayLayer = 0;
    image_view_create_info.subresourceRange.layerCount = 1;
    CheckVkResult(vkCreateImageView(context->logical_device, &image_view_create_info, nullptr, &image->view));

    return true;
}

void
copy_buffer_to_image(Vulkan_Context *context, Vulkan_Buffer *buffer, Vulkan_Image *image, void *data, U64 size)
{
    Assert(context);
    Assert(buffer);
    Assert(data);
    Assert(size);
    Assert(size <= buffer->size && size <= image->size);

    copy_memory(buffer->data, data, size);

    // todo(amer): check if graphics queue families always does transfer
    VkCommandBuffer command_buffer = context->graphics_command_buffers[0];
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer,
                         &command_buffer_begin_info);

    transtion_image_to_layout(image->handle,
                              command_buffer,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { image->width, image->height, 1 };

    vkCmdCopyBufferToImage(command_buffer, buffer->handle,
                           image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    transtion_image_to_layout(image->handle,
                              command_buffer,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(context->graphics_queue);
}

void
destroy_image(Vulkan_Image *image, Vulkan_Context *context)
{
    vkDestroyImageView(context->logical_device, image->view, nullptr);
    vkFreeMemory(context->logical_device, image->memory, nullptr);
    vkDestroyImage(context->logical_device, image->handle, nullptr);
}