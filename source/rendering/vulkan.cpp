#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <string.h>

#include "vulkan.h"
#include "core/platform.h"
#include "core/debugging.h"
#include "core/memory.h"
#include "renderer.h"
#include "core/engine.h"

static Vulkan_Context vulkan_context;

internal_function VKAPI_ATTR VkBool32 VKAPI_CALL
vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                      VkDebugUtilsMessageTypeFlagsEXT message_type,
                      const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                      void *user_data)
{
    (void)message_severity;
    (void)message_type;
    (void)user_data;
    DebugPrintf(Rendering, Trace, "%s\n", callback_data->pMessage);
    Assert(message_severity != VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
    return VK_FALSE;
}

internal_function VkPhysicalDevice
pick_physical_device(VkInstance instance, VkSurfaceKHR surface, Memory_Arena *arena)
{
    Scoped_Temprary_Memory_Arena temp_arena(arena);

    U32 physical_device_count = 0;
    CheckVkResult(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));

    if (!physical_device_count)
    {
        return VK_NULL_HANDLE;
    }

    VkPhysicalDevice *physical_devices = AllocateArray(&temp_arena,
                                                        VkPhysicalDevice,
                                                        physical_device_count);
    Assert(physical_devices);

    CheckVkResult(vkEnumeratePhysicalDevices(instance,
                                             &physical_device_count,
                                             physical_devices));

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    U32 best_physical_device_score_so_far = 0;

    for (U32 physical_device_index = 0;
         physical_device_index < physical_device_count;
         physical_device_index++)
    {
        VkPhysicalDevice *current_physical_device = &physical_devices[physical_device_index];

        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(*current_physical_device, &properties);

        VkPhysicalDeviceFeatures features = {};
        vkGetPhysicalDeviceFeatures(*current_physical_device, &features);

        if (!features.samplerAnisotropy)
        {
            continue;
        }

        U32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(*current_physical_device,
                                                 &queue_family_count,
                                                 nullptr);

        bool can_physical_device_do_graphics = false;
        bool can_physical_device_present = false;

        VkQueueFamilyProperties *queue_families = AllocateArray(&temp_arena,
                                                                 VkQueueFamilyProperties,
                                                                 queue_family_count);

        vkGetPhysicalDeviceQueueFamilyProperties(*current_physical_device,
                                                 &queue_family_count,
                                                 queue_families);

        for (U32 queue_family_index = 0;
             queue_family_index < queue_family_count;
             queue_family_index++)
        {
            VkQueueFamilyProperties *queue_family = &queue_families[queue_family_index];

            if ((queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                can_physical_device_do_graphics = true;
            }

            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(*current_physical_device,
                                                 queue_family_index,
                                                 surface,
                                                 &present_support);

            if (present_support == VK_TRUE)
            {
                can_physical_device_present = true;
            }
        }

        if (can_physical_device_do_graphics && can_physical_device_present)
        {
            U32 score = 0;
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                score++;
            }
            if (score >= best_physical_device_score_so_far)
            {
                best_physical_device_score_so_far = score;
                physical_device = *current_physical_device;
            }
        }
    }

    return physical_device;
}

internal_function bool
init_swapchain_support(Vulkan_Context *context,
                       VkFormat *formats,
                       U32 format_count,
                       VkColorSpaceKHR color_space,
                       Memory_Arena *arena,
                       Vulkan_Swapchain_Support *swapchain_support)
{
    swapchain_support->surface_format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device,
                                         context->surface,
                                         &swapchain_support->surface_format_count,
                                         nullptr);

    Assert(swapchain_support->surface_format_count);

    swapchain_support->surface_formats = AllocateArray(arena,
                                                        VkSurfaceFormatKHR,
                                                        swapchain_support->surface_format_count);

    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device,
                                         context->surface,
                                         &swapchain_support->surface_format_count,
                                         swapchain_support->surface_formats);

    swapchain_support->present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device,
                                              context->surface,
                                              &swapchain_support->present_mode_count,
                                              nullptr);

    Assert(swapchain_support->present_mode_count);

    swapchain_support->present_modes = AllocateArray(arena,
                                                      VkPresentModeKHR,
                                                      swapchain_support->present_mode_count);

    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device,
                                              context->surface,
                                              &swapchain_support->present_mode_count,
                                              swapchain_support->present_modes);

    VkFormat format = swapchain_support->surface_formats[0].format;

    for (U32 format_index = 0;
         format_index < format_count;
         format_index++)
    {
        VkFormat desired_format = formats[format_index];
        bool found = false;

        for (U32 surface_format_index = 0;
             surface_format_index < swapchain_support->surface_format_count;
             surface_format_index++)
        {
            const VkSurfaceFormatKHR *surface_format = &swapchain_support->surface_formats[surface_format_index];

            if (surface_format->format == desired_format &&
                surface_format->colorSpace == color_space)
            {
                format = desired_format;
                found = true;
                break;
            }
        }

        if (found)
        {
            break;
        }
    }

    swapchain_support->format = format;
    return true;
}

internal_function S32 find_memory_type_index(Vulkan_Context *context,
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

internal_function bool
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

internal_function void
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

internal_function void destroy_image(Vulkan_Image *image, Vulkan_Context *context)
{
    vkDestroyImageView(context->logical_device, image->view, nullptr);
    vkFreeMemory(context->logical_device, image->memory, nullptr);
    vkDestroyImage(context->logical_device, image->handle, nullptr);
}

internal_function bool
create_swapchain(Vulkan_Context *context,
                 U32 width, U32 height,
                 U32 min_image_count,
                 VkPresentModeKHR present_mode,
                 Vulkan_Swapchain *swapchain)
{
    Assert(context);
    Assert(width);
    Assert(height);
    Assert(min_image_count);
    Assert(swapchain);

    const Vulkan_Swapchain_Support *swapchain_support = &context->swapchain_support;

    VkColorSpaceKHR image_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device,
                                              context->surface,
                                              &surface_capabilities);

    width = Clamp(width,
                  surface_capabilities.minImageExtent.width,
                  surface_capabilities.maxImageExtent.width);

    height = Clamp(height,
                   surface_capabilities.minImageExtent.height,
                   surface_capabilities.maxImageExtent.height);

    swapchain->image_format = swapchain_support->format;
    swapchain->image_color_space = image_color_space;
    swapchain->width = width;
    swapchain->height = height;
    swapchain->present_mode = VK_PRESENT_MODE_FIFO_KHR;

    for (U32 present_mode_index = 0;
         present_mode_index < swapchain_support->present_mode_count;
         present_mode_index++)
    {
        VkPresentModeKHR supported_present_mode = swapchain_support->present_modes[present_mode_index];
        if (supported_present_mode == present_mode)
        {
            swapchain->present_mode = present_mode;
            break;
        }
    }

    min_image_count = Max(min_image_count, surface_capabilities.minImageCount);

    if (surface_capabilities.maxImageCount)
    {
        min_image_count = Min(min_image_count, surface_capabilities.maxImageCount);
    }

    VkExtent2D extent = { width, height };

    VkCompositeAlphaFlagsKHR composite_alpha_flags = 0;

    if ((surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
    {
        composite_alpha_flags = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    else if ((surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR))
    {
        composite_alpha_flags = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    else
    {
        Assert(false);
    }

    VkSwapchainCreateInfoKHR swapchain_create_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swapchain_create_info.surface = context->surface;
    swapchain_create_info.minImageCount = min_image_count;
    swapchain_create_info.imageFormat = swapchain->image_format;
    swapchain_create_info.imageColorSpace = swapchain->image_color_space;
    swapchain_create_info.imageExtent = extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_create_info.preTransform = surface_capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = swapchain->present_mode;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;

    if (context->graphics_queue_family_index != context->present_queue_family_index)
    {
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        U32 queue_family_indices[2] = { context->graphics_queue_family_index, context->present_queue_family_index };
        swapchain_create_info.queueFamilyIndexCount = 2;
        swapchain_create_info.pQueueFamilyIndices = queue_family_indices;
    }
    else
    {
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    Assert(swapchain->handle == VK_NULL_HANDLE);
    CheckVkResult(vkCreateSwapchainKHR(context->logical_device,
                                       &swapchain_create_info,
                                       nullptr,
                                       &swapchain->handle));

    CheckVkResult(vkGetSwapchainImagesKHR(context->logical_device,
                                          swapchain->handle,
                                          &swapchain->image_count,
                                          nullptr));

    swapchain->images = AllocateArray(context->allocator, VkImage, swapchain->image_count);
    swapchain->image_views = AllocateArray(context->allocator, VkImageView, swapchain->image_count);
    swapchain->frame_buffers = AllocateArray(context->allocator, VkFramebuffer, swapchain->image_count);

    CheckVkResult(vkGetSwapchainImagesKHR(context->logical_device,
                                          swapchain->handle,
                                          &swapchain->image_count,
                                          swapchain->images));

    for (U32 image_index = 0; image_index < swapchain->image_count; image_index++)
    {
        VkImageViewCreateInfo image_view_create_info =
            { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

        image_view_create_info.image = swapchain->images[image_index];
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = swapchain->image_format;
        image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_view_create_info.subresourceRange.baseMipLevel = 0;
        image_view_create_info.subresourceRange.levelCount = 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount = 1;

        CheckVkResult(vkCreateImageView(context->logical_device,
                                        &image_view_create_info,
                                        nullptr,
                                        &swapchain->image_views[image_index]));
    }

    // todo(amer): we should check if VK_FORMAT_D32_SFLOAT_S8_UINT is supported
    create_image(&swapchain->depth_sentcil_attachment, context, width, height, VK_FORMAT_D32_SFLOAT_S8_UINT,
                 VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    for (U32 image_index = 0; image_index < swapchain->image_count; image_index++)
    {
        VkFramebufferCreateInfo frame_buffer_create_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        frame_buffer_create_info.renderPass = context->render_pass;
        VkImageView image_views[2] =
        {
            swapchain->image_views[image_index],
            swapchain->depth_sentcil_attachment.view
        };
        frame_buffer_create_info.attachmentCount = ArrayCount(image_views);
        frame_buffer_create_info.pAttachments = image_views;
        frame_buffer_create_info.width = swapchain->width;
        frame_buffer_create_info.height = swapchain->height;
        frame_buffer_create_info.layers = 1;

        CheckVkResult(vkCreateFramebuffer(context->logical_device,
                                          &frame_buffer_create_info,
                                          nullptr,
                                          &swapchain->frame_buffers[image_index]));
    }

    return true;
}

internal_function void
destroy_swapchain(Vulkan_Context *context, Vulkan_Swapchain *swapchain)
{
    for (U32 image_index = 0;
         image_index < swapchain->image_count;
         image_index++)
    {
        vkDestroyFramebuffer(context->logical_device,
                             swapchain->frame_buffers[image_index],
                             nullptr);

        swapchain->frame_buffers[image_index] = VK_NULL_HANDLE;

        vkDestroyImageView(context->logical_device,
                           swapchain->image_views[image_index],
                           nullptr);

        swapchain->image_views[image_index] = VK_NULL_HANDLE;
    }

    deallocate(context->allocator, swapchain->images);
    deallocate(context->allocator, swapchain->image_views);
    deallocate(context->allocator, swapchain->frame_buffers);

    destroy_image(&swapchain->depth_sentcil_attachment, context);

    vkDestroySwapchainKHR(context->logical_device, swapchain->handle, nullptr);
    swapchain->handle = VK_NULL_HANDLE;
}

internal_function void
recreate_swapchain(Vulkan_Context *context, Vulkan_Swapchain *swapchain,
                   U32 width, U32 height, VkPresentModeKHR present_mode)
{
    vkDeviceWaitIdle(context->logical_device);
    destroy_swapchain(context, swapchain);
    create_swapchain(context, width, height,
                     swapchain->image_count, present_mode, swapchain);
}

internal_function bool
create_graphics_pipeline(Vulkan_Context *context,
                         VkShaderModule vertex_shader,
                         VkShaderModule fragment_shader,
                         VkRenderPass render_pass,
                         Vulkan_Graphics_Pipeline *pipeline)
{
    VkVertexInputBindingDescription vertex_input_binding_description = {};
    vertex_input_binding_description.binding = 0;
    vertex_input_binding_description.stride = sizeof(Vertex); // todo(amer): temprary
    vertex_input_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_input_attribute_descriptions[3] = {};
    vertex_input_attribute_descriptions[0].binding = 0;
    vertex_input_attribute_descriptions[0].location = 0;
    vertex_input_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_input_attribute_descriptions[0].offset = offsetof(Vertex, position);

    vertex_input_attribute_descriptions[1].binding = 0;
    vertex_input_attribute_descriptions[1].location = 1;
    vertex_input_attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_input_attribute_descriptions[1].offset = offsetof(Vertex, normal);

    vertex_input_attribute_descriptions[2].binding = 0;
    vertex_input_attribute_descriptions[2].location = 2;
    vertex_input_attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    vertex_input_attribute_descriptions[2].offset = offsetof(Vertex, uv);

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    vertex_input_state_create_info.vertexBindingDescriptionCount = 1;
    vertex_input_state_create_info.pVertexBindingDescriptions = &vertex_input_binding_description;

    vertex_input_state_create_info.vertexAttributeDescriptionCount = ArrayCount(vertex_input_attribute_descriptions);
    vertex_input_state_create_info.pVertexAttributeDescriptions = vertex_input_attribute_descriptions;

    VkPipelineShaderStageCreateInfo vertex_shader_stage_info
        = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vertex_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_shader_stage_info.module = vertex_shader;
    vertex_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo fragment_shader_stage_info
        = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    fragment_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragment_shader_stage_info.module = fragment_shader;
    fragment_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] =
    {
        vertex_shader_stage_info,
        fragment_shader_stage_info
    };

    VkDynamicState dynamic_states[] =
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic_state_create_info.dynamicStateCount = ArrayCount(dynamic_states);
    dynamic_state_create_info.pDynamicStates = dynamic_states;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state_create_info.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (F32)context->swapchain.width;
    viewport.height = (F32)context->swapchain.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D sissor = {};
    sissor.offset = { 0, 0 };
    sissor.extent = { context->swapchain.width, context->swapchain.height };

    VkPipelineViewportStateCreateInfo viewport_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports = &viewport;
    viewport_state_create_info.scissorCount = 1;
    viewport_state_create_info.pScissors = &sissor;

    VkPipelineRasterizationStateCreateInfo rasterization_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };

    rasterization_state_create_info.depthClampEnable = VK_FALSE;
    rasterization_state_create_info.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state_create_info.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state_create_info.lineWidth = 1.0f;
    rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterization_state_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state_create_info.depthBiasEnable = VK_FALSE;
    rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
    rasterization_state_create_info.depthBiasClamp = 0.0f;
    rasterization_state_create_info.depthBiasSlopeFactor = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling_state_create_info.sampleShadingEnable = VK_FALSE;
    multisampling_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling_state_create_info.minSampleShading = 1.0f;
    multisampling_state_create_info.pSampleMask = nullptr;
    multisampling_state_create_info.alphaToCoverageEnable = VK_FALSE;
    multisampling_state_create_info.alphaToOneEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
    color_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|
        VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment_state.blendEnable = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    color_blend_state_create_info.logicOpEnable = VK_FALSE;
    color_blend_state_create_info.logicOp = VK_LOGIC_OP_COPY;
    color_blend_state_create_info.attachmentCount = 1;
    color_blend_state_create_info.pAttachments = &color_blend_attachment_state;
    color_blend_state_create_info.blendConstants[0] = 0.0f;
    color_blend_state_create_info.blendConstants[1] = 0.0f;
    color_blend_state_create_info.blendConstants[2] = 0.0f;
    color_blend_state_create_info.blendConstants[3] = 0.0f;

    VkDescriptorSetLayoutBinding descriptor_layout_bindings[2] = {};
    descriptor_layout_bindings[0].binding = 0;
    descriptor_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_layout_bindings[0].descriptorCount = 1;
    descriptor_layout_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    descriptor_layout_bindings[1].binding = 1;
    descriptor_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_layout_bindings[1].descriptorCount = 1;
    descriptor_layout_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info =
        { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    descriptor_set_layout_create_info.bindingCount = ArrayCount(descriptor_layout_bindings);
    descriptor_set_layout_create_info.pBindings = descriptor_layout_bindings;

    CheckVkResult(vkCreateDescriptorSetLayout(context->logical_device,
                                              &descriptor_set_layout_create_info,
                                              nullptr,
                                              &context->graphics_pipeline.descriptor_set_layout));

    VkPipelineLayoutCreateInfo pipeline_layout_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipeline_layout_create_info.setLayoutCount = 1;
    pipeline_layout_create_info.pSetLayouts = &context->graphics_pipeline.descriptor_set_layout;
    pipeline_layout_create_info.pushConstantRangeCount = 0;
    pipeline_layout_create_info.pPushConstantRanges = nullptr;

    CheckVkResult(vkCreatePipelineLayout(context->logical_device,
                                         &pipeline_layout_create_info,
                                         nullptr, &pipeline->layout));

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info
        = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

    depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
    depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
    depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil_state_create_info.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state_create_info.minDepthBounds = 0.0f;
    depth_stencil_state_create_info.maxDepthBounds = 1.0f;
    depth_stencil_state_create_info.stencilTestEnable = VK_FALSE; // todo(amer): stencil test is disabled
    depth_stencil_state_create_info.front = {};
    depth_stencil_state_create_info.back = {};

    VkGraphicsPipelineCreateInfo graphics_pipeline_create_info =
        { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    graphics_pipeline_create_info.stageCount = 2;
    graphics_pipeline_create_info.pStages = shader_stages;
    graphics_pipeline_create_info.pVertexInputState = &vertex_input_state_create_info;
    graphics_pipeline_create_info.pInputAssemblyState = &input_assembly_state_create_info;
    graphics_pipeline_create_info.pViewportState = &viewport_state_create_info;
    graphics_pipeline_create_info.pRasterizationState = &rasterization_state_create_info;
    graphics_pipeline_create_info.pMultisampleState = &multisampling_state_create_info;
    graphics_pipeline_create_info.pDepthStencilState = &depth_stencil_state_create_info;
    graphics_pipeline_create_info.pColorBlendState = &color_blend_state_create_info;
    graphics_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    graphics_pipeline_create_info.layout = pipeline->layout;
    graphics_pipeline_create_info.renderPass = render_pass;
    graphics_pipeline_create_info.subpass = 0;
    graphics_pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    graphics_pipeline_create_info.basePipelineIndex = -1;

    CheckVkResult(vkCreateGraphicsPipelines(context->logical_device, VK_NULL_HANDLE,
                                            1, &graphics_pipeline_create_info,
                                            nullptr, &pipeline->handle));

    return true;
}

internal_function
void destroy_graphics_pipeline(VkDevice logical_device, Vulkan_Graphics_Pipeline *graphics_pipeline)
{
    Assert(logical_device != VK_NULL_HANDLE);
    Assert(graphics_pipeline);

    vkDestroyDescriptorSetLayout(logical_device, graphics_pipeline->descriptor_set_layout, nullptr);
    vkDestroyPipelineLayout(logical_device, graphics_pipeline->layout, nullptr);
    vkDestroyPipeline(logical_device, graphics_pipeline->handle, nullptr);
}


internal_function bool
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

internal_function void
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

internal_function void
destroy_buffer(Vulkan_Buffer *buffer,
               VkDevice logical_device)
{
    vkFreeMemory(logical_device, buffer->memory, nullptr);
    vkDestroyBuffer(logical_device, buffer->handle, nullptr);
}



internal_function bool load_static_mesh(const char *path, Static_Mesh *static_mesh, Vulkan_Context *context, Memory_Arena *arena)
{
    Scoped_Temprary_Memory_Arena temp_arena(arena);
    Read_Entire_File_Result result =
        platform_begin_read_entire_file(path);

    U32 position_count = 0;
    glm::vec3 *positions = nullptr;

    U32 normal_count = 0;
    glm::vec3 *normals = nullptr;

    U32 uv_count = 0;
    glm::vec2 *uvs = nullptr;

    U32 index_count = 0;
    U16 *indices = nullptr;

    if (result.success)
    {
        U8 *buffer = AllocateArray(&temp_arena, U8, result.size);
        platform_end_read_entire_file(&result, buffer);

        cgltf_options options = {};
        cgltf_data *data = nullptr;
        if (cgltf_parse(&options, buffer, result.size, &data) == cgltf_result_success)
        {
            Assert(data->meshes_count >= 1);
            cgltf_mesh *mesh = &data->meshes[0];
            Assert(mesh->primitives_count >= 1);
            cgltf_primitive *primitive = &mesh->primitives[0];
            Assert(primitive->type == cgltf_primitive_type_triangles);
            for (U32 i = 0; i < primitive->attributes_count; i++)
            {
                cgltf_attribute *attribute = &primitive->attributes[i];
                Assert(attribute->type != cgltf_attribute_type_invalid);
                switch (attribute->type)
                {
                    case cgltf_attribute_type_position:
                    {
                        Assert(attribute->data->type == cgltf_type_vec3);
                        Assert(attribute->data->component_type == cgltf_component_type_r_32f);
                        Assert(attribute->data->buffer_view->type == cgltf_buffer_view_type_vertices);

                        position_count = u64_to_u32(attribute->data->count);
                        U64 stride = attribute->data->stride;
                        Assert(stride == sizeof(glm::vec3));

                        U64 buffer_offset = attribute->data->buffer_view->buffer->extras.start_offset;
                        U8 *position_buffer = ((U8*)data->bin + buffer_offset) + attribute->data->buffer_view->offset;
                        positions = (glm::vec3 *)position_buffer;
                    } break;

                    case cgltf_attribute_type_normal:
                    {
                        Assert(attribute->data->type == cgltf_type_vec3);
                        Assert(attribute->data->component_type == cgltf_component_type_r_32f);
                        Assert(attribute->data->buffer_view->type == cgltf_buffer_view_type_vertices);

                        normal_count = u64_to_u32(attribute->data->count);
                        U64 stride = attribute->data->stride;
                        Assert(stride == sizeof(glm::vec3));

                        U64 buffer_offset = attribute->data->buffer_view->buffer->extras.start_offset;
                        U8* normal_buffer = ((U8*)data->bin + buffer_offset) + attribute->data->buffer_view->offset;
                        normals = (glm::vec3*)normal_buffer;
                    } break;

                    case cgltf_attribute_type_texcoord:
                    {
                        Assert(attribute->data->type == cgltf_type_vec2);
                        Assert(attribute->data->component_type == cgltf_component_type_r_32f);
                        Assert(attribute->data->buffer_view->type == cgltf_buffer_view_type_vertices);

                        uv_count = u64_to_u32(attribute->data->count);
                        U64 stride = attribute->data->stride;
                        Assert(stride == sizeof(glm::vec2));

                        U64 buffer_offset = attribute->data->buffer_view->buffer->extras.start_offset;
                        U8* uv_buffer = ((U8*)data->bin + buffer_offset) + attribute->data->buffer_view->offset;
                        uvs = (glm::vec2*)uv_buffer;
                    } break;
                }
            }

            Assert(primitive->indices->type == cgltf_type_scalar);
            Assert(primitive->indices->component_type == cgltf_component_type_r_16u);
            Assert(primitive->indices->stride == sizeof(U16));
            index_count = u64_to_u32(primitive->indices->count);
            U64 buffer_offset = primitive->indices->buffer_view->buffer->extras.start_offset;
            U8 *index_buffer =
                ((U8*)data->bin + buffer_offset) + primitive->indices->buffer_view->offset;
            indices = (U16*)index_buffer;
            cgltf_free(data);
        }
    }

    Assert(position_count == normal_count);
    Assert(position_count == uv_count);

    U32 vertex_count = position_count;
    Vertex *vertices = AllocateArray(&temp_arena, Vertex, vertex_count);

    for (U32 vertex_index = 0; vertex_index < vertex_count; vertex_index++)
    {
        Vertex *vertex = &vertices[vertex_index];
        vertex->position = positions[vertex_index];
        vertex->normal = normals[vertex_index];
        vertex->uv = uvs[vertex_index];
    }

    U64 vertex_size = vertex_count * sizeof(Vertex);
    create_buffer(&static_mesh->vertex_buffer, context, vertex_size,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    copy_buffer(context, &context->transfer_buffer,
                &static_mesh->vertex_buffer, vertices, vertex_size);
    static_mesh->vertex_count = u64_to_u32(vertex_count);

    U64 index_size = index_count * sizeof(U16);
    create_buffer(&static_mesh->index_buffer, context,
                  index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT|
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    copy_buffer(context, &context->transfer_buffer,
                &static_mesh->index_buffer, indices, index_size);
    static_mesh->index_count = u32_to_u16(index_count);

    S32 texture_width;
    S32 texture_height;
    S32 texture_channels;
    stbi_uc* pixels = stbi_load("models/Default_albedo.jpg",
                                &texture_width, &texture_height,
                                &texture_channels, STBI_rgb_alpha);

    Assert(pixels);

    create_image(&static_mesh->image, context, texture_width, texture_height,
                 VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_IMAGE_ASPECT_COLOR_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    copy_buffer_to_image(context, &context->transfer_buffer,
                         &static_mesh->image, pixels,
                         texture_width * texture_height * sizeof(U32));

    stbi_image_free(pixels);

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
    sampler_create_info.maxLod = 0.0f;
    CheckVkResult(vkCreateSampler(context->logical_device, &sampler_create_info, nullptr, &static_mesh->sampler));

    return true;
}

internal_function bool
init_vulkan(Vulkan_Context *context, Engine *engine, Memory_Arena *arena)
{
    context->allocator = &engine->memory.free_list_allocator;

    const char *required_instance_extensions[] =
    {
        "VK_KHR_surface",

#if HE_OS_WINDOWS
        "VK_KHR_win32_surface",
#endif

#if HE_VULKAN_DEBUGGING
        "VK_EXT_debug_utils",
#endif
    };

    U32 required_api_version = VK_API_VERSION_1_0;
    U32 driver_api_version = 0;

    // vkEnumerateInstanceVersion requires at least vulkan 1.1
    auto enumerate_instance_version = (PFN_vkEnumerateInstanceVersion)
        vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");

    if (enumerate_instance_version)
    {
        enumerate_instance_version(&driver_api_version);
    }
    else
    {
        driver_api_version = VK_API_VERSION_1_0;
    }

    Assert(required_api_version <= driver_api_version);

    VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app_info.pApplicationName = "Hope"; // todo(amer): hard coding "Hope" for now
    app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName = "Hope"; // todo(amer): hard coding "Hope" for now
    app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.apiVersion = required_api_version;

    VkInstanceCreateInfo instance_create_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instance_create_info.pApplicationInfo = &app_info;
    instance_create_info.enabledExtensionCount = ArrayCount(required_instance_extensions);
    instance_create_info.ppEnabledExtensionNames = required_instance_extensions;

#if HE_VULKAN_DEBUGGING

    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info =
        { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };

    debug_messenger_create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    debug_messenger_create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;

    debug_messenger_create_info.pfnUserCallback = vulkan_debug_callback;
    debug_messenger_create_info.pUserData = nullptr;

    const char *layers[] =
    {
        "VK_LAYER_KHRONOS_validation",
    };

    instance_create_info.enabledLayerCount = ArrayCount(layers);
    instance_create_info.ppEnabledLayerNames = layers;
    instance_create_info.pNext = &debug_messenger_create_info;
#endif

    CheckVkResult(vkCreateInstance(&instance_create_info, nullptr, &context->instance));

#if HE_VULKAN_DEBUGGING

    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerExt =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance,
                                                                  "vkCreateDebugUtilsMessengerEXT");
    Assert(vkCreateDebugUtilsMessengerExt);

    CheckVkResult(vkCreateDebugUtilsMessengerExt(context->instance,
                                                 &debug_messenger_create_info,
                                                 nullptr,
                                                 &context->debug_messenger));

#endif

    context->surface = (VkSurfaceKHR)platform_create_vulkan_surface(engine,
                                                                    context->instance);
    Assert(context->surface);

    context->physical_device = pick_physical_device(context->instance, context->surface, arena);
    Assert(context->physical_device != VK_NULL_HANDLE);

    vkGetPhysicalDeviceMemoryProperties(context->physical_device, &context->physical_device_memory_properties);
    vkGetPhysicalDeviceProperties(context->physical_device, &context->physical_device_properties);

    {
        Scoped_Temprary_Memory_Arena temp_arena(arena);

        context->graphics_queue_family_index = 0;
        context->present_queue_family_index = 0;

        U32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device,
                                                 &queue_family_count,
                                                 nullptr);

        VkQueueFamilyProperties *queue_families = AllocateArray(&temp_arena,
                                                                 VkQueueFamilyProperties,
                                                                 queue_family_count);

        vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device,
                                                 &queue_family_count,
                                                 queue_families);

        bool found_a_queue_family_that_can_do_graphics_and_present = false;

        for (U32 queue_family_index = 0;
                 queue_family_index < queue_family_count;
                 queue_family_index++)
        {
            VkQueueFamilyProperties *queue_family = &queue_families[queue_family_index];

            bool can_queue_family_do_graphics =
                (queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT);

            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(context->physical_device,
                                                 queue_family_index,
                                                 context->surface, &present_support);

            bool can_queue_family_present = present_support == VK_TRUE;

            if (can_queue_family_do_graphics && can_queue_family_present)
            {
                context->graphics_queue_family_index = queue_family_index;
                context->present_queue_family_index = queue_family_index;
                found_a_queue_family_that_can_do_graphics_and_present = true;
                break;
            }
        }

        if (!found_a_queue_family_that_can_do_graphics_and_present)
        {
            for (U32 queue_family_index = 0;
                 queue_family_index < queue_family_count;
                 queue_family_index++)
            {
                VkQueueFamilyProperties *queue_family = &queue_families[queue_family_index];

                if (queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    context->graphics_queue_family_index = queue_family_index;
                }

                VkBool32 present_support = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(context->physical_device,
                                                     queue_family_index,
                                                     context->surface, &present_support);
                if (present_support == VK_TRUE)
                {
                    context->present_queue_family_index = queue_family_index;
                }
            }
        }

        F32 queue_priority = 1.0f;
        VkDeviceQueueCreateInfo *queue_create_infos = AllocateArray(&temp_arena,
                                                                     VkDeviceQueueCreateInfo,
                                                                     2);

        queue_create_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[0].queueFamilyIndex = context->graphics_queue_family_index;
        queue_create_infos[0].queueCount = 1;
        queue_create_infos[0].pQueuePriorities = &queue_priority;

        U32 queue_create_info_count = 1;

        if (!found_a_queue_family_that_can_do_graphics_and_present)
        {
            queue_create_infos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_infos[1].queueFamilyIndex = context->present_queue_family_index;
            queue_create_infos[1].queueCount = 1;
            queue_create_infos[1].pQueuePriorities = &queue_priority;
            queue_create_info_count = 2;
        }

        // todo(amer): physical device can use this to check for features....
        VkPhysicalDeviceFeatures physical_device_features = {};
        physical_device_features.samplerAnisotropy = VK_TRUE;

        const char *required_device_extensions[] =
        {
            "VK_KHR_swapchain",
        };

        U32 extension_property_count = 0;
        vkEnumerateDeviceExtensionProperties(context->physical_device,
                                             nullptr, &extension_property_count,
                                             nullptr);

        VkExtensionProperties *extension_properties = AllocateArray(&temp_arena,
                                                                     VkExtensionProperties,
                                                                     extension_property_count);

        vkEnumerateDeviceExtensionProperties(context->physical_device,
                                             nullptr, &extension_property_count,
                                             extension_properties);

        bool not_all_required_device_extensions_are_supported = false;

        for (U32 extension_index = 0;
             extension_index < ArrayCount(required_device_extensions);
             extension_index++)
        {
            const char *device_extension = required_device_extensions[extension_index];
            bool is_extension_supported = false;

            for (U32 extension_property_index = 0;
                 extension_property_index < extension_property_count;
                 extension_property_index++)
            {
                VkExtensionProperties *extension_property = &extension_properties[extension_property_index];
                // todo(amer): string utils
                if (strcmp(device_extension, extension_property->extensionName) == 0)
                {
                    is_extension_supported = true;
                    break;
                }
            }

            if (!is_extension_supported)
            {
                not_all_required_device_extensions_are_supported = true;
            }
        }

        if (not_all_required_device_extensions_are_supported)
        {
            return false;
        }

        VkDeviceCreateInfo device_create_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        device_create_info.pQueueCreateInfos = queue_create_infos;
        device_create_info.queueCreateInfoCount = queue_create_info_count;
        device_create_info.pEnabledFeatures = &physical_device_features;
        device_create_info.ppEnabledExtensionNames = required_device_extensions;
        device_create_info.enabledExtensionCount = ArrayCount(required_device_extensions);

        CheckVkResult(vkCreateDevice(context->physical_device,
                                     &device_create_info, nullptr,
                                     &context->logical_device));

        vkGetDeviceQueue(context->logical_device,
                         context->graphics_queue_family_index,
                         0, &context->graphics_queue);

        vkGetDeviceQueue(context->logical_device,
                         context->present_queue_family_index,
                         0, &context->present_queue);
    }

    VkFormat formats[] =
    {
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_SRGB
    };

    init_swapchain_support(context,
                           formats,
                           ArrayCount(formats),
                           VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                           arena,
                           &context->swapchain_support);

    VkAttachmentDescription attachments[2] = {};

    attachments[0].format = context->swapchain_support.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_stencil_attachment_ref = {};
    depth_stencil_attachment_ref.attachment = 1;
    depth_stencil_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_stencil_attachment_ref;

    VkSubpassDependency dependency = {};

    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;

    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_create_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    render_pass_create_info.attachmentCount = ArrayCount(attachments);
    render_pass_create_info.pAttachments = attachments;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    render_pass_create_info.dependencyCount = 1;
    render_pass_create_info.pDependencies = &dependency;

    CheckVkResult(vkCreateRenderPass(context->logical_device,
                                     &render_pass_create_info,
                                     nullptr, &context->render_pass));

    VkSemaphoreCreateInfo semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    U32 width = 1280;
    U32 height = 720;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    U32 min_image_count = MAX_FRAMES_IN_FLIGHT;
    bool swapchain_created = create_swapchain(context, width, height,
                                              min_image_count, present_mode, &context->swapchain);
    Assert(swapchain_created);

    {
        Scoped_Temprary_Memory_Arena temp_arena(arena);

        Read_Entire_File_Result result =
            platform_begin_read_entire_file("shaders/basic.vert.spv");

        if (result.success)
        {
            U8 *data = AllocateArray(&temp_arena, U8, result.size);
            Assert(data);

            if (platform_end_read_entire_file(&result, data))
            {
                VkShaderModuleCreateInfo vertex_shader_create_info =
                    { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
                vertex_shader_create_info.codeSize = result.size;
                vertex_shader_create_info.pCode = (U32 *)data;

                CheckVkResult(vkCreateShaderModule(context->logical_device,
                                                   &vertex_shader_create_info,
                                                   nullptr,
                                                   &context->vertex_shader_module));
            }
        }
    }

    {
        Scoped_Temprary_Memory_Arena temp_arena(arena);

        Read_Entire_File_Result result =
            platform_begin_read_entire_file("shaders/basic.frag.spv");
        if (result.success)
        {
            U8 *data = AllocateArray(&temp_arena, U8, result.size);
            Assert(data);
            if (platform_end_read_entire_file(&result, data))
            {
                VkShaderModuleCreateInfo fragment_shader_create_info =
                    { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
                fragment_shader_create_info.codeSize = result.size;
                fragment_shader_create_info.pCode = (U32 *)data;

                CheckVkResult(vkCreateShaderModule(context->logical_device,
                                                   &fragment_shader_create_info,
                                                   nullptr,
                                                   &context->fragment_shader_module));
            }
        }
    }

    VkCommandPoolCreateInfo graphics_command_pool_create_info
        = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };

    graphics_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    graphics_command_pool_create_info.queueFamilyIndex = context->graphics_queue_family_index;

    CheckVkResult(vkCreateCommandPool(context->logical_device,
                                      &graphics_command_pool_create_info,
                                      nullptr, &context->graphics_command_pool));

    VkCommandBufferAllocateInfo graphics_command_buffer_allocate_info
        = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    graphics_command_buffer_allocate_info.commandPool = context->graphics_command_pool;
    graphics_command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    graphics_command_buffer_allocate_info.commandBufferCount = 3;
    CheckVkResult(vkAllocateCommandBuffers(context->logical_device,
                                           &graphics_command_buffer_allocate_info,
                                           context->graphics_command_buffers));

    create_buffer(&context->transfer_buffer, context, HE_MegaBytes(128),
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    bool loaded = load_static_mesh("models/DamagedHelmet.glb", &context->static_mesh, context, arena);
    Assert(loaded);

    create_graphics_pipeline(context,
                             context->vertex_shader_module,
                             context->fragment_shader_module,
                             context->render_pass,
                             &context->graphics_pipeline);

    for (U32 frame_index = 0; frame_index < MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        Vulkan_Buffer *global_uniform_buffer = &context->global_uniform_buffers[frame_index];
        create_buffer(global_uniform_buffer, context, sizeof(Global_Uniform_Buffer),
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    VkDescriptorPoolSize descriptor_pool_sizes[2] = {};
    descriptor_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_pool_sizes[0].descriptorCount = U32(MAX_FRAMES_IN_FLIGHT);

    descriptor_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_pool_sizes[1].descriptorCount = U32(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo descriptor_pool_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descriptor_pool_create_info.poolSizeCount = ArrayCount(descriptor_pool_sizes);
    descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes;
    descriptor_pool_create_info.maxSets = MAX_FRAMES_IN_FLIGHT;

    CheckVkResult(vkCreateDescriptorPool(context->logical_device,
                                         &descriptor_pool_create_info,
                                         nullptr, &context->descriptor_pool));

    VkDescriptorSetLayout descriptor_set_layouts[MAX_FRAMES_IN_FLIGHT] = {};

    for (U32 frame_index = 0;
         frame_index < MAX_FRAMES_IN_FLIGHT;
         frame_index++)
    {
        descriptor_set_layouts[frame_index] = context->graphics_pipeline.descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo descriptor_set_allocation_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    descriptor_set_allocation_info.descriptorPool = context->descriptor_pool;
    descriptor_set_allocation_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    descriptor_set_allocation_info.pSetLayouts = descriptor_set_layouts;

    CheckVkResult(vkAllocateDescriptorSets(context->logical_device,
                                           &descriptor_set_allocation_info,
                                           context->descriptor_sets));

    for (U32 frame_index = 0; frame_index < MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        VkDescriptorBufferInfo descriptor_buffer_info = {};
        descriptor_buffer_info.buffer = context->global_uniform_buffers[frame_index].handle;
        descriptor_buffer_info.offset = 0;
        descriptor_buffer_info.range = sizeof(Global_Uniform_Buffer);

        VkDescriptorImageInfo descriptor_image_info = {};
        descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptor_image_info.imageView = context->static_mesh.image.view;
        descriptor_image_info.sampler = context->static_mesh.sampler;

        VkWriteDescriptorSet write_descriptor_sets[2] = {};
        write_descriptor_sets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_sets[0].dstSet = context->descriptor_sets[frame_index];
        write_descriptor_sets[0].dstBinding = 0;
        write_descriptor_sets[0].dstArrayElement = 0;
        write_descriptor_sets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write_descriptor_sets[0].descriptorCount = 1;
        write_descriptor_sets[0].pBufferInfo = &descriptor_buffer_info;

        write_descriptor_sets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_sets[1].dstSet = context->descriptor_sets[frame_index];
        write_descriptor_sets[1].dstBinding = 1;
        write_descriptor_sets[1].dstArrayElement = 0;
        write_descriptor_sets[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write_descriptor_sets[1].descriptorCount = 1;
        write_descriptor_sets[1].pImageInfo = &descriptor_image_info;

        vkUpdateDescriptorSets(context->logical_device, ArrayCount(write_descriptor_sets), write_descriptor_sets, 0, nullptr);
    }

    for (U32 sync_primitive_index = 0;
         sync_primitive_index < MAX_FRAMES_IN_FLIGHT;
         sync_primitive_index++)
    {
        CheckVkResult(vkCreateSemaphore(context->logical_device,
                                        &semaphore_create_info,
                                        nullptr,
                                        &context->image_available_semaphores[sync_primitive_index]));

        CheckVkResult(vkCreateSemaphore(context->logical_device,
                                        &semaphore_create_info,
                                        nullptr,
                                        &context->rendering_finished_semaphores[sync_primitive_index]));


        CheckVkResult(vkCreateFence(context->logical_device,
                                    &fence_create_info,
                                    nullptr,
                                    &context->frame_in_flight_fences[sync_primitive_index]));
    }

    context->current_frame_in_flight_index = 0;
    context->frames_in_flight = 2;
    Assert(context->frames_in_flight <= MAX_FRAMES_IN_FLIGHT);
    return true;
}

internal_function void
vulkan_draw(Renderer_State *renderer_state, Vulkan_Context *context, F32 delta_time)
{
    U32 current_frame_in_flight_index = context->current_frame_in_flight_index;

    vkWaitForFences(context->logical_device,
                    1, &context->frame_in_flight_fences[current_frame_in_flight_index],
                    VK_TRUE, UINT64_MAX);

    if ((renderer_state->back_buffer_width != context->swapchain.width ||
         renderer_state->back_buffer_height != context->swapchain.height) &&
        renderer_state->back_buffer_width != 0 && renderer_state->back_buffer_height != 0)
    {
        recreate_swapchain(context,
                           &context->swapchain,
                           renderer_state->back_buffer_width,
                           renderer_state->back_buffer_height,
                           context->swapchain.present_mode);
        return;
    }

    U32 image_index = 0;
    VkResult result = vkAcquireNextImageKHR(context->logical_device,
                                            context->swapchain.handle,
                                            UINT64_MAX,
                                            context->image_available_semaphores[current_frame_in_flight_index],
                                            VK_NULL_HANDLE,
                                            &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        if (renderer_state->back_buffer_width != 0 && renderer_state->back_buffer_height != 0)
        {
            recreate_swapchain(context,
                               &context->swapchain,
                               renderer_state->back_buffer_width,
                               renderer_state->back_buffer_height,
                               context->swapchain.present_mode);

        }
        return;
    }
    else
    {
        Assert(result == VK_SUCCESS);
    }

    vkResetFences(context->logical_device, 1, &context->frame_in_flight_fences[current_frame_in_flight_index]);

    VkCommandBuffer command_buffer = context->graphics_command_buffers[current_frame_in_flight_index];
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer,
                         &command_buffer_begin_info);

    VkClearValue clear_values[2] = {};
    clear_values[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
    clear_values[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo render_pass_begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    render_pass_begin_info.renderPass = context->render_pass;
    render_pass_begin_info.framebuffer = context->swapchain.frame_buffers[image_index];
    render_pass_begin_info.renderArea.offset = { 0, 0 };
    render_pass_begin_info.renderArea.extent = { context->swapchain.width, context->swapchain.height };
    render_pass_begin_info.clearValueCount = ArrayCount(clear_values);
    render_pass_begin_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(command_buffer,
                         &render_pass_begin_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      context->graphics_pipeline.handle);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (F32)context->swapchain.width;
    viewport.height = (F32)context->swapchain.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer,
                     0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = context->swapchain.width;
    scissor.extent.height = context->swapchain.height;
    vkCmdSetScissor(command_buffer,
                    0, 1, &scissor);

    VkBuffer vertex_buffers[] = { context->static_mesh.vertex_buffer.handle };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffer,
                           0, 1, vertex_buffers, offsets);

    vkCmdBindIndexBuffer(command_buffer,
                         context->static_mesh.index_buffer.handle, 0, VK_INDEX_TYPE_UINT16);

    F32 aspect_ratio = (F32)renderer_state->back_buffer_width / (F32)renderer_state->back_buffer_height;

    F32 rotation_speed = 45.0f;
    local_presist F32 rotation_angle = 0.0f;
    rotation_angle += rotation_speed * delta_time;
    if (rotation_angle >= 360.0f) rotation_angle -= 360.0f;

    glm::vec3 euler = glm::vec3(glm::radians(90.0f), glm::radians(rotation_angle), 0.0f);
    glm::quat rotation = glm::quat(euler);

    Global_Uniform_Buffer gub_data;
    gub_data.model = glm::toMat4(rotation);
    gub_data.view = glm::lookAt(glm::vec3(0.0f, 1.0f, -5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    gub_data.projection = glm::perspective(glm::radians(45.0f), aspect_ratio, 0.1f, 1000.0f);
    gub_data.projection[1][1] *= -1;

    Vulkan_Buffer *global_uniform_buffer = &context->global_uniform_buffers[current_frame_in_flight_index];
    memcpy(global_uniform_buffer->data, &gub_data, sizeof(Global_Uniform_Buffer));

    vkCmdBindDescriptorSets(command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            context->graphics_pipeline.layout,
                            0, 1,
                            &context->descriptor_sets[current_frame_in_flight_index],
                            0, nullptr);

    vkCmdDrawIndexed(command_buffer,
                     context->static_mesh.index_count, 1, 0, 0, 0);

    vkCmdEndRenderPass(command_buffer);
    vkEndCommandBuffer(command_buffer);

    VkPipelineStageFlags wait_stage =  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

    submit_info.pWaitDstStageMask = &wait_stage;

    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &context->image_available_semaphores[current_frame_in_flight_index];

    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &context->rendering_finished_semaphores[current_frame_in_flight_index];

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(context->graphics_queue, 1, &submit_info, context->frame_in_flight_fences[current_frame_in_flight_index]);

    VkPresentInfoKHR present_info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &context->rendering_finished_semaphores[current_frame_in_flight_index];

    present_info.swapchainCount = 1;
    present_info.pSwapchains = &context->swapchain.handle;
    present_info.pImageIndices = &image_index;

    result = vkQueuePresentKHR(context->present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        if (renderer_state->back_buffer_width != 0 && renderer_state->back_buffer_height != 0)
        {
            recreate_swapchain(context,
                               &context->swapchain,
                               renderer_state->back_buffer_width,
                               renderer_state->back_buffer_height,
                               context->swapchain.present_mode);
        }
    }
    else
    {
        Assert(result == VK_SUCCESS);
    }

    context->current_frame_in_flight_index++;
    if (context->current_frame_in_flight_index == context->frames_in_flight)
    {
        context->current_frame_in_flight_index = 0;
    }
}

void deinit_vulkan(Vulkan_Context *context)
{
    vkDeviceWaitIdle(context->logical_device);

    vkDestroyDescriptorPool(context->logical_device, context->descriptor_pool, nullptr);

    vkDestroySampler(context->logical_device, context->static_mesh.sampler, nullptr);
    destroy_image(&context->static_mesh.image, context);

    destroy_buffer(&context->transfer_buffer, context->logical_device);
    destroy_buffer(&context->static_mesh.vertex_buffer, context->logical_device);
    destroy_buffer(&context->static_mesh.index_buffer, context->logical_device);

    for (U32 frame_index = 0;
         frame_index < MAX_FRAMES_IN_FLIGHT;
         frame_index++)
    {
        destroy_buffer(&context->global_uniform_buffers[frame_index], context->logical_device);

        vkDestroySemaphore(context->logical_device,
                           context->image_available_semaphores[frame_index],
                           nullptr);

        vkDestroySemaphore(context->logical_device,
                           context->rendering_finished_semaphores[frame_index],
                           nullptr);

        vkDestroyFence(context->logical_device,
                       context->frame_in_flight_fences[frame_index],
                       nullptr);
    }

    vkDestroyCommandPool(context->logical_device, context->graphics_command_pool, nullptr);

    destroy_swapchain(context, &context->swapchain);
    destroy_graphics_pipeline(context->logical_device, &context->graphics_pipeline);

    vkDestroyRenderPass(context->logical_device, context->render_pass, nullptr);
    vkDestroyShaderModule(context->logical_device, context->vertex_shader_module, nullptr);
    vkDestroyShaderModule(context->logical_device, context->fragment_shader_module, nullptr);

    vkDestroySurfaceKHR(context->instance, context->surface, nullptr);
    vkDestroyDevice(context->logical_device, nullptr);

#if HE_VULKAN_DEBUGGING
     PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerExt =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance,
                                                                  "vkDestroyDebugUtilsMessengerEXT");
    Assert(vkDestroyDebugUtilsMessengerExt);
    vkDestroyDebugUtilsMessengerExt(context->instance,
                                    context->debug_messenger,
                                    nullptr);
#endif

    vkDestroyInstance(context->instance, nullptr);
}

bool vulkan_renderer_init(Renderer_State *renderer_state,
                          Engine *engine,
                          Memory_Arena *arena)
{
    (void)renderer_state;
    return init_vulkan(&vulkan_context, engine, arena);
}

void vulkan_renderer_deinit(Renderer_State *renderer_state)
{
    (void)renderer_state;
    deinit_vulkan(&vulkan_context);
}

void vulkan_renderer_on_resize(Renderer_State *renderer_state,
                               U32 width,
                               U32 height)
{
    (void)renderer_state;
    recreate_swapchain(&vulkan_context,
                       &vulkan_context.swapchain,
                       width,
                       height,
                       vulkan_context.swapchain.present_mode);
}

void vulkan_renderer_draw(Renderer_State *renderer_state, F32 delta_time)
{
    vulkan_draw(renderer_state, &vulkan_context, delta_time);
}