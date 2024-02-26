#include "vulkan_utils.h"
#include "vulkan_swapchain.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize.h>

#include "vulkan_renderer.h"
#include "vulkan_utils.h"

#include "core/engine.h"
#include "core/memory.h"

S32 find_memory_type_index(VkMemoryRequirements memory_requirements, VkMemoryPropertyFlags memory_property_flags, Vulkan_Context *context)
{
    S32 result = -1;

    for (U32 memory_type_index = 0; memory_type_index < context->physical_device_memory_properties.memoryTypeCount; memory_type_index++)
    {
        if (((1 << memory_type_index) & memory_requirements.memoryTypeBits))
        {
            // todo(amer): we should track how much memory we allocated from heaps so allocations don't fail
            const VkMemoryType *memory_type = &context->physical_device_memory_properties.memoryTypes[memory_type_index];
            if ((memory_type->propertyFlags & memory_property_flags) == memory_property_flags)
            {
                result = (S32)memory_type_index;
                break;
            }
        }
    }

    return result;
}

VkSampleCountFlagBits get_sample_count(U32 sample_count)
{
    switch (sample_count)
    {
        case 1: return VK_SAMPLE_COUNT_1_BIT;
        case 2: return VK_SAMPLE_COUNT_2_BIT;
        case 4: return VK_SAMPLE_COUNT_4_BIT;
        case 8: return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;

        default:
        {
            HE_ASSERT(false);
        }
    }

    return VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
}

VkPresentModeKHR pick_present_mode(bool vsync, Vulkan_Swapchain_Support *swapchain_support)
{
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;

    if (vsync)
    {
        if (is_present_mode_supported(swapchain_support, VK_PRESENT_MODE_FIFO_RELAXED_KHR))
        {
            present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        }
        else if (is_present_mode_supported(swapchain_support, VK_PRESENT_MODE_FIFO_KHR))
        {
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
        }
    }
    else
    {
        if (is_present_mode_supported(swapchain_support, VK_PRESENT_MODE_MAILBOX_KHR))
        {
            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        }
    }

    return present_mode;
}

//
// Images
//

VkFormat get_texture_format(Texture_Format texture_format)
{
    switch (texture_format)
    {
        case Texture_Format::R8G8B8A8_SRGB:
        {
            return VK_FORMAT_R8G8B8A8_SRGB;
        } break;

        case Texture_Format::B8G8R8A8_SRGB:
        {
            return VK_FORMAT_B8G8R8A8_SRGB;
        } break;

        case Texture_Format::DEPTH_F32_STENCIL_U8:
        {
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        } break;

        default:
        {
            HE_ASSERT(!"unsupported texture format");
        } break;
    }

    return VK_FORMAT_UNDEFINED;
}

void transtion_image_to_layout(VkCommandBuffer command_buffer, VkImage image, U32 mip_levels, U32 layer_count, VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mip_levels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layer_count;

    VkPipelineStageFlags source_stage = 0;
    VkPipelineStageFlags destination_stage = 0;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;

        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else
    {
        HE_ASSERT(false);
    }

    vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void copy_data_to_image(Vulkan_Context *context, Vulkan_Image *image, U32 width, U32 height, U32 mip_levels, U32 layer_count, VkFormat format, Array_View< void * > data, Allocation_Group *allocation_group)
{
    HE_ASSERT(context);
    HE_ASSERT(image);
    HE_ASSERT(width);
    HE_ASSERT(height);

    Renderer_State *renderer_state = context->renderer_state;

    VkCommandBufferAllocateInfo command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    command_buffer_allocate_info.commandPool = context->upload_textures_command_pool;
    command_buffer_allocate_info.commandBufferCount = 1;
    command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VkCommandBuffer command_buffer = {};
    vkAllocateCommandBuffers(context->logical_device, &command_buffer_allocate_info, &command_buffer); // @Leak
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
    transtion_image_to_layout(command_buffer, image->handle, mip_levels, layer_count, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // todo(amer): only supporting RGBA for now.
    U64 size = (U64)width * (U64)height * sizeof(U32);

    for (U32 layer_index = 0; layer_index < layer_count; layer_index++)
    {
        U64 offset = (U8 *)data[layer_index] - renderer_state->transfer_allocator.base;

        VkBufferImageCopy region = {};
        region.bufferOffset = offset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = layer_index;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { width, height, 1 };

        Renderer_State *renderer_state = context->renderer_state;
        Vulkan_Buffer *transfer_buffer = &context->buffers[renderer_state->transfer_buffer.index];
        vkCmdCopyBufferToImage(command_buffer, transfer_buffer->handle, image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.image = image->handle;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = layer_index;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;

        VkFormatProperties format_properties;
        vkGetPhysicalDeviceFormatProperties(context->physical_device, format, &format_properties);
        HE_ASSERT((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT));

        U32 mip_width = width;
        U32 mip_height = height;

        for (U32 mip_index = 1; mip_index < mip_levels; mip_index++)
        {
            barrier.subresourceRange.baseMipLevel = mip_index - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkImageBlit blit = {};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { (S32)mip_width, (S32)mip_height, 1 };
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = mip_index - 1;
            blit.srcSubresource.baseArrayLayer = layer_index;
            blit.srcSubresource.layerCount = 1;

            U32 new_mip_width = mip_width > 1 ? (mip_width / 2) : 1;
            U32 new_mip_height = mip_height > 1 ? (mip_height / 2) : 1;
            mip_width = new_mip_width;
            mip_height = new_mip_height;

            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = { (S32)new_mip_width,  (S32)new_mip_height, 1 };

            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = mip_index;
            blit.dstSubresource.baseArrayLayer = layer_index;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(command_buffer, image->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        barrier.subresourceRange.baseMipLevel = mip_levels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkEndCommandBuffer(command_buffer);

    VkCommandBufferSubmitInfo command_buffer_submit_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    command_buffer_submit_info.commandBuffer = command_buffer;

    VkSubmitInfo2KHR submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &command_buffer_submit_info;

    VkSemaphoreSubmitInfoKHR semaphore_submit_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };

    if (allocation_group)
    {
        allocation_group->target_value++;
        Vulkan_Semaphore *vulkan_semaphore = &context->semaphores[allocation_group->semaphore.index];
        
        semaphore_submit_info.semaphore = vulkan_semaphore->handle;
        semaphore_submit_info.value = allocation_group->target_value;
        semaphore_submit_info.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;

        submit_info.signalSemaphoreInfoCount = 1;
        submit_info.pSignalSemaphoreInfos = &semaphore_submit_info;
    }
    
    context->vkQueueSubmit2KHR(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
}

void destroy_image(Vulkan_Image *image, Vulkan_Context *context)
{
    vkDestroyImageView(context->logical_device, image->view, nullptr);

#if 0
    if (image->memory)
    {
        vkFreeMemory(context->logical_device, image->memory, nullptr);
    }
    
    vkDestroyImage(context->logical_device, image->handle, nullptr);
    
    image->memory = VK_NULL_HANDLE;
#endif

    vmaDestroyImage(context->allocator, image->handle, image->allocation);

    image->handle = VK_NULL_HANDLE;
    image->view = VK_NULL_HANDLE;
}