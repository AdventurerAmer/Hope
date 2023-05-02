#include "vulkan.h"
#include "core/platform.h"
#include "core/debugging.h"
#include "core/memory.h"

internal_function VKAPI_ATTR VkBool32 VKAPI_CALL
vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                      VkDebugUtilsMessageTypeFlagsEXT message_type,
                      const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                      void *user_data)
{
    (void)message_severity;
    (void)message_type;
    (void)user_data;
    HE_DebugPrintf(Rendering, Trace, "%s\n", callback_data->pMessage);
    HE_Assert(message_severity != VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
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

    VkPhysicalDevice *physical_devices = ArenaPushArray(&temp_arena,
                                                        VkPhysicalDevice,
                                                        physical_device_count);
    HE_Assert(physical_devices);

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

        U32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(*current_physical_device,
                                                 &queue_family_count,
                                                 nullptr);

        bool can_physical_device_do_graphics = false;
        bool can_physical_device_present = false;

        VkQueueFamilyProperties *queue_families = ArenaPushArray(&temp_arena,
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
            // todo(amer): add more checking as we do features in the future
            // for now we are looking for a discrete gpu
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
create_swapchain(Vulkan_Context *context,
                 U32 width, U32 height,
                 U32 min_image_count,
                 VkPresentModeKHR present_mode,
                 Vulkan_Swapchain *swapchain)
{
    HE_Assert(context);
    HE_Assert(width);
    HE_Assert(height);
    HE_Assert(min_image_count);
    HE_Assert(swapchain);

    const Vulkan_Swapchain_Support *swapchain_support = &context->swapchain_support;

    VkColorSpaceKHR image_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkFormat image_format = swapchain_support->surface_formats[0].format;

    VkFormat desired_formats[] =
    {
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_SRGB,
    };

    for (U32 desired_format_index = 0;
         desired_format_index < HE_ArrayCount(desired_formats);
         desired_format_index++)
    {
        VkFormat desired_format = desired_formats[desired_format_index];
        bool found = false;

        for (U32 surface_format_index = 0;
             surface_format_index < swapchain_support->surface_format_count;
             surface_format_index++)
        {
            const VkSurfaceFormatKHR *surface_format = &swapchain_support->surface_formats[surface_format_index];

            if (surface_format->format == desired_format &&
                surface_format->colorSpace == image_color_space)
            {
                image_format = desired_format;
                found = true;
                break;
            }
        }

        if (found)
        {
            break;
        }
    }

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device,
                                              context->surface,
                                              &surface_capabilities);

    width = HE_Clamp(width,
                     surface_capabilities.minImageExtent.width,
                     surface_capabilities.maxImageExtent.width);

    height = HE_Clamp(height,
                      surface_capabilities.minImageExtent.height,
                      surface_capabilities.maxImageExtent.height);

    swapchain->image_format = image_format;
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

    min_image_count = HE_Max(min_image_count, surface_capabilities.minImageCount);

    if (surface_capabilities.maxImageCount)
    {
        min_image_count = HE_Min(min_image_count, surface_capabilities.maxImageCount);
    }

    VkExtent2D extent = { width, height };

    VkSwapchainCreateInfoKHR swapchain_create_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swapchain_create_info.surface = context->surface;
    swapchain_create_info.minImageCount = min_image_count;
    swapchain_create_info.imageFormat = swapchain->image_format;
    swapchain_create_info.imageColorSpace = swapchain->image_color_space;
    swapchain_create_info.imageExtent = extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_create_info.preTransform = surface_capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // todo(amer): VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR is not supported on all platforms
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

    HE_Assert(swapchain->handle == VK_NULL_HANDLE);
    CheckVkResult(vkCreateSwapchainKHR(context->logical_device,
                                       &swapchain_create_info,
                                       nullptr,
                                       &swapchain->handle));

    CheckVkResult(vkGetSwapchainImagesKHR(context->logical_device,
                                          swapchain->handle,
                                          &swapchain->image_count,
                                          nullptr));

    HE_Assert(swapchain->image_count <= HE_ArrayCount(swapchain->images));
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

    for (U32 image_index = 0; image_index < swapchain->image_count; image_index++)
    {
        VkFramebufferCreateInfo frame_buffer_create_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        frame_buffer_create_info.renderPass = context->render_pass;
        frame_buffer_create_info.attachmentCount = 1;
        frame_buffer_create_info.pAttachments = &swapchain->image_views[image_index];
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
destroy_swapchain(VkDevice logical_device, Vulkan_Swapchain *swapchain)
{
    for (U32 image_index = 0;
         image_index < swapchain->image_count;
         image_index++)
    {
        vkDestroyFramebuffer(logical_device,
                             swapchain->frame_buffers[image_index],
                             nullptr);

        swapchain->frame_buffers[image_index] = VK_NULL_HANDLE;

        vkDestroyImageView(logical_device,
                           swapchain->image_views[image_index],
                           nullptr);

        swapchain->image_views[image_index] = VK_NULL_HANDLE;
    }

    vkDestroySwapchainKHR(logical_device, swapchain->handle, nullptr);
    swapchain->handle = VK_NULL_HANDLE;
}

internal_function void
recreate_swapchain(Vulkan_Context *context, Vulkan_Swapchain *swapchain,
                   U32 width, U32 height, VkPresentModeKHR present_mode)
{
    vkDeviceWaitIdle(context->logical_device);
    destroy_swapchain(context->logical_device, swapchain);
    create_swapchain(context, width, height,
                     swapchain->image_count, present_mode, swapchain);
}

internal_function bool
init_vulkan(Vulkan_Context *context, Engine *engine, Memory_Arena *arena)
{
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

    HE_Assert(required_api_version <= driver_api_version);

    VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app_info.pApplicationName = "Hope"; // todo(amer): hard coding "Hope" for now
    app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName = "Hope"; // todo(amer): hard coding "Hope" for now
    app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.apiVersion = required_api_version;

    VkInstanceCreateInfo instance_create_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instance_create_info.pApplicationInfo = &app_info;
    instance_create_info.enabledExtensionCount = HE_ArrayCount(required_instance_extensions);
    instance_create_info.ppEnabledExtensionNames = required_instance_extensions;

#if HE_VULKAN_DEBUGGING

    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info =
        { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };

    debug_messenger_create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT|
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

    instance_create_info.enabledLayerCount = HE_ArrayCount(layers);
    instance_create_info.ppEnabledLayerNames = layers;
    instance_create_info.pNext = &debug_messenger_create_info;
#endif

    CheckVkResult(vkCreateInstance(&instance_create_info, nullptr, &context->instance));

#if HE_VULKAN_DEBUGGING

    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerExt =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance,
                                                                  "vkCreateDebugUtilsMessengerEXT");
    HE_Assert(vkCreateDebugUtilsMessengerExt);

    CheckVkResult(vkCreateDebugUtilsMessengerExt(context->instance,
                                                 &debug_messenger_create_info,
                                                 nullptr,
                                                 &context->debug_messenger));

#endif

    context->surface = (VkSurfaceKHR)platform_create_vulkan_surface(engine,
                                                                    context->instance);
    HE_Assert(context->surface);

    context->physical_device = pick_physical_device(context->instance, context->surface, arena);
    HE_Assert(context->physical_device != VK_NULL_HANDLE);

    {
        Scoped_Temprary_Memory_Arena temp_arena(arena);

        context->graphics_queue_family_index = 0;
        context->present_queue_family_index = 0;

        U32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device,
                                                 &queue_family_count,
                                                 nullptr);

        VkQueueFamilyProperties *queue_families = ArenaPushArray(&temp_arena,
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

        VkDeviceQueueCreateInfo *queue_create_infos = ArenaPushArray(&temp_arena,
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

        VkPhysicalDeviceFeatures physical_device_features = {};

        const char *required_device_extensions[] =
        {
            "VK_KHR_swapchain",
        };

        U32 extension_property_count = 0;
        vkEnumerateDeviceExtensionProperties(context->physical_device,
                                             nullptr, &extension_property_count,
                                             nullptr);

        VkExtensionProperties *extension_properties = ArenaPushArray(&temp_arena,
                                                                     VkExtensionProperties,
                                                                     extension_property_count);

        vkEnumerateDeviceExtensionProperties(context->physical_device,
                                             nullptr, &extension_property_count,
                                             extension_properties);

        bool not_all_required_device_extensions_are_supported = false;

        for (U32 extension_index = 0;
             extension_index < HE_ArrayCount(required_device_extensions);
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
        device_create_info.enabledExtensionCount = HE_ArrayCount(required_device_extensions);

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

    // todo(amer): pick the image format here
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = VK_FORMAT_B8G8R8A8_SRGB;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkSubpassDependency dependency = {};

    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;

    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_create_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    render_pass_create_info.attachmentCount = 1;
    render_pass_create_info.pAttachments = &color_attachment;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    render_pass_create_info.dependencyCount = 1;
    render_pass_create_info.pDependencies = &dependency;

    CheckVkResult(vkCreateRenderPass(context->logical_device,
                                     &render_pass_create_info,
                                     nullptr, &context->render_pass));

    Vulkan_Swapchain_Support *swapchain_support = &context->swapchain_support;

    swapchain_support->surface_format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device,
                                         context->surface,
                                         &swapchain_support->surface_format_count,
                                         nullptr);

    HE_Assert(swapchain_support->surface_format_count);

    swapchain_support->surface_formats = ArenaPushArray(arena,
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

    HE_Assert(swapchain_support->present_mode_count);

    swapchain_support->present_modes = ArenaPushArray(arena,
                                                      VkPresentModeKHR,
                                                      swapchain_support->present_mode_count);

    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device,
                                              context->surface,
                                              &swapchain_support->present_mode_count,
                                              swapchain_support->present_modes);

    VkSemaphoreCreateInfo semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (U32 sync_primitive_index = 0;
         sync_primitive_index < HE_Max_Frames_In_Flight;
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

    U32 width = 1280;
    U32 height = 720;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    U32 min_image_count = 2;
    bool swapchain_created = create_swapchain(context, width, height,
                                              min_image_count, present_mode, &context->swapchain);
    HE_Assert(swapchain_created);
    HE_Assert(color_attachment.format == context->swapchain.image_format);

    {
        Scoped_Temprary_Memory_Arena temp_arena(arena);

        Read_Entire_File_Result result =
            platform_begin_read_entire_file("shaders/basic.vert.spv");

        if (result.success)
        {
            U8 *data = ArenaPushArray(&temp_arena, U8, result.size);
            HE_Assert(data);

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
            U8 *data = ArenaPushArray(&temp_arena, U8, result.size);
            HE_Assert(data);
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

    VkPipelineShaderStageCreateInfo vertex_shader_stage_info
        = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vertex_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_shader_stage_info.module = context->vertex_shader_module;
    vertex_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo fragment_shader_stage_info
        = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    fragment_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragment_shader_stage_info.module = context->fragment_shader_module;
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
    dynamic_state_create_info.dynamicStateCount = HE_ArrayCount(dynamic_states);
    dynamic_state_create_info.pDynamicStates = dynamic_states;

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertex_input_state_create_info.vertexBindingDescriptionCount = 0;
    vertex_input_state_create_info.pVertexBindingDescriptions = nullptr;
    vertex_input_state_create_info.vertexAttributeDescriptionCount = 0;
    vertex_input_state_create_info.pVertexAttributeDescriptions = nullptr;

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
    rasterization_state_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
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

    VkPipelineLayoutCreateInfo pipeline_layout_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipeline_layout_create_info.setLayoutCount = 0;
    pipeline_layout_create_info.pSetLayouts = nullptr;
    pipeline_layout_create_info.pushConstantRangeCount = 0;
    pipeline_layout_create_info.pPushConstantRanges = nullptr;

    CheckVkResult(vkCreatePipelineLayout(context->logical_device,
                                         &pipeline_layout_create_info,
                                         nullptr, &context->pipeline_layout));

    VkGraphicsPipelineCreateInfo graphics_pipeline_create_info =
        { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    graphics_pipeline_create_info.stageCount = 2;
    graphics_pipeline_create_info.pStages = shader_stages;
    graphics_pipeline_create_info.pVertexInputState = &vertex_input_state_create_info;
    graphics_pipeline_create_info.pInputAssemblyState = &input_assembly_state_create_info;
    graphics_pipeline_create_info.pViewportState = &viewport_state_create_info;
    graphics_pipeline_create_info.pRasterizationState = &rasterization_state_create_info;
    graphics_pipeline_create_info.pMultisampleState = &multisampling_state_create_info;
    graphics_pipeline_create_info.pDepthStencilState = nullptr;
    graphics_pipeline_create_info.pColorBlendState = &color_blend_state_create_info;
    graphics_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    graphics_pipeline_create_info.layout = context->pipeline_layout;
    graphics_pipeline_create_info.renderPass = context->render_pass;
    graphics_pipeline_create_info.subpass = 0;
    graphics_pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    graphics_pipeline_create_info.basePipelineIndex = -1;

    CheckVkResult(vkCreateGraphicsPipelines(context->logical_device, VK_NULL_HANDLE,
                                            1, &graphics_pipeline_create_info,
                                            nullptr, &context->graphics_pipeline));


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

    context->current_frame_index = 0;
    context->frames_in_flight = 2;
    HE_Assert(context->frames_in_flight <= HE_Max_Frames_In_Flight);

    return true;
}

internal_function void
vulkan_draw(Vulkan_Context *context, U32 width, U32 height)
{
    vkWaitForFences(context->logical_device,
                    1, &context->frame_in_flight_fences[context->current_frame_index],
                    VK_TRUE, UINT64_MAX);

    if ((width != context->swapchain.width || height != context->swapchain.height) && width != 0 && height != 0)
    {
        recreate_swapchain(context,
                           &context->swapchain,
                           width,
                           height,
                           context->swapchain.present_mode);
        return;
    }

    U32 image_index = 0;
    VkResult result = vkAcquireNextImageKHR(context->logical_device,
                                            context->swapchain.handle,
                                            UINT64_MAX,
                                            context->image_available_semaphores[context->current_frame_index],
                                            VK_NULL_HANDLE,
                                            &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        if (width != 0 && height != 0)
        {
            recreate_swapchain(context,
                               &context->swapchain,
                               width,
                               height,
                               context->swapchain.present_mode);

        }
        return;
    }
    else
    {
        HE_Assert(result == VK_SUCCESS);
    }

    vkResetFences(context->logical_device, 1, &context->frame_in_flight_fences[context->current_frame_index]);
    vkResetCommandBuffer(context->graphics_command_buffers[context->current_frame_index], 0);

    VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(context->graphics_command_buffers[context->current_frame_index],
                         &command_buffer_begin_info);

    VkClearValue clear_value = {};
    clear_value.color = { 1.0f, 0.0f, 1.0f, 1.0f };

    VkRenderPassBeginInfo render_pass_begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    render_pass_begin_info.renderPass = context->render_pass;
    render_pass_begin_info.framebuffer = context->swapchain.frame_buffers[image_index];
    render_pass_begin_info.renderArea.offset = { 0, 0 };
    render_pass_begin_info.renderArea.extent = { context->swapchain.width, context->swapchain.height };
    render_pass_begin_info.clearValueCount = 1;
    render_pass_begin_info.pClearValues = &clear_value;

    vkCmdBeginRenderPass(context->graphics_command_buffers[context->current_frame_index],
                         &render_pass_begin_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(context->graphics_command_buffers[context->current_frame_index],
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      context->graphics_pipeline);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (F32)context->swapchain.width;
    viewport.height = (F32)context->swapchain.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(context->graphics_command_buffers[context->current_frame_index],
                     0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = context->swapchain.width;
    scissor.extent.height = context->swapchain.height;
    vkCmdSetScissor(context->graphics_command_buffers[context->current_frame_index],
                    0, 1, &scissor);

    vkCmdDraw(context->graphics_command_buffers[context->current_frame_index],
              3, 1, 0, 0);

    vkCmdEndRenderPass(context->graphics_command_buffers[context->current_frame_index]);
    vkEndCommandBuffer(context->graphics_command_buffers[context->current_frame_index]);

    VkPipelineStageFlags wait_stage =  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

    submit_info.pWaitDstStageMask = &wait_stage;

    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &context->image_available_semaphores[context->current_frame_index];

    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &context->rendering_finished_semaphores[context->current_frame_index];

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &context->graphics_command_buffers[context->current_frame_index];

    vkQueueSubmit(context->graphics_queue, 1, &submit_info, context->frame_in_flight_fences[context->current_frame_index]);

    VkPresentInfoKHR present_info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &context->rendering_finished_semaphores[context->current_frame_index];

    present_info.swapchainCount = 1;
    present_info.pSwapchains = &context->swapchain.handle;
    present_info.pImageIndices = &image_index;

    result = vkQueuePresentKHR(context->present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        if (width != 0 && height != 0)
        {
            recreate_swapchain(context,
                               &context->swapchain,
                               width,
                               height,
                               context->swapchain.present_mode);
        }
    }
    else
    {
        HE_Assert(result == VK_SUCCESS);
    }

    context->current_frame_index++;
    if (context->current_frame_index == context->frames_in_flight) context->current_frame_index = 0;
}

internal_function void
deinit_vulkan(Vulkan_Context *context)
{
    vkDeviceWaitIdle(context->logical_device);

    for (U32 sync_primitive_index = 0;
         sync_primitive_index < HE_Max_Frames_In_Flight;
         sync_primitive_index++)
    {
        vkDestroySemaphore(context->logical_device,
                           context->image_available_semaphores[sync_primitive_index],
                           nullptr);

        vkDestroySemaphore(context->logical_device,
                           context->rendering_finished_semaphores[sync_primitive_index],
                           nullptr);

        vkDestroyFence(context->logical_device,
                       context->frame_in_flight_fences[sync_primitive_index],
                       nullptr);
    }

    vkDestroyCommandPool(context->logical_device, context->graphics_command_pool, nullptr);

    destroy_swapchain(context->logical_device, &context->swapchain);

    vkDestroyPipeline(context->logical_device, context->graphics_pipeline, nullptr);
    vkDestroyRenderPass(context->logical_device, context->render_pass, nullptr);
    vkDestroyPipelineLayout(context->logical_device, context->pipeline_layout, nullptr);
    vkDestroyShaderModule(context->logical_device, context->vertex_shader_module, nullptr);
    vkDestroyShaderModule(context->logical_device, context->fragment_shader_module, nullptr);

    vkDestroySurfaceKHR(context->instance, context->surface, nullptr);
    vkDestroyDevice(context->logical_device, nullptr);

#if HE_VULKAN_DEBUGGING
     PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerExt =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance,
                                                                  "vkDestroyDebugUtilsMessengerEXT");
    HE_Assert(vkDestroyDebugUtilsMessengerExt);
    vkDestroyDebugUtilsMessengerExt(context->instance,
                                    context->debug_messenger,
                                    nullptr);
#endif

    vkDestroyInstance(context->instance, nullptr);
}