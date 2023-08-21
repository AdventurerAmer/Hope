#include "vulkan_swapchain.h"
#include "vulkan_image.h"

bool
init_swapchain_support(Vulkan_Context *context,
                       VkFormat *image_formats,
                       U32 image_format_count,
                       VkFormat *depth_stencil_formats,
                       U32 depth_stencil_format_count,
                       VkColorSpaceKHR color_space,
                       Memory_Arena *arena,
                       Vulkan_Swapchain_Support *swapchain_support)
{
    swapchain_support->surface_format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device,
                                         context->surface,
                                         &swapchain_support->surface_format_count,
                                         nullptr);

    HOPE_Assert(swapchain_support->surface_format_count);

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

    HOPE_Assert(swapchain_support->present_mode_count);

    swapchain_support->present_modes = AllocateArray(arena,
                                                     VkPresentModeKHR,
                                                     swapchain_support->present_mode_count);

    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device,
                                              context->surface,
                                              &swapchain_support->present_mode_count,
                                              swapchain_support->present_modes);

    swapchain_support->image_format = swapchain_support->surface_formats[0].format;

    for (U32 format_index = 0;
         format_index < image_format_count;
         format_index++)
    {
        VkFormat format = image_formats[format_index];
        bool found = false;

        for (U32 format_index = 0;
             format_index < swapchain_support->surface_format_count;
             format_index++)
        {
            const VkSurfaceFormatKHR *surface_format = &swapchain_support->surface_formats[format_index];

            if (surface_format->format == format &&
                surface_format->colorSpace == color_space)
            {
                swapchain_support->image_format = format;
                found = true;
                break;
            }
        }

        if (found)
        {
            break;
        }
    }

    for (U32 format_index = 0;
         format_index < depth_stencil_format_count;
         format_index++)
    {
        VkFormat format = depth_stencil_formats[format_index];

        VkFormatProperties format_properties;
        vkGetPhysicalDeviceFormatProperties(context->physical_device, format, &format_properties);

        if (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            swapchain_support->depth_stencil_format = format;
            break;
        }
    }

    return true;
}

bool
create_swapchain(Vulkan_Context *context,
                 U32 width, U32 height,
                 U32 min_image_count,
                 VkPresentModeKHR present_mode,
                 Vulkan_Swapchain *swapchain)
{
    HOPE_Assert(context);
    HOPE_Assert(width);
    HOPE_Assert(height);
    HOPE_Assert(min_image_count);
    HOPE_Assert(swapchain);
    
    const Vulkan_Swapchain_Support *swapchain_support = &context->swapchain_support;
    VkColorSpaceKHR image_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device,
                                              context->surface,
                                              &surface_capabilities);

    width = HOPE_Clamp(width,
                  surface_capabilities.minImageExtent.width,
                  surface_capabilities.maxImageExtent.width);

    height = HOPE_Clamp(height,
                   surface_capabilities.minImageExtent.height,
                   surface_capabilities.maxImageExtent.height);

    swapchain->image_format = swapchain_support->image_format;
    swapchain->depth_stencil_format = swapchain_support->depth_stencil_format;
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

    min_image_count = HOPE_Max(min_image_count, surface_capabilities.minImageCount);

    if (surface_capabilities.maxImageCount)
    {
        min_image_count = HOPE_Min(min_image_count, surface_capabilities.maxImageCount);
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
        HOPE_Assert(false);
    }

    VkSwapchainCreateInfoKHR swapchain_create_info =
        { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
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

    HOPE_Assert(swapchain->handle == VK_NULL_HANDLE);
    CheckVkResult(vkCreateSwapchainKHR(context->logical_device,
                                       &swapchain_create_info,
                                       nullptr,
                                       &swapchain->handle));

    CheckVkResult(vkGetSwapchainImagesKHR(context->logical_device,
                                          swapchain->handle,
                                          &swapchain->image_count,
                                          nullptr));

    swapchain->images = AllocateArray(context->allocator,
                                      VkImage, swapchain->image_count);
    swapchain->image_views = AllocateArray(context->allocator,
                                           VkImageView, swapchain->image_count);
    swapchain->frame_buffers = AllocateArray(context->allocator,
                                             VkFramebuffer, swapchain->image_count);

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

    bool mipmapping = false;

    if (context->msaa_samples != VK_SAMPLE_COUNT_1_BIT)
    {
        create_image(&swapchain->color_attachment, context,
                     width, height, swapchain->image_format, VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     mipmapping, context->msaa_samples);

    }

    create_image(&swapchain->depth_stencil_attachment,
                 context, width, height, swapchain->depth_stencil_format,
                 VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mipmapping,
                 context->msaa_samples);

    for (U32 image_index = 0; image_index < swapchain->image_count; image_index++)
    {
        VkImageView image_views_msaa[] =
        {
            swapchain->color_attachment.view,
            swapchain->image_views[image_index],
            swapchain->depth_stencil_attachment.view
        };

        VkImageView image_views[] =
        {
            swapchain->image_views[image_index],
            swapchain->depth_stencil_attachment.view
        };

        VkFramebufferCreateInfo frame_buffer_create_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        frame_buffer_create_info.renderPass = context->render_pass;

        if (context->msaa_samples != VK_SAMPLE_COUNT_1_BIT)
        {
            frame_buffer_create_info.attachmentCount = HOPE_ArrayCount(image_views_msaa);
            frame_buffer_create_info.pAttachments = image_views_msaa;
        }
        else
        {
            frame_buffer_create_info.attachmentCount = HOPE_ArrayCount(image_views);
            frame_buffer_create_info.pAttachments = image_views;
        }

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

void
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

    destroy_image(&swapchain->color_attachment, context);
    destroy_image(&swapchain->depth_stencil_attachment, context);

    deallocate(context->allocator, swapchain->images);
    deallocate(context->allocator, swapchain->image_views);
    deallocate(context->allocator, swapchain->frame_buffers);

    vkDestroySwapchainKHR(context->logical_device, swapchain->handle, nullptr);
    swapchain->handle = VK_NULL_HANDLE;
}

void
recreate_swapchain(Vulkan_Context *context, Vulkan_Swapchain *swapchain,
                   U32 width, U32 height, VkPresentModeKHR present_mode)
{
    vkDeviceWaitIdle(context->logical_device);
    destroy_swapchain(context, swapchain);
    create_swapchain(context, width, height,
                     swapchain->image_count, present_mode, swapchain);
}