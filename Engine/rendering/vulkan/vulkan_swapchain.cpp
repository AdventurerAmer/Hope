#include "vulkan_swapchain.h"
#include "vulkan_utils.h"

bool init_swapchain_support(Vulkan_Context *context, VkFormat *image_formats, U32 image_format_count, VkColorSpaceKHR color_space, Vulkan_Swapchain_Support *swapchain_support)
{
    Memory_Arena *arena = get_permenent_arena();

    swapchain_support->surface_format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device, context->surface, &swapchain_support->surface_format_count, nullptr);

    HE_ASSERT(swapchain_support->surface_format_count);

    swapchain_support->surface_formats = HE_ALLOCATE_ARRAY(arena, VkSurfaceFormatKHR, swapchain_support->surface_format_count);

    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device, context->surface, &swapchain_support->surface_format_count, swapchain_support->surface_formats);

    swapchain_support->present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device, context->surface, &swapchain_support->present_mode_count, nullptr);

    HE_ASSERT(swapchain_support->present_mode_count);

    swapchain_support->present_modes = HE_ALLOCATE_ARRAY(arena, VkPresentModeKHR, swapchain_support->present_mode_count);

    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device, context->surface, &swapchain_support->present_mode_count, swapchain_support->present_modes);

    swapchain_support->image_format = VK_FORMAT_UNDEFINED;

    for (U32 i = 0; i < image_format_count; i++)
    {
        VkFormat format = image_formats[i];
        bool found = false;

        for (U32 j = 0; j < swapchain_support->surface_format_count; j++)
        {
            const VkSurfaceFormatKHR *surface_format = &swapchain_support->surface_formats[j];

            if (surface_format->format == format && surface_format->colorSpace == color_space)
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

    return true;
}

bool is_present_mode_supported(Vulkan_Swapchain_Support *swapchain_support, VkPresentModeKHR present_mode)
{
    for (U32 present_mode_index = 0; present_mode_index < swapchain_support->present_mode_count; present_mode_index++)
    {
        if (swapchain_support->present_modes[present_mode_index] == present_mode)
        {
            return true;
        }
    }

    return false;
}

bool create_swapchain(Vulkan_Context *context, U32 width, U32 height, U32 min_image_count, VkPresentModeKHR present_mode, Vulkan_Swapchain *swapchain)
{
    HE_ASSERT(context);
    HE_ASSERT(width);
    HE_ASSERT(height);
    HE_ASSERT(min_image_count);
    HE_ASSERT(swapchain);
    
    const Vulkan_Swapchain_Support *swapchain_support = &context->swapchain_support;
    VkColorSpaceKHR image_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device, context->surface, &surface_capabilities);

    width = HE_CLAMP(width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
    height = HE_CLAMP(height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);

    swapchain->image_format = swapchain_support->image_format;
    swapchain->image_color_space = image_color_space;
    swapchain->width = width;
    swapchain->height = height;
    swapchain->present_mode = present_mode;

    min_image_count = HE_MAX(min_image_count, surface_capabilities.minImageCount);

    if (surface_capabilities.maxImageCount)
    {
        min_image_count = HE_MIN(min_image_count, surface_capabilities.maxImageCount);
    }

    VkExtent2D extent = { width, height };

    VkCompositeAlphaFlagBitsKHR composite_alpha_flags = VK_COMPOSITE_ALPHA_FLAG_BITS_MAX_ENUM_KHR;

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
        HE_ASSERT(false);
    }

    VkSwapchainCreateInfoKHR swapchain_create_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swapchain_create_info.surface = context->surface;
    swapchain_create_info.minImageCount = min_image_count;
    swapchain_create_info.imageFormat = swapchain->image_format;
    swapchain_create_info.imageColorSpace = swapchain->image_color_space;
    swapchain_create_info.imageExtent = extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_create_info.preTransform = surface_capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = composite_alpha_flags;
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

    HE_ASSERT(swapchain->handle == VK_NULL_HANDLE);
    HE_CHECK_VKRESULT(vkCreateSwapchainKHR(context->logical_device, &swapchain_create_info, nullptr, &swapchain->handle));

    HE_CHECK_VKRESULT(vkGetSwapchainImagesKHR(context->logical_device, swapchain->handle, &swapchain->image_count, nullptr));

    Free_List_Allocator *allocator = get_general_purpose_allocator();
    swapchain->images = HE_ALLOCATE_ARRAY(allocator, VkImage, swapchain->image_count);
    swapchain->image_views = HE_ALLOCATE_ARRAY(allocator, VkImageView, swapchain->image_count);

    HE_CHECK_VKRESULT(vkGetSwapchainImagesKHR(context->logical_device, swapchain->handle, &swapchain->image_count, swapchain->images));

    for (U32 image_index = 0; image_index < swapchain->image_count; image_index++)
    {
        VkImageViewCreateInfo image_view_create_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
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
        HE_CHECK_VKRESULT(vkCreateImageView(context->logical_device, &image_view_create_info, nullptr, &swapchain->image_views[image_index]));
    }

    return true;
}

void destroy_swapchain(Vulkan_Context *context, Vulkan_Swapchain *swapchain)
{
    for (U32 image_index = 0; image_index < swapchain->image_count; image_index++)
    {
        vkDestroyImageView(context->logical_device, swapchain->image_views[image_index], nullptr);
        swapchain->image_views[image_index] = VK_NULL_HANDLE;
    }

    Free_List_Allocator *allocator = get_general_purpose_allocator();
    deallocate(allocator, swapchain->images);
    deallocate(allocator, swapchain->image_views);

    vkDestroySwapchainKHR(context->logical_device, swapchain->handle, nullptr);
    swapchain->handle = VK_NULL_HANDLE;
}

void recreate_swapchain(Vulkan_Context *context, Vulkan_Swapchain *swapchain, U32 width, U32 height, VkPresentModeKHR present_mode)
{
    destroy_swapchain(context, swapchain);
    create_swapchain(context, width, height, swapchain->image_count, present_mode, swapchain);
}