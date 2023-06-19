#include <string.h>

#include "vulkan_renderer.h"
#include "core/platform.h"
#include "core/debugging.h"
#include "core/memory.h"
#include "rendering/renderer.h"
#include "core/engine.h"

#include "vulkan_image.h"
#include "vulkan_buffer.h"
#include "vulkan_swapchain.h"
#include "vulkan_shader.h"

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

S32
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
            // todo(amer): we should track how much memory we allocated from heaps so allocations don't fail
            const VkMemoryType *memory_type =
                &context->physical_device_memory_properties.memoryTypes[memory_type_index];
            if ((memory_type->propertyFlags & memory_property_flags) == memory_property_flags)
            {
                result = (S32)memory_type_index;
                break;
            }
        }
    }

    return result;
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

        VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features =
            { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };

        VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        features2.pNext = &descriptor_indexing_features;
        vkGetPhysicalDeviceFeatures2(*current_physical_device, &features2);

        if (!features2.features.samplerAnisotropy ||
            !features2.features.sampleRateShading ||
            !descriptor_indexing_features.runtimeDescriptorArray ||
            !descriptor_indexing_features.descriptorBindingPartiallyBound ||
            !descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind)
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
init_vulkan(Vulkan_Context *context, Engine *engine, Memory_Arena *arena)
{
    context->allocator = &engine->memory.free_list_allocator;

    const char *required_instance_extensions[] =
    {
#if HE_OS_WINDOWS
        "VK_KHR_win32_surface",
#endif

#if HE_VULKAN_DEBUGGING
        "VK_EXT_debug_utils",
#endif
        "VK_KHR_surface",
    };

    U32 required_api_version = VK_API_VERSION_1_1;
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

    VkSampleCountFlags counts = context->physical_device_properties.limits.framebufferColorSampleCounts &
                                context->physical_device_properties.limits.framebufferDepthSampleCounts;

    VkSampleCountFlagBits max_sample_count = VK_SAMPLE_COUNT_1_BIT;

    if (counts & VK_SAMPLE_COUNT_64_BIT)
    {
        max_sample_count = VK_SAMPLE_COUNT_64_BIT;
    }
    else if (counts & VK_SAMPLE_COUNT_32_BIT)
    {
        max_sample_count = VK_SAMPLE_COUNT_32_BIT;
    }
    else if (counts & VK_SAMPLE_COUNT_16_BIT)
    {
        max_sample_count = VK_SAMPLE_COUNT_16_BIT;
    }
    else if (counts & VK_SAMPLE_COUNT_8_BIT)
    {
        max_sample_count = VK_SAMPLE_COUNT_8_BIT;
    }
    else if (counts & VK_SAMPLE_COUNT_4_BIT)
    {
        max_sample_count = VK_SAMPLE_COUNT_4_BIT;
    }
    else if (counts & VK_SAMPLE_COUNT_2_BIT)
    {
        max_sample_count = VK_SAMPLE_COUNT_2_BIT;
    }

    context->msaa_samples = VK_SAMPLE_COUNT_8_BIT;

    if (context->msaa_samples > max_sample_count)
    {
        context->msaa_samples = max_sample_count;
    }

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

        context->transfer_queue_family_index = context->graphics_queue_family_index;

        for (U32 queue_family_index = 0;
             queue_family_index < queue_family_count;
             queue_family_index++)
        {
            VkQueueFamilyProperties *queue_family = &queue_families[queue_family_index];
            if ((queue_family->queueFlags & VK_QUEUE_TRANSFER_BIT) && !(queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                context->transfer_queue_family_index = queue_family_index;
                break;
            }
        }

        F32 queue_priority = 1.0f;
        VkDeviceQueueCreateInfo *queue_create_infos = AllocateArray(&temp_arena,
                                                                    VkDeviceQueueCreateInfo,
                                                                    3);

        queue_create_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[0].queueFamilyIndex = context->graphics_queue_family_index;
        queue_create_infos[0].queueCount = 1;
        queue_create_infos[0].pQueuePriorities = &queue_priority;

        U32 queue_create_info_count = 1;

        if (!found_a_queue_family_that_can_do_graphics_and_present)
        {
            U32 queue_create_info_index = queue_create_info_count++;
            queue_create_infos[queue_create_info_index].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_infos[queue_create_info_index].queueFamilyIndex = context->present_queue_family_index;
            queue_create_infos[queue_create_info_index].queueCount = 1;
            queue_create_infos[queue_create_info_index].pQueuePriorities = &queue_priority;
        }

        if ((context->transfer_queue_family_index != context->graphics_queue_family_index)
            && (context->transfer_queue_family_index != context->present_queue_family_index))
        {
            U32 queue_create_info_index = queue_create_info_count++;
            queue_create_infos[queue_create_info_index].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_infos[queue_create_info_index].queueFamilyIndex = context->transfer_queue_family_index;
            queue_create_infos[queue_create_info_index].queueCount = 1;
            queue_create_infos[queue_create_info_index].pQueuePriorities = &queue_priority;
        }

        VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features =
            { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };

        descriptor_indexing_features.runtimeDescriptorArray = VK_TRUE;
        descriptor_indexing_features.descriptorBindingPartiallyBound = VK_TRUE;
        descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

        VkPhysicalDeviceFeatures2 physical_device_features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        physical_device_features2.features.samplerAnisotropy = VK_TRUE;
        physical_device_features2.features.sampleRateShading = VK_TRUE;
        physical_device_features2.pNext = &descriptor_indexing_features;

        const char *required_device_extensions[] =
        {
            "VK_KHR_swapchain",
            "VK_KHR_push_descriptor",
            "VK_EXT_descriptor_indexing"
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
        device_create_info.pNext = &physical_device_features2;
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

        vkGetDeviceQueue(context->logical_device,
                         context->transfer_queue_family_index,
                         0, &context->transfer_queue);
    }

    VkFormat image_formats[] =
    {
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_SRGB
    };

    VkFormat depth_stencil_formats[] =
    {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    init_swapchain_support(context,
                           image_formats,
                           ArrayCount(image_formats),
                           depth_stencil_formats,
                           ArrayCount(depth_stencil_formats),
                           VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                           arena,
                           &context->swapchain_support);

    VkAttachmentDescription attachments[3] = {};

    attachments[0].format = context->swapchain_support.image_format;
    attachments[0].samples = context->msaa_samples;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[1].format = context->swapchain_support.image_format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[2].format = context->swapchain_support.depth_stencil_format;
    attachments[2].samples = context->msaa_samples;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference resolve_color_attachment_ref = {};
    resolve_color_attachment_ref.attachment = 1;
    resolve_color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_stencil_attachment_ref = {};
    depth_stencil_attachment_ref.attachment = 2;
    depth_stencil_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pResolveAttachments = &resolve_color_attachment_ref;

    subpass.pDepthStencilAttachment = &depth_stencil_attachment_ref;

    VkSubpassDependency dependency = {};

    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;

    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_create_info =
        { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    render_pass_create_info.attachmentCount = ArrayCount(attachments);
    render_pass_create_info.pAttachments = attachments;

    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;

    render_pass_create_info.dependencyCount = 1;
    render_pass_create_info.pDependencies = &dependency;

    CheckVkResult(vkCreateRenderPass(context->logical_device,
                                     &render_pass_create_info,
                                     nullptr, &context->render_pass));
    
    U32 width = 1280;
    U32 height = 720;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    U32 min_image_count = MAX_FRAMES_IN_FLIGHT;
    bool swapchain_created = create_swapchain(context, width, height,
                                              min_image_count, present_mode, &context->swapchain);
    Assert(swapchain_created);

    bool shader_loaded = load_shader(&context->vertex_shader, "shaders/mesh.vert.spv", context, arena);
    Assert(shader_loaded);

    shader_loaded = load_shader(&context->fragment_shader, "shaders/mesh.frag.spv", context, arena);
    Assert(shader_loaded);

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
    graphics_command_buffer_allocate_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    CheckVkResult(vkAllocateCommandBuffers(context->logical_device,
                                           &graphics_command_buffer_allocate_info,
                                           context->graphics_command_buffers));

    VkCommandPoolCreateInfo transfer_command_pool_create_info
        = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };

    transfer_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    transfer_command_pool_create_info.queueFamilyIndex = context->transfer_queue_family_index;

    CheckVkResult(vkCreateCommandPool(context->logical_device,
                                      &transfer_command_pool_create_info,
                                      nullptr, &context->transfer_command_pool));

    VkCommandBufferAllocateInfo transfer_command_buffer_allocate_info
        = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };

    transfer_command_buffer_allocate_info.commandPool = context->transfer_command_pool;
    transfer_command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    transfer_command_buffer_allocate_info.commandBufferCount = 1;

    CheckVkResult(vkAllocateCommandBuffers(context->logical_device,
                                           &transfer_command_buffer_allocate_info,
                                           &context->transfer_command_buffer));

    U64 vertex_size = HE_MegaBytes(512);
    create_buffer(&context->vertex_buffer, context, vertex_size,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    U64 index_size = HE_MegaBytes(128);
    create_buffer(&context->index_buffer, context, index_size,
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    create_buffer(&context->transfer_buffer, context, HE_MegaBytes(128),
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    for (U32 frame_index = 0; frame_index < MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        Vulkan_Buffer *global_uniform_buffer = &context->global_uniform_buffers[frame_index];
        create_buffer(global_uniform_buffer, context, sizeof(Vulkan_Global_Uniform_Buffer),
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        Vulkan_Buffer *object_storage_buffer = &context->object_storage_buffers[frame_index];
        create_buffer(object_storage_buffer, context, sizeof(Vulkan_Object_Data) * MAX_OBJECT_DATA_COUNT,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        Vulkan_Buffer *material_storage_buffer = &context->material_storage_buffers[frame_index];
        create_buffer(material_storage_buffer, context, sizeof(Vulkan_Material_Data) * MAX_MATERIAL_COUNT,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    VkDescriptorPoolSize descriptor_pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURE_COUNT }
    };

    VkDescriptorPoolCreateInfo descriptor_pool_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descriptor_pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    descriptor_pool_create_info.poolSizeCount = ArrayCount(descriptor_pool_sizes);
    descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes;
    descriptor_pool_create_info.maxSets = (16 + 16 + MAX_TEXTURE_COUNT) * ArrayCount(descriptor_pool_sizes);

    CheckVkResult(vkCreateDescriptorPool(context->logical_device,
                                         &descriptor_pool_create_info,
                                         nullptr, &context->descriptor_pool));

    // Per Frame Descriptor Sets
    {
        VkDescriptorSetLayoutBinding per_frame_descriptor_set_layout_bindings[3] = {};
        per_frame_descriptor_set_layout_bindings[0].binding = 0;
        per_frame_descriptor_set_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        per_frame_descriptor_set_layout_bindings[0].descriptorCount = 1;
        per_frame_descriptor_set_layout_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        per_frame_descriptor_set_layout_bindings[1].binding = 1;
        per_frame_descriptor_set_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        per_frame_descriptor_set_layout_bindings[1].descriptorCount = 1;
        per_frame_descriptor_set_layout_bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        per_frame_descriptor_set_layout_bindings[2].binding = 2;
        per_frame_descriptor_set_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        per_frame_descriptor_set_layout_bindings[2].descriptorCount = 1;
        per_frame_descriptor_set_layout_bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo per_frame_descriptor_set_layout_create_info =
            { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };

        per_frame_descriptor_set_layout_create_info.bindingCount = ArrayCount(per_frame_descriptor_set_layout_bindings);
        per_frame_descriptor_set_layout_create_info.pBindings = per_frame_descriptor_set_layout_bindings;

        CheckVkResult(vkCreateDescriptorSetLayout(context->logical_device,
                                                  &per_frame_descriptor_set_layout_create_info,
                                                  nullptr,
                                                  &context->per_frame_descriptor_set_layout));

        VkDescriptorSetLayout per_frame_descriptor_set_layouts[MAX_FRAMES_IN_FLIGHT] = {};

        for (U32 frame_index = 0;
             frame_index < MAX_FRAMES_IN_FLIGHT;
             frame_index++)
        {
            per_frame_descriptor_set_layouts[frame_index] = context->per_frame_descriptor_set_layout;
        }

        VkDescriptorSetAllocateInfo descriptor_set_allocation_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        descriptor_set_allocation_info.descriptorPool = context->descriptor_pool;
        descriptor_set_allocation_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        descriptor_set_allocation_info.pSetLayouts = per_frame_descriptor_set_layouts;

        CheckVkResult(vkAllocateDescriptorSets(context->logical_device,
                                               &descriptor_set_allocation_info,
                                               context->per_frame_descriptor_sets));

        for (U32 frame_index = 0;
             frame_index < MAX_FRAMES_IN_FLIGHT;
             frame_index++)
        {
            VkDescriptorBufferInfo global_uniform_buffer_descriptor_info = {};
            global_uniform_buffer_descriptor_info.buffer = context->global_uniform_buffers[frame_index].handle;
            global_uniform_buffer_descriptor_info.offset = 0;
            global_uniform_buffer_descriptor_info.range = sizeof(Vulkan_Global_Uniform_Buffer);

            VkWriteDescriptorSet global_uniform_buffer_write_descriptor_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            global_uniform_buffer_write_descriptor_set.dstSet = context->per_frame_descriptor_sets[frame_index];
            global_uniform_buffer_write_descriptor_set.dstBinding = 0;
            global_uniform_buffer_write_descriptor_set.dstArrayElement = 0;
            global_uniform_buffer_write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            global_uniform_buffer_write_descriptor_set.descriptorCount = 1;
            global_uniform_buffer_write_descriptor_set.pBufferInfo = &global_uniform_buffer_descriptor_info;

            VkDescriptorBufferInfo object_data_storage_buffer_descriptor_info = {};
            object_data_storage_buffer_descriptor_info.buffer = context->object_storage_buffers[frame_index].handle;
            object_data_storage_buffer_descriptor_info.offset = 0;
            object_data_storage_buffer_descriptor_info.range = sizeof(Vulkan_Object_Data) * MAX_OBJECT_DATA_COUNT;

            VkWriteDescriptorSet object_data_storage_buffer_write_descriptor_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            object_data_storage_buffer_write_descriptor_set.dstSet = context->per_frame_descriptor_sets[frame_index];
            object_data_storage_buffer_write_descriptor_set.dstBinding = 1;
            object_data_storage_buffer_write_descriptor_set.dstArrayElement = 0;
            object_data_storage_buffer_write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            object_data_storage_buffer_write_descriptor_set.descriptorCount = 1;
            object_data_storage_buffer_write_descriptor_set.pBufferInfo = &object_data_storage_buffer_descriptor_info;

            VkDescriptorBufferInfo material_storage_buffer_descriptor_info = {};
            material_storage_buffer_descriptor_info.buffer = context->material_storage_buffers[frame_index].handle;
            material_storage_buffer_descriptor_info.offset = 0;
            material_storage_buffer_descriptor_info.range = sizeof(Vulkan_Material_Data) * MAX_MATERIAL_COUNT;

            VkWriteDescriptorSet material_storage_buffer_write_descriptor_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            material_storage_buffer_write_descriptor_set.dstSet = context->per_frame_descriptor_sets[frame_index];
            material_storage_buffer_write_descriptor_set.dstBinding = 2;
            material_storage_buffer_write_descriptor_set.dstArrayElement = 0;
            material_storage_buffer_write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            material_storage_buffer_write_descriptor_set.descriptorCount = 1;
            material_storage_buffer_write_descriptor_set.pBufferInfo = &material_storage_buffer_descriptor_info;

            VkWriteDescriptorSet write_descriptor_sets[] =
            {
                global_uniform_buffer_write_descriptor_set,
                object_data_storage_buffer_write_descriptor_set,
                material_storage_buffer_write_descriptor_set
            };

            vkUpdateDescriptorSets(context->logical_device,
                                   ArrayCount(write_descriptor_sets),
                                   write_descriptor_sets, 0, nullptr);
        }
    }

    {
        VkDescriptorSetLayoutBinding texture_array_descriptor_set_layout_bindings[1] = {};
        texture_array_descriptor_set_layout_bindings[0].binding = 0;
        texture_array_descriptor_set_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texture_array_descriptor_set_layout_bindings[0].descriptorCount = MAX_MATERIAL_COUNT;
        texture_array_descriptor_set_layout_bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo texture_array_descriptor_set_layout_create_info =
            { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };

        texture_array_descriptor_set_layout_create_info.bindingCount = ArrayCount(texture_array_descriptor_set_layout_bindings);
        texture_array_descriptor_set_layout_create_info.pBindings = texture_array_descriptor_set_layout_bindings;
        texture_array_descriptor_set_layout_create_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

        VkDescriptorBindingFlags bindless_flags[1] =
        {
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT|VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT
        };

        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_layout_create_info =
         { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
         extended_layout_create_info.bindingCount = ArrayCount(texture_array_descriptor_set_layout_bindings);
         extended_layout_create_info.pBindingFlags = bindless_flags;

        texture_array_descriptor_set_layout_create_info.pNext = &extended_layout_create_info;
        CheckVkResult(vkCreateDescriptorSetLayout(context->logical_device,
                                                  &texture_array_descriptor_set_layout_create_info,
                                                  nullptr,
                                                  &context->texture_array_descriptor_set_layout));

        VkDescriptorSetLayout textures_array_descriptor_set_layouts[MAX_FRAMES_IN_FLIGHT] = {};

        for (U32 frame_index = 0;
             frame_index < MAX_FRAMES_IN_FLIGHT;
             frame_index++)
        {
            textures_array_descriptor_set_layouts[frame_index] = context->texture_array_descriptor_set_layout;
        }

        VkDescriptorSetAllocateInfo descriptor_set_allocation_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        descriptor_set_allocation_info.descriptorPool = context->descriptor_pool;
        descriptor_set_allocation_info.descriptorSetCount = U32(MAX_FRAMES_IN_FLIGHT);
        descriptor_set_allocation_info.pSetLayouts = textures_array_descriptor_set_layouts;

        CheckVkResult(vkAllocateDescriptorSets(context->logical_device,
                                               &descriptor_set_allocation_info,
                                               context->texture_array_descriptor_sets));
    }

    create_graphics_pipeline(context,
                             context->vertex_shader.handle,
                             context->fragment_shader.handle,
                             context->render_pass,
                             &context->mesh_pipeline);

    VkSemaphoreCreateInfo semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

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

void deinit_vulkan(Vulkan_Context *context)
{
    vkDeviceWaitIdle(context->logical_device);

    vkDestroyDescriptorSetLayout(context->logical_device, context->per_frame_descriptor_set_layout, nullptr);
    vkDestroyDescriptorSetLayout(context->logical_device, context->texture_array_descriptor_set_layout, nullptr);
    vkDestroyDescriptorPool(context->logical_device, context->descriptor_pool, nullptr);

    destroy_buffer(&context->transfer_buffer, context->logical_device);
    destroy_buffer(&context->vertex_buffer, context->logical_device);
    destroy_buffer(&context->index_buffer, context->logical_device);

    for (U32 frame_index = 0;
         frame_index < MAX_FRAMES_IN_FLIGHT;
         frame_index++)
    {
        destroy_buffer(&context->global_uniform_buffers[frame_index], context->logical_device);
        destroy_buffer(&context->object_storage_buffers[frame_index], context->logical_device);
        destroy_buffer(&context->material_storage_buffers[frame_index], context->logical_device);

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
    vkDestroyCommandPool(context->logical_device, context->transfer_command_pool, nullptr);

    destroy_swapchain(context, &context->swapchain);
    destroy_graphics_pipeline(context->logical_device, &context->mesh_pipeline);

    vkDestroyRenderPass(context->logical_device, context->render_pass, nullptr);

    destroy_shader(&context->vertex_shader, context->logical_device);
    destroy_shader(&context->fragment_shader, context->logical_device);

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
    renderer_state->texture_bundle_size = sizeof(Vulkan_Texture_Bundle);
    renderer_state->material_bundle_size = sizeof(Vulkan_Material_Bundle);
    renderer_state->static_mesh_bundle_size = sizeof(Vulkan_Static_Mesh_Bundle);
    return init_vulkan(&vulkan_context, engine, arena);
}

void vulkan_renderer_wait_for_gpu_to_finish_all_work(struct Renderer_State *renderer_state)
{
    vkDeviceWaitIdle(vulkan_context.logical_device);
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

void vulkan_renderer_begin_frame(struct Renderer_State *renderer_state, const Scene_Data *scene_data)
{
    Vulkan_Context *context = &vulkan_context;
    U32 current_frame_in_flight_index = context->current_frame_in_flight_index;

    vkWaitForFences(context->logical_device,
                    1, &context->frame_in_flight_fences[current_frame_in_flight_index],
                    VK_TRUE, UINT64_MAX);

    Vulkan_Global_Uniform_Buffer global_uniform_buffer_data = {};
    global_uniform_buffer_data.view = scene_data->view;
    global_uniform_buffer_data.projection = scene_data->projection;
    global_uniform_buffer_data.projection[1][1] *= -1;

    Vulkan_Buffer *global_uniform_buffer = &context->global_uniform_buffers[current_frame_in_flight_index];
    memcpy(global_uniform_buffer->data, &global_uniform_buffer_data, sizeof(Vulkan_Global_Uniform_Buffer));

    Vulkan_Buffer *material_storage_buffer = &context->material_storage_buffers[current_frame_in_flight_index];
    Vulkan_Material_Data *material_data_base = (Vulkan_Material_Data *)material_storage_buffer->data;

    for (U32 material_index = 0;
         material_index < renderer_state->material_count;
         material_index++)
    {
        Material *material = get_material(renderer_state, material_index);
        Vulkan_Material *vulkan_material = get_data(material);
        material_data_base[material_index] = vulkan_material->data;
    }

    context->object_data_base = (Vulkan_Object_Data *)context->object_storage_buffers[current_frame_in_flight_index].data;
    context->object_data_count = 0;

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

    VkResult result = vkAcquireNextImageKHR(context->logical_device,
                                            context->swapchain.handle,
                                            UINT64_MAX,
                                            context->image_available_semaphores[current_frame_in_flight_index],
                                            VK_NULL_HANDLE,
                                            &context->current_swapchain_image_index);

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

    VkClearValue clear_values[3] = {};
    clear_values[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
    clear_values[1].color = { 1.0f, 0.0f, 1.0f, 1.0f };
    clear_values[2].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo render_pass_begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    render_pass_begin_info.renderPass = context->render_pass;
    render_pass_begin_info.framebuffer = context->swapchain.frame_buffers[context->current_swapchain_image_index];
    render_pass_begin_info.renderArea.offset = { 0, 0 };
    render_pass_begin_info.renderArea.extent = { context->swapchain.width, context->swapchain.height };
    render_pass_begin_info.clearValueCount = ArrayCount(clear_values);
    render_pass_begin_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(command_buffer,
                         &render_pass_begin_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      context->mesh_pipeline.handle);

    VkDescriptorImageInfo descriptor_image_infos[MAX_TEXTURE_COUNT] = {};

    for (U32 texture_index = 0;
         texture_index < renderer_state->texture_count;
         texture_index++)
    {
        Texture *texture = get_texture(renderer_state, texture_index);
        Vulkan_Image *vulkan_image = get_data(texture);

        descriptor_image_infos[texture_index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptor_image_infos[texture_index].imageView = vulkan_image->view;
        descriptor_image_infos[texture_index].sampler = vulkan_image->sampler;
    }

    VkWriteDescriptorSet write_descriptor_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write_descriptor_set.dstSet = context->texture_array_descriptor_sets[current_frame_in_flight_index];
    write_descriptor_set.dstBinding = 0;
    write_descriptor_set.dstArrayElement = 0;
    write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_descriptor_set.descriptorCount = renderer_state->texture_count;
    write_descriptor_set.pImageInfo = descriptor_image_infos;

    vkUpdateDescriptorSets(context->logical_device, 1, &write_descriptor_set, 0, nullptr);

    VkDescriptorSet descriptor_sets[] =
    {
        context->per_frame_descriptor_sets[current_frame_in_flight_index],
        context->texture_array_descriptor_sets[current_frame_in_flight_index]
    };

    vkCmdBindDescriptorSets(command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            context->mesh_pipeline.layout,
                            0, ArrayCount(descriptor_sets),
                            descriptor_sets,
                            0, nullptr);

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

    VkBuffer vertex_buffers[] = { context->vertex_buffer.handle };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffer,
                           0, ArrayCount(vertex_buffers), vertex_buffers, offsets);

    vkCmdBindIndexBuffer(command_buffer,
                         context->index_buffer.handle, 0, VK_INDEX_TYPE_UINT16);
}

void vulkan_renderer_submit_static_mesh(struct Renderer_State *renderer_state,
                                        const struct Static_Mesh *static_mesh, const glm::mat4 transform)
{
    Vulkan_Context *context = &vulkan_context;
    Assert(context->object_data_count < MAX_OBJECT_DATA_COUNT);
    U32 object_data_index = context->object_data_count++;
    Vulkan_Object_Data *object_data = &context->object_data_base[object_data_index];
    object_data->model = transform;
    object_data->material_index = index_of(renderer_state, static_mesh->material);
    U32 current_frame_in_flight_index = context->current_frame_in_flight_index;
    VkCommandBuffer command_buffer = context->graphics_command_buffers[current_frame_in_flight_index];

    Vulkan_Static_Mesh *vulkan_static_mesh = get_data(static_mesh);

    U32 instance_count = 1;
    U32 start_instance = object_data_index;
    U32 first_index = vulkan_static_mesh->first_index;
    S32 first_vertex = vulkan_static_mesh->first_vertex;

    vkCmdDrawIndexed(command_buffer, static_mesh->index_count, instance_count,
                     first_index, first_vertex, start_instance);
}

void vulkan_renderer_end_frame(struct Renderer_State *renderer_state)
{
    Vulkan_Context *context = &vulkan_context;
    U32 current_frame_in_flight_index = context->current_frame_in_flight_index;
    VkCommandBuffer command_buffer = context->graphics_command_buffers[current_frame_in_flight_index];

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
    present_info.pImageIndices = &context->current_swapchain_image_index;

    VkResult result = vkQueuePresentKHR(context->present_queue, &present_info);
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

bool vulkan_renderer_create_texture(Texture *texture, U32 width, U32 height,
                                    void *data, TextureFormat format, bool mipmapping)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Image* image = get_data(texture);

    Assert(format == TextureFormat_RGBA); // todo(amer): only supporting RGBA for now.
    create_image(image, context, width, height,
                 VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT|
                 VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_IMAGE_ASPECT_COLOR_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mipmapping);

    copy_buffer_to_image(context, image, width, height, data,
                         (U64)width * (U64)height * sizeof(U32));

    texture->width = width;
    texture->height = height;
    return true;
}

void vulkan_renderer_destroy_texture(Texture *texture)
{
    Vulkan_Image *vulkan_image = get_data(texture);
    destroy_image(vulkan_image, &vulkan_context);
}

bool vulkan_renderer_create_material(Material *material, Texture *albedo, U32 albedo_texture_index)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Material *vulkan_material = get_data(material);
    vulkan_material->data.albedo_texture_index = albedo_texture_index;
    material->albedo = albedo;
    return true;
}

void vulkan_renderer_destroy_material(Material *material)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Material *vulkan_material = get_data(material);
}

bool vulkan_renderer_create_static_mesh(Static_Mesh *static_mesh, void *vertices,
                                        U16 vertex_count, U16 *indices, U32 index_count)
{
    Vulkan_Context *context = &vulkan_context;

    U64 vertex_size = vertex_count * sizeof(Vertex);
    U64 index_size = index_count * sizeof(U16);

    Assert(context->vertex_offset + vertex_size <= context->vertex_buffer.size);
    Assert(context->index_offset + index_size <= context->index_buffer.size);
    static_mesh->index_count = index_count;
    static_mesh->vertex_count = vertex_count;

    Vulkan_Static_Mesh *vulkan_static_mesh = get_data(static_mesh);

    copy_data_to_buffer(context, &context->vertex_buffer, context->vertex_offset, vertices, vertex_size);
    copy_data_to_buffer(context, &context->index_buffer, context->index_offset, indices, index_size);

    vulkan_static_mesh->first_vertex = (S32)u64_to_u32(context->vertex_offset / sizeof(Vertex));
    vulkan_static_mesh->first_index = u64_to_u32(context->index_offset / sizeof(U16));

    context->vertex_offset += vertex_size;
    context->index_offset += index_size;
    return true;
}

void vulkan_renderer_destroy_static_mesh(Static_Mesh *static_mesh)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Static_Mesh *vulkan_static_mesh = get_data(static_mesh);
}