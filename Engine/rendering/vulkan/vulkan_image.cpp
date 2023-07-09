#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize.h>

#include "vulkan_image.h"
#include "vulkan_renderer.h"
#include "core/memory.h"

internal_function void
transtion_image_to_layout(Vulkan_Image *image,
                          VkCommandBuffer command_buffer,
                          VkImageLayout old_layout,
                          VkImageLayout new_layout)
{
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image->handle;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image->mip_levels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags source_stage = 0;
    VkPipelineStageFlags destination_stage = 0;

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
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

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
             VkMemoryPropertyFlags properties,
             bool mipmapping/*= false*/,
             VkSampleCountFlagBits samples/*= VK_SAMPLE_COUNT_1_BIT*/)
{

    U32 mip_levels = 1;

    if (mipmapping)
    {
        mip_levels = (U32)glm::floor(glm::log2((F32)glm::max(width, height)));
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkImageCreateInfo image_create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.extent.width = width;
    image_create_info.extent.height = height;
    image_create_info.extent.depth = 1;
    image_create_info.mipLevels = mip_levels;
    image_create_info.arrayLayers = 1;
    image_create_info.format = format;
    image_create_info.tiling = tiling;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create_info.usage = usage;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_create_info.samples = samples;
    image_create_info.flags = 0;

    CheckVkResult(vkCreateImage(context->logical_device, &image_create_info,
                                nullptr, &image->handle));

    VkMemoryRequirements memory_requirements = {};
    vkGetImageMemoryRequirements(context->logical_device, image->handle, &memory_requirements);

    VkMemoryAllocateInfo memory_allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memory_allocate_info.allocationSize = memory_requirements.size;
    memory_allocate_info.memoryTypeIndex = find_memory_type_index(context,
                                                                  memory_requirements,
                                                                  properties);

    CheckVkResult(vkAllocateMemory(context->logical_device, &memory_allocate_info,
                                   nullptr, &image->memory));

    vkBindImageMemory(context->logical_device, image->handle, image->memory, 0);
    image->mip_levels = mip_levels;
    image->size = memory_requirements.size;
    image->format = format;
    image->data = nullptr;

    VkImageViewCreateInfo image_view_create_info =
        { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    image_view_create_info.image = image->handle;
    image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_create_info.format = format;
    image_view_create_info.subresourceRange.aspectMask = aspect_flags;
    image_view_create_info.subresourceRange.baseMipLevel = 0;
    image_view_create_info.subresourceRange.levelCount = mip_levels;
    image_view_create_info.subresourceRange.baseArrayLayer = 0;
    image_view_create_info.subresourceRange.layerCount = 1;
    CheckVkResult(vkCreateImageView(context->logical_device, &image_view_create_info,
                                    nullptr, &image->view));

    VkSamplerCreateInfo sampler_create_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler_create_info.minFilter = VK_FILTER_LINEAR;
    sampler_create_info.magFilter = VK_FILTER_LINEAR;
    sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_create_info.anisotropyEnable = VK_TRUE;
    sampler_create_info.maxAnisotropy = context->physical_device_properties.limits.maxSamplerAnisotropy;
    sampler_create_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_create_info.unnormalizedCoordinates = VK_FALSE;
    sampler_create_info.compareEnable = VK_FALSE;
    sampler_create_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_create_info.mipLodBias = 0.0f;
    sampler_create_info.minLod = 0.0f;
    sampler_create_info.maxLod = (F32)image->mip_levels;
    CheckVkResult(vkCreateSampler(context->logical_device, &sampler_create_info, nullptr, &image->sampler));

    return true;
}

#define CPU_SIDE_MIPMAPS 0

void
copy_data_to_image(Vulkan_Context *context,
                   Vulkan_Image *image,
                   U32 width, U32 height,
                   void *data, U64 size)
{
    Assert(context);
    Assert(data);
    Assert(size);
    Assert(size <= context->transfer_buffer.size && size <= image->size);

#if CPU_SIDE_MIPMAPS

    VkCommandBufferAllocateInfo transfer_command_buffer_allocate_info =
        { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    transfer_command_buffer_allocate_info.commandPool = context->transfer_command_pool;
    transfer_command_buffer_allocate_info.commandBufferCount = 1;
    transfer_command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VkCommandBuffer tranfer_command_buffer = {};
    vkAllocateCommandBuffers(context->logical_device, &transfer_command_buffer_allocate_info, &tranfer_command_buffer);
    vkResetCommandBuffer(tranfer_command_buffer, 0);

    VkCommandBufferBeginInfo transfer_command_buffer_begin_info =
    { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    transfer_command_buffer_begin_info.flags = 0;
    transfer_command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(tranfer_command_buffer,
                         &transfer_command_buffer_begin_info);

    // note(amer): transtion_image_to_layout transitions all mipmaps levels
    transtion_image_to_layout(image,
                              tranfer_command_buffer,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    U32 current_width = width;
    U32 current_height = height;
    const U8 *src = (U8 *)data;

    for (U32 mip_level = 0; mip_level < image->mip_levels; mip_level++)
    {
        U32 current_mip_size = current_width * current_height * sizeof(U32);
        U8 *transfer_buffer_data = (U8 *)allocate(&context->transfer_arena, current_mip_size, alignof(U32));
        copy_memory(transfer_buffer_data, src, size);

        VkBufferImageCopy region = {};
        region.bufferOffset = transfer_buffer_data - context->transfer_arena.base;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mip_level;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { current_width, current_height, 1 };

        vkCmdCopyBufferToImage(tranfer_command_buffer,
                               context->transfer_buffer.handle,
                               image->handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &region);

        if (mip_level != image->mip_levels - 1)
        {
            U32 new_width = current_width / 2;
            U32 new_height = current_height / 2;

            U8 *dst = transfer_buffer_data;
            stbir_resize_uint8_generic((U8*)src, current_width, current_height, 0,
                                       dst, new_width, new_height, 0, 4, 0, 0,
                                       STBIR_EDGE_ZERO, STBIR_FILTER_BOX, STBIR_COLORSPACE_SRGB, 0);

            current_width = new_width;
            current_height = new_height;
            src = dst;
        }
    }

    VkImageMemoryBarrier post_transfer_image_memory_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    post_transfer_image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    post_transfer_image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post_transfer_image_memory_barrier.srcQueueFamilyIndex = context->transfer_queue_family_index;
    post_transfer_image_memory_barrier.dstQueueFamilyIndex = context->graphics_queue_family_index;
    post_transfer_image_memory_barrier.image = image->handle;
    post_transfer_image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    post_transfer_image_memory_barrier.subresourceRange.baseMipLevel = 0;
    post_transfer_image_memory_barrier.subresourceRange.levelCount = image->mip_levels;
    post_transfer_image_memory_barrier.subresourceRange.baseArrayLayer = 0;
    post_transfer_image_memory_barrier.subresourceRange.layerCount = 1;
    post_transfer_image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    post_transfer_image_memory_barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(tranfer_command_buffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &post_transfer_image_memory_barrier);

    vkEndCommandBuffer(tranfer_command_buffer);

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &tranfer_command_buffer;

    vkQueueSubmit(context->transfer_queue, 1, &submit_info, VK_NULL_HANDLE);

    VkCommandBufferAllocateInfo graphics_command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    graphics_command_buffer_allocate_info.commandPool = context->graphics_command_pool;
    graphics_command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    graphics_command_buffer_allocate_info.commandBufferCount = 1;

    VkCommandBuffer graphics_command_buffer = {};
    vkAllocateCommandBuffers(context->logical_device, &graphics_command_buffer_allocate_info, &graphics_command_buffer);
    vkResetCommandBuffer(graphics_command_buffer, 0);

    VkCommandBufferBeginInfo graphics_command_buffer_begin_info =
        { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    graphics_command_buffer_begin_info.flags = 0;
    graphics_command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(graphics_command_buffer,
                         &graphics_command_buffer_begin_info);

    VkImageMemoryBarrier post_copy_image_memory_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    post_copy_image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    post_copy_image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post_copy_image_memory_barrier.srcQueueFamilyIndex = context->transfer_queue_family_index;
    post_copy_image_memory_barrier.dstQueueFamilyIndex = context->graphics_queue_family_index;
    post_copy_image_memory_barrier.image = image->handle;
    post_copy_image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    post_copy_image_memory_barrier.subresourceRange.baseMipLevel = 0;
    post_copy_image_memory_barrier.subresourceRange.levelCount = image->mip_levels;
    post_copy_image_memory_barrier.subresourceRange.baseArrayLayer = 0;
    post_copy_image_memory_barrier.subresourceRange.layerCount = 1;
    post_copy_image_memory_barrier.srcAccessMask = 0;
    post_copy_image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(graphics_command_buffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &post_copy_image_memory_barrier);

    vkEndCommandBuffer(graphics_command_buffer);

    submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &graphics_command_buffer;
    vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);

#else
    VkCommandBufferAllocateInfo command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    command_buffer_allocate_info.commandPool = context->graphics_command_pool;
    command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_allocate_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = {};
    vkAllocateCommandBuffers(context->logical_device, &command_buffer_allocate_info, &command_buffer);
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info =
        { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer,
                         &command_buffer_begin_info);

    transtion_image_to_layout(image,
                              command_buffer,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    U8 *transfer_buffer_data = (U8 *)allocate(&context->transfer_arena, size, alignof(U32));
    copy_memory(transfer_buffer_data, data, size);

    VkBufferImageCopy region = {};
    region.bufferOffset = transfer_buffer_data - context->transfer_arena.base;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(command_buffer, context->transfer_buffer.handle,
                           image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = image->handle;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(context->physical_device, image->format, &format_properties);
    Assert((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT));

    U32 mip_width = width;
    U32 mip_height = height;

    for (U32 mip_index = 1; mip_index < image->mip_levels; mip_index++)
    {
        barrier.subresourceRange.baseMipLevel = mip_index - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

        VkImageBlit blit = {};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { (S32)mip_width, (S32)mip_height, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = mip_index - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        U32 new_mip_width = mip_width > 1 ? mip_width / 2 : 1;
        U32 new_mip_height = mip_height > 1 ? mip_height / 2 : 1;

        mip_width = new_mip_width;
        mip_height = new_mip_height;

        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { (S32)new_mip_width,  (S32)new_mip_height, 1 };

        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = mip_index;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(command_buffer, image->handle,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                            0, nullptr,
                            0, nullptr,
                            1, &barrier);
    }

    barrier.subresourceRange.baseMipLevel = image->mip_levels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);

    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
#endif
}

void
destroy_image(Vulkan_Image *image, Vulkan_Context *context)
{
    vkDestroyImageView(context->logical_device, image->view, nullptr);
    vkFreeMemory(context->logical_device, image->memory, nullptr);
    vkDestroyImage(context->logical_device, image->handle, nullptr);
    vkDestroySampler(context->logical_device, image->sampler, nullptr);
}