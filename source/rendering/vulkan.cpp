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
    (void)message_type;
    (void)user_data;
    if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        HE_DebugPrintf(Rendering, Trace, "%s\n", callback_data->pMessage);
    }
    return VK_FALSE;
}

internal_function bool
init_vulkan(Vulkan_Context *context, HWND window, HINSTANCE instance, Memory_Arena *arena)
{
    VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app_info.pApplicationName = "Hope";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName = "Hope";
    app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.apiVersion = VK_API_VERSION_1_0;

    const char *extensions[] =
    {
        "VK_KHR_surface",

#if HE_OS_WINDOWS
        "VK_KHR_win32_surface",
#endif

#if HE_VULKAN_DEBUGGING
        "VK_EXT_debug_utils",
#endif
    };

    VkInstanceCreateInfo instance_create_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instance_create_info.pApplicationInfo = &app_info;
    instance_create_info.enabledExtensionCount = HE_ArrayCount(extensions);
    instance_create_info.ppEnabledExtensionNames = extensions;

#if HE_VULKAN_DEBUGGING
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info =
        { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };

    debug_messenger_create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    debug_messenger_create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug_messenger_create_info.pfnUserCallback = vulkan_debug_callback;
    debug_messenger_create_info.pUserData = nullptr;

    const char *layers[] =
    {
        "VK_LAYER_KHRONOS_validation",
    };

    instance_create_info.enabledLayerCount = HE_ArrayCount(layers);
    instance_create_info.ppEnabledLayerNames = layers;
    instance_create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debug_messenger_create_info;
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

#if HE_OS_WINDOWS
    VkWin32SurfaceCreateInfoKHR surface_create_info = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    surface_create_info.hwnd = window;
    surface_create_info.hinstance = instance;
    CheckVkResult(vkCreateWin32SurfaceKHR(context->instance, &surface_create_info, nullptr, &context->surface));
#endif

    U32 physical_device_count = 0;
    vkEnumeratePhysicalDevices(context->instance, &physical_device_count, nullptr);

    if (physical_device_count == 0)
    {
        // todo(amer): platform report error and exit
        return false;
    }

    Scoped_Temprary_Memory_Arena temp_arena(arena);

    VkPhysicalDevice *physical_devices = ArenaPushArray(&temp_arena,
                                                        VkPhysicalDevice,
                                                        physical_device_count);
    vkEnumeratePhysicalDevices(context->instance,
                               &physical_device_count,
                               physical_devices);

    // note(amer): don't be lazy and just use a regular function please
    auto rate_physical_device = [&temp_arena](VkPhysicalDevice physical_device, VkSurfaceKHR surface) -> U32
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical_device, &properties);

        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(physical_device, &features);

        // todo(amer): add more checking as we do features in the future for now we need a discrete gpu
        U32 score = 0;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            score++;
        }

        U32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                                 &queue_family_count,
                                                 nullptr);

        VkQueueFamilyProperties *queue_families = ArenaPushArray(&temp_arena,
                                                                 VkQueueFamilyProperties,
                                                                 queue_family_count);

        vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                                 &queue_family_count,
                                                 queue_families);

        bool can_device_do_graphics = false;
        bool can_present = false;

        for (U32 queue_family_index = 0;
             queue_family_index < queue_family_count;
             queue_family_index++)
        {
            VkQueueFamilyProperties *queue_family = &queue_families[queue_family_index];

            if (queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                can_device_do_graphics = true;
            }

            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device,
                                                 queue_family_index,
                                                 surface, &present_support);

            if (present_support)
            {
                can_present = true;
            }
        }

        if (!can_device_do_graphics || !can_present)
        {
            return 0;
        }

        return score;
    };

    context->physical_device = VK_NULL_HANDLE;
    U32 best_physical_device_so_far = 0;

    for (U32 physical_device_index = 0;
         physical_device_index < physical_device_count;
         physical_device_index++)
    {
        VkPhysicalDevice *physical_device = &physical_devices[physical_device_index];
        U32 physical_device_score = rate_physical_device(*physical_device, context->surface);

        if (physical_device_score > best_physical_device_so_far)
        {
            best_physical_device_so_far = physical_device_score;
            context->physical_device = *physical_device;
        }
    }

    if (context->physical_device == VK_NULL_HANDLE)
    {
        // todo(amer): platform report error and exit
        return false;
    }

    U32 graphics_queue_family = 0;
    U32 present_queue_family = 0;

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

        bool can_queue_family_do_graphics = (queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT);

        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(context->physical_device,
                                             queue_family_index,
                                             context->surface, &present_support);

        bool can_queue_family_present = present_support == VK_TRUE;

        if (can_queue_family_do_graphics && can_queue_family_present)
        {
            graphics_queue_family = queue_family_index;
            present_queue_family = queue_family_index;
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
                graphics_queue_family = queue_family_index;
            }

            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(context->physical_device,
                                                 queue_family_index,
                                                 context->surface, &present_support);
            if (present_support == VK_TRUE)
            {
                present_queue_family = queue_family_index;
            }
        }
    }

    F32 queue_priority = 1.0f;

    VkDeviceQueueCreateInfo *queue_create_infos = ArenaPushArray(&temp_arena,
                                                                 VkDeviceQueueCreateInfo,
                                                                 4);

    queue_create_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_infos[0].queueFamilyIndex = graphics_queue_family;
    queue_create_infos[0].queueCount = 1;
    queue_create_infos[0].pQueuePriorities = &queue_priority;

    U32 queue_create_info_count = 1;

    if (!found_a_queue_family_that_can_do_graphics_and_present)
    {
        queue_create_infos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[1].queueFamilyIndex = present_queue_family;
        queue_create_infos[1].queueCount = 1;
        queue_create_infos[1].pQueuePriorities = &queue_priority;
        queue_create_info_count = 2;
    }

    VkPhysicalDeviceFeatures physical_device_features = {};

    const char *device_extensions[] =
    {
        "VK_KHR_swapchain",
    };

    U32 extension_count = 0;
    vkEnumerateDeviceExtensionProperties(context->physical_device,
                                         nullptr, &extension_count,
                                         nullptr);

    VkExtensionProperties *extension_properties = ArenaPushArray(&temp_arena,
                                                                 VkExtensionProperties,
                                                                 extension_count);

    vkEnumerateDeviceExtensionProperties(context->physical_device,
                                         nullptr, &extension_count,
                                         extension_properties);

    // todo(amer): check if the device extenions are supported

    VkDeviceCreateInfo device_create_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    device_create_info.pQueueCreateInfos = queue_create_infos;
    device_create_info.queueCreateInfoCount = queue_create_info_count;
    device_create_info.pEnabledFeatures = &physical_device_features;
    device_create_info.ppEnabledExtensionNames = device_extensions;
    device_create_info.enabledExtensionCount = HE_ArrayCount(device_extensions);

    CheckVkResult(vkCreateDevice(context->physical_device,
                                 &device_create_info, nullptr,
                                 &context->logical_device));

    vkGetDeviceQueue(context->logical_device,
                     graphics_queue_family,
                     0, &context->graphics_queue);

    vkGetDeviceQueue(context->logical_device,
                     present_queue_family,
                     0, &context->present_queue);

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device,
                                              context->surface,
                                              &surface_capabilities);

    U32 format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device,
                                         context->surface,
                                         &format_count,
                                         nullptr);

    VkSurfaceFormatKHR *surface_formats = ArenaPushArray(&temp_arena,
                                                         VkSurfaceFormatKHR,
                                                         format_count);

    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device,
                                         context->surface,
                                         &format_count,
                                         surface_formats);

    Vulkan_Swapchain *swapchain = &context->swapchain;
    swapchain->surface_format = surface_formats[0];

    for (U32 surface_format_index = 0;
         surface_format_index < format_count;
         surface_format_index++)
    {
        VkSurfaceFormatKHR *surface_format = &surface_formats[surface_format_index];
        if (surface_format->format == VK_FORMAT_B8G8R8A8_SRGB &&
            surface_format->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            swapchain->surface_format = *surface_format;
        }
    }

    U32 present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device,
                                              context->surface,
                                              &present_mode_count,
                                              nullptr);

    VkPresentModeKHR *present_modes = ArenaPushArray(&temp_arena,
                                                     VkPresentModeKHR,
                                                     present_mode_count);

    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device,
                                              context->surface,
                                              &present_mode_count,
                                              present_modes);

    // todo(amer): hardcoding for now...
    swapchain->width = HE_Clamp(1280, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
    swapchain->height = HE_Clamp(720, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);
    VkExtent2D extent = { swapchain->width, swapchain->height };

    swapchain->present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    HE_Assert(surface_capabilities.minImageCount + 1 >= 3);
    swapchain->image_count = 3;

    VkSwapchainCreateInfoKHR swapchain_create_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swapchain_create_info.surface = context->surface;
    swapchain_create_info.minImageCount = swapchain->image_count;
    swapchain_create_info.imageFormat = swapchain->surface_format.format;
    swapchain_create_info.imageColorSpace = swapchain->surface_format.colorSpace;
    swapchain_create_info.imageExtent = extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (graphics_queue_family != present_queue_family)
    {
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        U32 queue_family_indices[2] = { graphics_queue_family, present_queue_family };
        swapchain_create_info.queueFamilyIndexCount = 2;
        swapchain_create_info.pQueueFamilyIndices = queue_family_indices;
    }
    else
    {
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    swapchain_create_info.preTransform = surface_capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = swapchain->present_mode;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;

    CheckVkResult(vkCreateSwapchainKHR(context->logical_device,
                                       &swapchain_create_info,
                                       nullptr,
                                       &context->swapchain.handle));

    vkGetSwapchainImagesKHR(context->logical_device,
                            context->swapchain.handle,
                            &context->swapchain.image_count,
                            nullptr);

    HE_Assert(swapchain->image_count <= HE_ArrayCount(swapchain->images));
    vkGetSwapchainImagesKHR(context->logical_device,
                            swapchain->handle,
                            &swapchain->image_count,
                            swapchain->images);

    for (U32 image_index = 0; image_index < swapchain->image_count; image_index++)
    {
        VkImageViewCreateInfo image_view_create_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        image_view_create_info.image = swapchain->images[image_index];
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = swapchain->surface_format.format;
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

    {
        Read_Entire_File_Result result = platform_begin_read_entire_file("shaders/basic.vert.spv");
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
                                                   nullptr, &context->vertex_shader_module));
            }
        }
    }

    {
        Read_Entire_File_Result result = platform_begin_read_entire_file("shaders/basic.frag.spv");
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
                                                   nullptr, &context->fragment_shader_module));
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
    color_blend_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
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

    VkAttachmentDescription color_attachment = {};
    color_attachment.format = context->swapchain.surface_format.format;
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

    VkRenderPassCreateInfo render_pass_create_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    render_pass_create_info.attachmentCount = 1;
    render_pass_create_info.pAttachments = &color_attachment;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;

    CheckVkResult(vkCreateRenderPass(context->logical_device,
                  &render_pass_create_info, nullptr, &context->render_pass));

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

    for (U32 image_index = 0; image_index < swapchain->image_count; image_index++)
    {
        VkFramebufferCreateInfo frame_buffer_create_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        frame_buffer_create_info.renderPass = context->render_pass;
        frame_buffer_create_info.attachmentCount = 1;
        frame_buffer_create_info.pAttachments = &context->swapchain.image_views[image_index];
        frame_buffer_create_info.width = context->swapchain.width;
        frame_buffer_create_info.height = context->swapchain.height;
        frame_buffer_create_info.layers = 1;
        CheckVkResult(vkCreateFramebuffer(context->logical_device,
                                          &frame_buffer_create_info,
                                          nullptr, &context->frame_buffers[image_index]));
    }

    VkCommandPoolCreateInfo graphics_command_pool_create_info
        = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    graphics_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    graphics_command_pool_create_info.queueFamilyIndex = graphics_queue_family;

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

    VkSemaphoreCreateInfo semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (U32 sync_primitive_index = 0;
         sync_primitive_index < 3;
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

    context->current_frame_index = 0;
    return true;
}

internal_function void
vulkan_draw(Vulkan_Context *context)
{
    vkWaitForFences(context->logical_device,
                    1, &context->frame_in_flight_fences[context->current_frame_index],
                    VK_TRUE, UINT64_MAX);

    vkResetFences(context->logical_device, 1, &context->frame_in_flight_fences[context->current_frame_index]);

    U32 image_index = 0;
    VkResult result = vkAcquireNextImageKHR(context->logical_device,
                                            context->swapchain.handle,
                                            UINT64_MAX,
                                            context->image_available_semaphores[context->current_frame_index],
                                            VK_NULL_HANDLE,
                                            &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
    }
    else if (result != VK_SUCCESS || result != VK_SUBOPTIMAL_KHR)
    {
    }

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
    render_pass_begin_info.framebuffer = context->frame_buffers[context->current_frame_index];
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

    // todo(amer): what is VkPipelineStageFlags ?
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &context->image_available_semaphores[context->current_frame_index];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &context->rendering_finished_semaphores[context->current_frame_index];
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &context->graphics_command_buffers[context->current_frame_index];
    submit_info.pWaitDstStageMask = wait_stages;

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
        // todo(amer): resize
    }

    context->current_frame_index++;
    if (context->current_frame_index == 3) context->current_frame_index = 0;
}

internal_function void
deinit_vulkan(Vulkan_Context *context)
{
    vkDeviceWaitIdle(context->logical_device);

    for (U32 sync_primitive_index = 0;
         sync_primitive_index < 3;
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

    for (U32 frame_buffer_index = 0;
         frame_buffer_index < 3;
         frame_buffer_index++)
    {
        vkDestroyFramebuffer(context->logical_device,
                             context->frame_buffers[frame_buffer_index],
                             nullptr);
    }

    vkDestroyPipeline(context->logical_device, context->graphics_pipeline, nullptr);
    vkDestroyRenderPass(context->logical_device, context->render_pass, nullptr);
    vkDestroyPipelineLayout(context->logical_device, context->pipeline_layout, nullptr);
    vkDestroyShaderModule(context->logical_device, context->vertex_shader_module, nullptr);
    vkDestroyShaderModule(context->logical_device, context->fragment_shader_module, nullptr);

    for (U32 image_index = 0;
         image_index < context->swapchain.image_count;
         image_index++)
    {
        vkDestroyImageView(context->logical_device,
                           context->swapchain.image_views[image_index],
                           nullptr);
    }

    vkDestroySwapchainKHR(context->logical_device, context->swapchain.handle, nullptr);

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