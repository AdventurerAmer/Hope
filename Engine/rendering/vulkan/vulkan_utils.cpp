#include "vulkan_utils.h"
#include "vulkan_swapchain.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize.h>

#include "vulkan_renderer.h"
#include "vulkan_utils.h"

#include "core/engine.h"
#include "core/memory.h"

#include "rendering/renderer_utils.h"

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
        case Texture_Format::R8G8B8A8_UNORM:
        {
            return VK_FORMAT_R8G8B8A8_UNORM;
        } break;

        case Texture_Format::R8G8B8_UNORM:
        {
            return VK_FORMAT_R8G8B8_UNORM;
        } break;

        case Texture_Format::R8G8B8A8_SRGB:
        {
            return VK_FORMAT_R8G8B8A8_SRGB;
        } break;

        case Texture_Format::B8G8R8A8_SRGB:
        {
            return VK_FORMAT_B8G8R8A8_SRGB;
        } break;

        case Texture_Format::B8G8R8A8_UNORM:
        {
            return VK_FORMAT_B8G8R8A8_UNORM;
        } break;

        case Texture_Format::R32G32B32A32_SFLOAT:
        {
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        } break;

        case Texture_Format::R32G32B32_SFLOAT:
        {
            return VK_FORMAT_R32G32B32_SFLOAT;
        } break;

        case Texture_Format::R16G16B16A16_SFLOAT:
        {
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        } break;

        case Texture_Format::R32_SINT:
        {
            return VK_FORMAT_R32_SINT;
        } break;

        case Texture_Format::R32_UINT:
        {
            return VK_FORMAT_R32_UINT;
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

VkImageLayout get_image_layout(Resource_State resource_state, Texture_Format format)
{
    using enum Resource_State;

    bool is_color = is_color_format(format);

    switch (resource_state)
    {
        case UNDEFINED: return VK_IMAGE_LAYOUT_UNDEFINED;
        case GENERAL: return VK_IMAGE_LAYOUT_GENERAL;
        case COPY_SRC: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case COPY_DST: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case RENDER_TARGET: return is_color ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case SHADER_READ_ONLY: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case PRESENT: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        default:
        {
            HE_ASSERT(!"unsupported resource state");
        } break;
    }

    return VK_IMAGE_LAYOUT_UNDEFINED;
}

VkAccessFlags get_access_flags(Resource_State resource_state, Texture_Format format)
{
    using enum Resource_State;

    bool is_color = is_color_format(format);

    switch (resource_state)
    {
        case UNDEFINED:
            return 0;

        case GENERAL: return VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;

        case COPY_SRC: return VK_ACCESS_TRANSFER_READ_BIT;
        case COPY_DST: return VK_ACCESS_TRANSFER_WRITE_BIT;

        case RENDER_TARGET:
        {
            if (is_color)
            {
                return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            }
            else
            {
                return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }
        } break;

        case SHADER_READ_ONLY: return VK_ACCESS_SHADER_READ_BIT;
        case PRESENT: return VK_ACCESS_MEMORY_READ_BIT;

        default:
        {
            HE_ASSERT(!"unsupported resource state");
        } break;
    }

    return 0;
}

VkPipelineStageFlags get_pipeline_stage_flags(VkAccessFlags access_flags, bool compute_only)
{
    VkPipelineStageFlags result = 0;

    if ((access_flags & (VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT)) != 0)
    {
        if (compute_only)
        {
            result |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }
        else
        {
            result |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }
    }
    
    if ((access_flags & (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0)
    {
        result |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }

    if ((access_flags & (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
    {
        result |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }

    if ((access_flags & VK_ACCESS_INDIRECT_COMMAND_READ_BIT) != 0)
    {
        result |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    }

    if ((access_flags & (VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT)) != 0)
    {
        result |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    }

    if ((access_flags & (VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT)) != 0)
    {
        result |= VK_PIPELINE_STAGE_HOST_BIT;
    }

    if (result == 0)
    {
        result = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

    return result;
}

void transtion_image_to_layout(VkCommandBuffer command_buffer, VkImage image, U32 base_mip_level, U32 mip_levels, U32 base_layer, U32 layer_count, VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = base_mip_level;
    barrier.subresourceRange.levelCount = mip_levels;
    barrier.subresourceRange.baseArrayLayer = base_layer;
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
    else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_GENERAL)
    {
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        HE_ASSERT(false);
    }

    vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void copy_data_to_image(Vulkan_Context *context, Vulkan_Command_Buffer *command_buffer, Vulkan_Image *image, const Texture_Descriptor &texture_descriptor, U32 mip_levels)
{
    HE_ASSERT(context);
    HE_ASSERT(image);

    Renderer_State *renderer_state = context->renderer_state;
    Vulkan_Thread_State *thread_state = get_thread_state(context);

    transtion_image_to_layout(command_buffer->handle, image->handle, 0, mip_levels, 0, texture_descriptor.layer_count, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // todo(amer): only supporting RGBA for now.
    U64 size = (U64)texture_descriptor.width * (U64)texture_descriptor.height * sizeof(U32);

    for (U32 layer_index = 0; layer_index < texture_descriptor.layer_count; layer_index++)
    {
        U64 offset = (U8 *)texture_descriptor.data_array[layer_index] - renderer_state->transfer_allocator.base;

        VkBufferImageCopy region = {};
        region.bufferOffset = offset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = layer_index;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { texture_descriptor.width, texture_descriptor.height, 1 };

        Renderer_State *renderer_state = context->renderer_state;
        Vulkan_Buffer *transfer_buffer = &context->buffers[renderer_state->transfer_buffer.index];
        vkCmdCopyBufferToImage(command_buffer->handle, transfer_buffer->handle, image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.image = image->handle;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = layer_index;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;

        VkFormatProperties format_properties;
        vkGetPhysicalDeviceFormatProperties(context->physical_device, get_texture_format(texture_descriptor.format), &format_properties);
        HE_ASSERT((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT));

        U32 mip_width = texture_descriptor.width;
        U32 mip_height = texture_descriptor.height;

        for (U32 mip_index = 1; mip_index < mip_levels; mip_index++)
        {
            barrier.subresourceRange.baseMipLevel = mip_index - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(command_buffer->handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

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

            vkCmdBlitImage(command_buffer->handle, image->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(command_buffer->handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        barrier.subresourceRange.baseMipLevel = mip_levels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer->handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}

Vulkan_Thread_State *get_thread_state(Vulkan_Context *context)
{
    U32 thread_id = platform_get_current_thread_id();
    auto it = find(&context->thread_states, thread_id);
    if (is_valid(it))
    {
        return it.value;
    }

    S32 slot_index = insert(&context->thread_states, thread_id);
    HE_ASSERT(slot_index != -1);

    Vulkan_Thread_State *thread_state = &context->thread_states.values[slot_index];

    VkCommandPoolCreateInfo graphics_command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    graphics_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    graphics_command_pool_create_info.queueFamilyIndex = context->graphics_queue_family_index;
    HE_CHECK_VKRESULT(vkCreateCommandPool(context->logical_device, &graphics_command_pool_create_info, &context->allocation_callbacks, &thread_state->graphics_command_pool));

    VkCommandPoolCreateInfo transfer_command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    transfer_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    transfer_command_pool_create_info.queueFamilyIndex = context->transfer_queue_family_index;
    HE_CHECK_VKRESULT(vkCreateCommandPool(context->logical_device, &transfer_command_pool_create_info, &context->allocation_callbacks, &thread_state->transfer_command_pool));

    return thread_state;
}

Vulkan_Command_Buffer begin_one_use_command_buffer(Vulkan_Context *context)
{
    Vulkan_Thread_State *thread_state = get_thread_state(context);

    VkCommandBufferAllocateInfo command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    command_buffer_allocate_info.commandPool = thread_state->graphics_command_pool;
    command_buffer_allocate_info.commandBufferCount = 1;
    command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VkCommandBuffer command_buffer = {};
    vkAllocateCommandBuffers(context->logical_device, &command_buffer_allocate_info, &command_buffer);
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    return
    {
        .pool = thread_state->graphics_command_pool,
        .handle = command_buffer
    };
}

void end_one_use_command_buffer(Vulkan_Context *context, Vulkan_Command_Buffer *command_buffer, Upload_Request_Handle upload_request_handle)
{
    Renderer_State *renderer_state = context->renderer_state;

    vkEndCommandBuffer(command_buffer->handle);

    VkCommandBufferSubmitInfo command_buffer_submit_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    command_buffer_submit_info.commandBuffer = command_buffer->handle;

    VkSubmitInfo2KHR submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &command_buffer_submit_info;

    if (is_valid_handle(&renderer_state->upload_requests, upload_request_handle))
    {
        Upload_Request *upload_request = renderer_get_upload_request(upload_request_handle);
        upload_request->target_value++;

        Vulkan_Upload_Request *vulkan_upload_request = &context->upload_requests[upload_request_handle.index];
        vulkan_upload_request->graphics_command_buffer = command_buffer->handle;
        vulkan_upload_request->graphics_command_pool = command_buffer->pool;

        vulkan_upload_request->transfer_command_buffer = VK_NULL_HANDLE;
        vulkan_upload_request->transfer_command_pool = VK_NULL_HANDLE;

        Vulkan_Semaphore *vulkan_semaphore = &context->semaphores[upload_request->semaphore.index];

        VkSemaphoreSubmitInfoKHR semaphore_submit_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        semaphore_submit_info.semaphore = vulkan_semaphore->handle;
        semaphore_submit_info.value = upload_request->target_value;
        semaphore_submit_info.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;

        submit_info.signalSemaphoreInfoCount = 1;
        submit_info.pSignalSemaphoreInfos = &semaphore_submit_info;

        context->vkQueueSubmit2KHR(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    }
    else
    {
        VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence fence = {};
        vkCreateFence(context->logical_device, &fence_create_info, &context->allocation_callbacks, &fence);

        context->vkQueueSubmit2KHR(context->graphics_queue, 1, &submit_info, fence);
        vkWaitForFences(context->logical_device, 1, &fence, VK_TRUE, HE_MAX_U64);

        vkDestroyFence(context->logical_device, fence, &context->allocation_callbacks);
        vkFreeCommandBuffers(context->logical_device, command_buffer->pool, 1, &command_buffer->handle);
    }
}