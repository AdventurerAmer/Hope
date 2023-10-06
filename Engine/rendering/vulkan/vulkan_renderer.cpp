#include <string.h>

#include "vulkan_renderer.h"
#include "vulkan_image.h"
#include "vulkan_buffer.h"
#include "vulkan_swapchain.h"
#include "vulkan_shader.h"
#include "vulkan_utils.h"

#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"

#include "core/platform.h"
#include "core/debugging.h"
#include "core/memory.h"
#include "core/file_system.h"
#include "core/engine.h"
#include "core/cvars.h"

#include "containers/dynamic_array.h"

#include "ImGui/backends/imgui_impl_vulkan.cpp"

static Vulkan_Context vulkan_context;

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data)
{
    (void)message_severity;
    (void)message_type;
    (void)user_data;
    HE_LOG(Rendering, Trace, "%s\n", callback_data->pMessage);
    HE_ASSERT(message_severity != VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
    return VK_FALSE;
}

static bool is_physical_device_supports_all_features(VkPhysicalDevice physical_device, VkPhysicalDeviceFeatures2 features2, VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features)
{
    VkPhysicalDeviceDescriptorIndexingFeatures supported_descriptor_indexing_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };

    VkPhysicalDeviceFeatures2 supported_features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    supported_features2.pNext = &supported_descriptor_indexing_features;
    vkGetPhysicalDeviceFeatures2(physical_device, &supported_features2);

    if (features2.features.robustBufferAccess && !supported_features2.features.robustBufferAccess ||
        features2.features.fullDrawIndexUint32 && !supported_features2.features.fullDrawIndexUint32 ||
        features2.features.imageCubeArray && !supported_features2.features.imageCubeArray ||
        features2.features.independentBlend && !supported_features2.features.independentBlend ||
        features2.features.geometryShader && !supported_features2.features.geometryShader ||
        features2.features.tessellationShader && !supported_features2.features.tessellationShader ||
        features2.features.sampleRateShading && !supported_features2.features.sampleRateShading ||
        features2.features.dualSrcBlend && !supported_features2.features.dualSrcBlend ||
        features2.features.logicOp && !supported_features2.features.logicOp ||
        features2.features.multiDrawIndirect && !supported_features2.features.multiDrawIndirect ||
        features2.features.drawIndirectFirstInstance && !supported_features2.features.drawIndirectFirstInstance ||
        features2.features.depthClamp && !supported_features2.features.depthClamp ||
        features2.features.depthBiasClamp && !supported_features2.features.depthBiasClamp ||
        features2.features.fillModeNonSolid && !supported_features2.features.fillModeNonSolid ||
        features2.features.depthBounds && !supported_features2.features.depthBounds ||
        features2.features.wideLines && !supported_features2.features.wideLines ||
        features2.features.largePoints && !supported_features2.features.largePoints ||
        features2.features.alphaToOne && !supported_features2.features.alphaToOne ||
        features2.features.multiViewport && !supported_features2.features.multiViewport ||
        features2.features.samplerAnisotropy && !supported_features2.features.samplerAnisotropy ||
        features2.features.textureCompressionETC2 && !supported_features2.features.textureCompressionETC2 ||
        features2.features.textureCompressionASTC_LDR && !supported_features2.features.textureCompressionASTC_LDR ||
        features2.features.textureCompressionBC && !supported_features2.features.textureCompressionBC ||
        features2.features.occlusionQueryPrecise && !supported_features2.features.occlusionQueryPrecise ||
        features2.features.pipelineStatisticsQuery && !supported_features2.features.pipelineStatisticsQuery ||
        features2.features.vertexPipelineStoresAndAtomics && !supported_features2.features.vertexPipelineStoresAndAtomics ||
        features2.features.fragmentStoresAndAtomics && !supported_features2.features.fragmentStoresAndAtomics ||
        features2.features.shaderTessellationAndGeometryPointSize && !supported_features2.features.shaderTessellationAndGeometryPointSize ||
        features2.features.shaderImageGatherExtended && !supported_features2.features.shaderImageGatherExtended ||
        features2.features.shaderStorageImageExtendedFormats && !supported_features2.features.shaderStorageImageExtendedFormats ||
        features2.features.shaderStorageImageMultisample && !supported_features2.features.shaderStorageImageMultisample ||
        features2.features.shaderStorageImageReadWithoutFormat && !supported_features2.features.shaderStorageImageReadWithoutFormat ||
        features2.features.shaderStorageImageWriteWithoutFormat && !supported_features2.features.shaderStorageImageWriteWithoutFormat ||
        features2.features.shaderUniformBufferArrayDynamicIndexing && !supported_features2.features.shaderUniformBufferArrayDynamicIndexing ||
        features2.features.shaderSampledImageArrayDynamicIndexing && !supported_features2.features.shaderSampledImageArrayDynamicIndexing ||
        features2.features.shaderStorageBufferArrayDynamicIndexing && !supported_features2.features.shaderStorageBufferArrayDynamicIndexing ||
        features2.features.shaderStorageImageArrayDynamicIndexing && !supported_features2.features.shaderStorageImageArrayDynamicIndexing ||
        features2.features.shaderClipDistance && !supported_features2.features.shaderClipDistance ||
        features2.features.shaderCullDistance && !supported_features2.features.shaderCullDistance ||
        features2.features.shaderFloat64 && !supported_features2.features.shaderFloat64 ||
        features2.features.shaderInt64 && !supported_features2.features.shaderInt64 ||
        features2.features.shaderInt16 && !supported_features2.features.shaderInt16 ||
        features2.features.shaderResourceResidency && !supported_features2.features.shaderResourceResidency ||
        features2.features.shaderResourceMinLod && !supported_features2.features.shaderResourceMinLod ||
        features2.features.sparseBinding && !supported_features2.features.sparseBinding ||
        features2.features.sparseResidencyBuffer && !supported_features2.features.sparseResidencyBuffer ||
        features2.features.sparseResidencyImage2D && !supported_features2.features.sparseResidencyImage2D ||
        features2.features.sparseResidencyImage3D && !supported_features2.features.sparseResidencyImage3D ||
        features2.features.sparseResidency2Samples && !supported_features2.features.sparseResidency2Samples ||
        features2.features.sparseResidency4Samples && !supported_features2.features.sparseResidency4Samples ||
        features2.features.sparseResidency8Samples && !supported_features2.features.sparseResidency8Samples ||
        features2.features.sparseResidency16Samples && !supported_features2.features.sparseResidency16Samples ||
        features2.features.sparseResidencyAliased && !supported_features2.features.sparseResidencyAliased ||
        features2.features.variableMultisampleRate && !supported_features2.features.variableMultisampleRate ||
        features2.features.inheritedQueries && !supported_features2.features.inheritedQueries)
    {
        return false;
    }

    if (descriptor_indexing_features.shaderInputAttachmentArrayDynamicIndexing && !supported_descriptor_indexing_features.shaderInputAttachmentArrayDynamicIndexing ||
        descriptor_indexing_features.shaderUniformTexelBufferArrayDynamicIndexing && !supported_descriptor_indexing_features.shaderUniformTexelBufferArrayDynamicIndexing ||
        descriptor_indexing_features.shaderStorageTexelBufferArrayDynamicIndexing && !supported_descriptor_indexing_features.shaderStorageTexelBufferArrayDynamicIndexing ||
        descriptor_indexing_features.shaderUniformBufferArrayNonUniformIndexing && !supported_descriptor_indexing_features.shaderUniformBufferArrayNonUniformIndexing ||
        descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing && !supported_descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing ||
        descriptor_indexing_features.shaderStorageBufferArrayNonUniformIndexing && !supported_descriptor_indexing_features.shaderStorageBufferArrayNonUniformIndexing ||
        descriptor_indexing_features.shaderStorageImageArrayNonUniformIndexing && !supported_descriptor_indexing_features.shaderStorageImageArrayNonUniformIndexing ||
        descriptor_indexing_features.shaderInputAttachmentArrayNonUniformIndexing && !supported_descriptor_indexing_features.shaderInputAttachmentArrayNonUniformIndexing ||
        descriptor_indexing_features.shaderUniformTexelBufferArrayNonUniformIndexing && !supported_descriptor_indexing_features.shaderUniformTexelBufferArrayNonUniformIndexing ||
        descriptor_indexing_features.shaderStorageTexelBufferArrayNonUniformIndexing && !supported_descriptor_indexing_features.shaderStorageTexelBufferArrayNonUniformIndexing ||
        descriptor_indexing_features.descriptorBindingUniformBufferUpdateAfterBind && !supported_descriptor_indexing_features.descriptorBindingUniformBufferUpdateAfterBind ||
        descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind && !supported_descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind ||
        descriptor_indexing_features.descriptorBindingStorageImageUpdateAfterBind && !supported_descriptor_indexing_features.descriptorBindingStorageImageUpdateAfterBind ||
        descriptor_indexing_features.descriptorBindingStorageBufferUpdateAfterBind && !supported_descriptor_indexing_features.descriptorBindingStorageBufferUpdateAfterBind ||
        descriptor_indexing_features.descriptorBindingUniformTexelBufferUpdateAfterBind && !supported_descriptor_indexing_features.descriptorBindingUniformTexelBufferUpdateAfterBind ||
        descriptor_indexing_features.descriptorBindingStorageTexelBufferUpdateAfterBind && !supported_descriptor_indexing_features.descriptorBindingStorageTexelBufferUpdateAfterBind ||
        descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending && !supported_descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending ||
        descriptor_indexing_features.descriptorBindingPartiallyBound && !supported_descriptor_indexing_features.descriptorBindingPartiallyBound ||
        descriptor_indexing_features.descriptorBindingVariableDescriptorCount && !supported_descriptor_indexing_features.descriptorBindingVariableDescriptorCount ||
        descriptor_indexing_features.runtimeDescriptorArray && !supported_descriptor_indexing_features.runtimeDescriptorArray)
    {
        return false;
    }

    return true;
}

static VkPhysicalDevice pick_physical_device(VkInstance instance, VkSurfaceKHR surface, VkPhysicalDeviceFeatures2 features, VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features, Memory_Arena *arena)
{
    Temprary_Memory_Arena temprary_arena = {};
    begin_temprary_memory_arena(&temprary_arena, arena);

    HE_DEFER
    {
        end_temprary_memory_arena(&temprary_arena);
    };

    U32 physical_device_count = 0;
    HE_CHECK_VKRESULT(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));

    if (!physical_device_count)
    {
        // todo(amer): logging
        return VK_NULL_HANDLE;
    }

    VkPhysicalDevice *physical_devices = HE_ALLOCATE_ARRAY(&temprary_arena, VkPhysicalDevice, physical_device_count);

    HE_CHECK_VKRESULT(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices));

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    U32 best_physical_device_score_so_far = 0;

    for (U32 physical_device_index = 0;
         physical_device_index < physical_device_count;
         physical_device_index++)
    {
        VkPhysicalDevice *current_physical_device = &physical_devices[physical_device_index];

        if (!is_physical_device_supports_all_features(*current_physical_device, features, descriptor_indexing_features))
        {
            continue;
        }

        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(*current_physical_device, &properties);

        U32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(*current_physical_device, &queue_family_count, nullptr);

        bool can_physical_device_do_graphics = false;
        bool can_physical_device_present = false;

        VkQueueFamilyProperties *queue_families = HE_ALLOCATE_ARRAY(&temprary_arena, VkQueueFamilyProperties, queue_family_count);

        vkGetPhysicalDeviceQueueFamilyProperties(*current_physical_device, &queue_family_count, queue_families);

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
            vkGetPhysicalDeviceSurfaceSupportKHR(*current_physical_device, queue_family_index, surface, &present_support);

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

static bool init_vulkan(Vulkan_Context *context, Engine *engine, Renderer_State *renderer_state)
{
    context->allocator = &engine->memory.free_list_allocator;
    context->renderer_state = renderer_state;
    
    Memory_Arena *arena = &engine->memory.permanent_arena;
    context->arena = create_sub_arena(arena, HE_MEGA(32));

    context->buffers = HE_ALLOCATE_ARRAY(arena, Vulkan_Buffer, HE_MAX_BUFFER_COUNT);
    context->textures = HE_ALLOCATE_ARRAY(arena, Vulkan_Image, HE_MAX_TEXTURE_COUNT);
    context->samplers = HE_ALLOCATE_ARRAY(arena, Vulkan_Sampler, HE_MAX_SAMPLER_COUNT);
    context->shaders = HE_ALLOCATE_ARRAY(arena, Vulkan_Shader, HE_MAX_SHADER_COUNT);
    context->shader_groups = HE_ALLOCATE_ARRAY(arena, Vulkan_Shader_Group, HE_MAX_SHADER_GROUP_COUNT);
    context->pipeline_states = HE_ALLOCATE_ARRAY(arena, Vulkan_Pipeline_State, HE_MAX_PIPELINE_STATE_COUNT);
    context->bind_group_layouts = HE_ALLOCATE_ARRAY(arena, Vulkan_Bind_Group_Layout, HE_MAX_BIND_GROUP_LAYOUT_COUNT);
    context->bind_groups = HE_ALLOCATE_ARRAY(arena, Vulkan_Bind_Group, HE_MAX_BIND_GROUP_COUNT);
    context->render_passes = HE_ALLOCATE_ARRAY(arena, Vulkan_Render_Pass, HE_MAX_RENDER_PASS_COUNT);
    context->frame_buffers = HE_ALLOCATE_ARRAY(arena, Vulkan_Frame_Buffer, HE_MAX_FRAME_BUFFER_COUNT);
    context->static_meshes = HE_ALLOCATE_ARRAY(arena, Vulkan_Static_Mesh, HE_MAX_STATIC_MESH_COUNT);

    const char *required_instance_extensions[] =
    {
        "VK_KHR_surface",

#if HE_OS_WINDOWS
        "VK_KHR_win32_surface",
#endif

#if HE_GRAPHICS_DEBUGGING
        "VK_EXT_debug_utils",
#endif
    };

    U32 required_api_version = VK_API_VERSION_1_1;
    U32 driver_api_version = 0;

    // vkEnumerateInstanceVersion requires at least vulkan 1.1
    auto enumerate_instance_version = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");

    if (enumerate_instance_version)
    {
        enumerate_instance_version(&driver_api_version);
    }
    else
    {
        driver_api_version = VK_API_VERSION_1_0;
    }

    HE_ASSERT(required_api_version <= driver_api_version);

    VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app_info.pApplicationName = engine->app_name.data;
    app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName = engine->name.data;
    app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.apiVersion = required_api_version;

    VkInstanceCreateInfo instance_create_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instance_create_info.pApplicationInfo = &app_info;
    instance_create_info.enabledExtensionCount = HE_ARRAYCOUNT(required_instance_extensions);
    instance_create_info.ppEnabledExtensionNames = required_instance_extensions;

#if HE_GRAPHICS_DEBUGGING

    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };

    debug_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    debug_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;

    debug_messenger_create_info.pfnUserCallback = vulkan_debug_callback;
    debug_messenger_create_info.pUserData = nullptr;

    const char *layers[] =
    {
        "VK_LAYER_KHRONOS_validation",
    };

    instance_create_info.enabledLayerCount = HE_ARRAYCOUNT(layers);
    instance_create_info.ppEnabledLayerNames = layers;
    instance_create_info.pNext = &debug_messenger_create_info;

#endif

    HE_CHECK_VKRESULT(vkCreateInstance(&instance_create_info, nullptr, &context->instance));

#if HE_GRAPHICS_DEBUGGING

    auto vkCreateDebugUtilsMessengerExt = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance, "vkCreateDebugUtilsMessengerEXT");
    HE_ASSERT(vkCreateDebugUtilsMessengerExt);

    HE_CHECK_VKRESULT(vkCreateDebugUtilsMessengerExt(context->instance, &debug_messenger_create_info, nullptr, &context->debug_messenger));

#endif

    context->surface = (VkSurfaceKHR)platform_create_vulkan_surface(engine, context->instance);
    HE_ASSERT(context->surface);

    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
    descriptor_indexing_features.shaderInputAttachmentArrayDynamicIndexing = VK_TRUE;
    descriptor_indexing_features.shaderUniformTexelBufferArrayDynamicIndexing = VK_TRUE;
    descriptor_indexing_features.shaderStorageTexelBufferArrayDynamicIndexing = VK_TRUE;
    descriptor_indexing_features.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    descriptor_indexing_features.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    descriptor_indexing_features.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    descriptor_indexing_features.shaderInputAttachmentArrayNonUniformIndexing = VK_TRUE;
    descriptor_indexing_features.shaderUniformTexelBufferArrayNonUniformIndexing = VK_TRUE;
    descriptor_indexing_features.shaderStorageTexelBufferArrayNonUniformIndexing = VK_TRUE;
    descriptor_indexing_features.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    descriptor_indexing_features.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    descriptor_indexing_features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    descriptor_indexing_features.descriptorBindingUniformTexelBufferUpdateAfterBind = VK_TRUE;
    descriptor_indexing_features.descriptorBindingStorageTexelBufferUpdateAfterBind = VK_TRUE;
    descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    descriptor_indexing_features.descriptorBindingPartiallyBound = VK_TRUE;
    descriptor_indexing_features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    descriptor_indexing_features.runtimeDescriptorArray = VK_TRUE;

    VkPhysicalDeviceFeatures2 physical_device_features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    physical_device_features2.features.samplerAnisotropy = VK_TRUE;
    physical_device_features2.features.sampleRateShading = VK_TRUE;
    physical_device_features2.pNext = &descriptor_indexing_features;

    context->physical_device = pick_physical_device(context->instance, context->surface, physical_device_features2, descriptor_indexing_features, arena);
    HE_ASSERT(context->physical_device != VK_NULL_HANDLE);

    vkGetPhysicalDeviceMemoryProperties(context->physical_device, &context->physical_device_memory_properties);
    vkGetPhysicalDeviceProperties(context->physical_device, &context->physical_device_properties);

    VkSampleCountFlags counts = context->physical_device_properties.limits.framebufferColorSampleCounts&
                                context->physical_device_properties.limits.framebufferDepthSampleCounts;

    {
        Temprary_Memory_Arena temprary_arena = {};
        begin_temprary_memory_arena(&temprary_arena, arena);

        HE_DEFER
        {
            end_temprary_memory_arena(&temprary_arena);
        };

        context->graphics_queue_family_index = 0;
        context->present_queue_family_index = 0;

        U32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device, &queue_family_count, nullptr);

        VkQueueFamilyProperties *queue_families = HE_ALLOCATE_ARRAY(&temprary_arena, VkQueueFamilyProperties, queue_family_count);

        vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device, &queue_family_count, queue_families);

        bool found_a_queue_family_that_can_do_graphics_and_present = false;

        for (U32 queue_family_index = 0; queue_family_index < queue_family_count; queue_family_index++)
        {
            VkQueueFamilyProperties *queue_family = &queue_families[queue_family_index];

            bool can_queue_family_do_graphics = (queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT);

            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(context->physical_device, queue_family_index, context->surface, &present_support);

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
            for (U32 queue_family_index = 0; queue_family_index < queue_family_count; queue_family_index++)
            {
                VkQueueFamilyProperties *queue_family = &queue_families[queue_family_index];

                if (queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    context->graphics_queue_family_index = queue_family_index;
                }

                VkBool32 present_support = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(context->physical_device, queue_family_index, context->surface, &present_support);

                if (present_support == VK_TRUE)
                {
                    context->present_queue_family_index = queue_family_index;
                }
            }
        }

        context->transfer_queue_family_index = context->graphics_queue_family_index;

        for (U32 queue_family_index = 0; queue_family_index < queue_family_count; queue_family_index++)
        {
            VkQueueFamilyProperties *queue_family = &queue_families[queue_family_index];
            if ((queue_family->queueFlags & VK_QUEUE_TRANSFER_BIT) && !(queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                context->transfer_queue_family_index = queue_family_index;
                break;
            }
        }

        F32 queue_priority = 1.0f;
        VkDeviceQueueCreateInfo *queue_create_infos = HE_ALLOCATE_ARRAY(&temprary_arena, VkDeviceQueueCreateInfo, 3);

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

        const char *required_device_extensions[] =
        {
            "VK_KHR_swapchain",
            "VK_KHR_push_descriptor",
            "VK_EXT_descriptor_indexing",
        };

        U32 extension_property_count = 0;
        vkEnumerateDeviceExtensionProperties(context->physical_device, nullptr, &extension_property_count, nullptr);

        VkExtensionProperties *extension_properties = HE_ALLOCATE_ARRAY(&temprary_arena, VkExtensionProperties, extension_property_count);

        vkEnumerateDeviceExtensionProperties(context->physical_device, nullptr, &extension_property_count, extension_properties);

        bool not_all_required_device_extensions_are_supported = false;

        for (U32 extension_index = 0; extension_index < HE_ARRAYCOUNT(required_device_extensions); extension_index++)
        {
            String device_extension = HE_STRING(required_device_extensions[extension_index]);
            bool is_extension_supported = false;

            for (U32 extension_property_index = 0; extension_property_index < extension_property_count; extension_property_index++)
            {
                VkExtensionProperties *extension_property = &extension_properties[extension_property_index];
                if (device_extension == extension_property->extensionName)
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
        device_create_info.enabledExtensionCount = HE_ARRAYCOUNT(required_device_extensions);

        HE_CHECK_VKRESULT(vkCreateDevice(context->physical_device, &device_create_info, nullptr, &context->logical_device));

        vkGetDeviceQueue(context->logical_device, context->graphics_queue_family_index, 0, &context->graphics_queue);
        vkGetDeviceQueue(context->logical_device, context->present_queue_family_index, 0, &context->present_queue);
        vkGetDeviceQueue(context->logical_device, context->transfer_queue_family_index, 0, &context->transfer_queue);
    }

    VkFormat image_formats[] =
    {
        VK_FORMAT_B8G8R8A8_SRGB
    };

    VkFormat depth_stencil_formats[] =
    {
        VK_FORMAT_D32_SFLOAT_S8_UINT
    };

    init_swapchain_support(context, image_formats, HE_ARRAYCOUNT(image_formats), depth_stencil_formats, HE_ARRAYCOUNT(depth_stencil_formats), VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, arena, &context->swapchain_support);

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    U32 min_image_count = HE_MAX_FRAMES_IN_FLIGHT;
    U32 width = (U32)engine->window.width;
    U32 height = (U32)engine->window.height;

    bool swapchain_created = create_swapchain(context, width, height, min_image_count, present_mode, &context->swapchain);
    HE_ASSERT(swapchain_created);

    {
        Temprary_Memory_Arena temprary_arena = {};
        begin_temprary_memory_arena(&temprary_arena, arena);

        HE_DEFER
        {
            end_temprary_memory_arena(&temprary_arena);
        };

        U64 pipeline_cache_size = 0;
        U8 *pipeline_cache_data = nullptr;

        Read_Entire_File_Result result = read_entire_file(HE_PIPELINE_CACHE_FILENAME, &temprary_arena);
        if (result.success)
        {
            VkPipelineCacheHeaderVersionOne *pipeline_cache_header = (VkPipelineCacheHeaderVersionOne *)result.data;
            if (pipeline_cache_header->deviceID == context->physical_device_properties.deviceID && pipeline_cache_header->vendorID == context->physical_device_properties.vendorID)
            {
                pipeline_cache_data = result.data;
                pipeline_cache_size = result.size;
            }
        }

        VkPipelineCacheCreateInfo pipeline_cache_create_info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        pipeline_cache_create_info.initialDataSize = pipeline_cache_size;
        pipeline_cache_create_info.pInitialData = pipeline_cache_data;
        HE_CHECK_VKRESULT(vkCreatePipelineCache(context->logical_device, &pipeline_cache_create_info, nullptr, &context->pipeline_cache));
    }

    VkCommandPoolCreateInfo graphics_command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };

    graphics_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    graphics_command_pool_create_info.queueFamilyIndex = context->graphics_queue_family_index;

    HE_CHECK_VKRESULT(vkCreateCommandPool(context->logical_device, &graphics_command_pool_create_info, nullptr, &context->graphics_command_pool));

    VkCommandBufferAllocateInfo graphics_command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };

    graphics_command_buffer_allocate_info.commandPool = context->graphics_command_pool;
    graphics_command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    graphics_command_buffer_allocate_info.commandBufferCount = HE_MAX_FRAMES_IN_FLIGHT;
    HE_CHECK_VKRESULT(vkAllocateCommandBuffers(context->logical_device, &graphics_command_buffer_allocate_info, context->graphics_command_buffers));

    VkCommandPoolCreateInfo transfer_command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };

    transfer_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    transfer_command_pool_create_info.queueFamilyIndex = context->transfer_queue_family_index;
    HE_CHECK_VKRESULT(vkCreateCommandPool(context->logical_device, &transfer_command_pool_create_info, nullptr, &context->transfer_command_pool));

    VkDescriptorPoolSize descriptor_pool_sizes[] =
    {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT }
    };

    VkDescriptorPoolCreateInfo descriptor_pool_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descriptor_pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    descriptor_pool_create_info.poolSizeCount = HE_ARRAYCOUNT(descriptor_pool_sizes);
    descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes;
    descriptor_pool_create_info.maxSets = HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT * HE_ARRAYCOUNT(descriptor_pool_sizes);

    HE_CHECK_VKRESULT(vkCreateDescriptorPool(context->logical_device, &descriptor_pool_create_info, nullptr, &context->descriptor_pool));

    VkSemaphoreCreateInfo semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        HE_CHECK_VKRESULT(vkCreateSemaphore(context->logical_device, &semaphore_create_info, nullptr, &context->image_available_semaphores[frame_index]));
        HE_CHECK_VKRESULT(vkCreateSemaphore(context->logical_device, &semaphore_create_info, nullptr, &context->rendering_finished_semaphores[frame_index]));
        HE_CHECK_VKRESULT(vkCreateFence(context->logical_device, &fence_create_info, nullptr, &context->frame_in_flight_fences[frame_index]));
    }

    return true;
}

void deinit_vulkan(Vulkan_Context *context)
{
    vkDeviceWaitIdle(context->logical_device);

    vkDestroyDescriptorPool(context->logical_device, context->descriptor_pool, nullptr);
    vkDestroyDescriptorPool(context->logical_device, context->imgui_descriptor_pool, nullptr);

    ImGui_ImplVulkan_Shutdown();

    for (U32 frame_index = 0;
         frame_index < HE_MAX_FRAMES_IN_FLIGHT;
         frame_index++)
    {
        vkDestroySemaphore(context->logical_device, context->image_available_semaphores[frame_index], nullptr);
        vkDestroySemaphore(context->logical_device, context->rendering_finished_semaphores[frame_index], nullptr);
        vkDestroyFence(context->logical_device, context->frame_in_flight_fences[frame_index], nullptr);
    }

    vkDestroyCommandPool(context->logical_device, context->graphics_command_pool, nullptr);
    vkDestroyCommandPool(context->logical_device, context->transfer_command_pool, nullptr);

    destroy_swapchain(context, &context->swapchain);

    U64 pipeline_cache_size = 0;
    vkGetPipelineCacheData(context->logical_device, context->pipeline_cache, &pipeline_cache_size, nullptr);

    if (pipeline_cache_size)
    {
        U8 *pipeline_cache_data = HE_ALLOCATE_ARRAY(&context->arena, U8, pipeline_cache_size);
        vkGetPipelineCacheData(context->logical_device, context->pipeline_cache, &pipeline_cache_size, pipeline_cache_data);
        write_entire_file(HE_PIPELINE_CACHE_FILENAME, pipeline_cache_data, pipeline_cache_size);
    }

    vkDestroyPipelineCache(context->logical_device, context->pipeline_cache, nullptr);

    vkDestroySurfaceKHR(context->instance, context->surface, nullptr);
    vkDestroyDevice(context->logical_device, nullptr);

#if HE_GRAPHICS_DEBUGGING
    auto vkDestroyDebugUtilsMessengerExt = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance, "vkDestroyDebugUtilsMessengerEXT");
    HE_ASSERT(vkDestroyDebugUtilsMessengerExt);
    vkDestroyDebugUtilsMessengerExt(context->instance, context->debug_messenger, nullptr);
#endif

    vkDestroyInstance(context->instance, nullptr);
}

bool vulkan_renderer_init(Engine *engine, Renderer_State *renderer_state)
{
    return init_vulkan(&vulkan_context, engine, renderer_state);
}

void vulkan_renderer_wait_for_gpu_to_finish_all_work()
{
    vkDeviceWaitIdle(vulkan_context.logical_device);
}

void vulkan_renderer_deinit()
{
    deinit_vulkan(&vulkan_context);
}

void vulkan_renderer_on_resize(U32 width, U32 height)
{
    Vulkan_Context *context = &vulkan_context;
    if (width != 0 && height != 0)
    {
        recreate_swapchain(context, &context->swapchain, width, height, context->swapchain.present_mode);
    }
}

void vulkan_renderer_begin_frame(const Scene_Data *scene_data)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;
    U32 current_frame_in_flight_index = renderer_state->current_frame_in_flight_index;

    vkWaitForFences(context->logical_device, 1, &context->frame_in_flight_fences[current_frame_in_flight_index], VK_TRUE, UINT64_MAX);

    U32 width = renderer_state->back_buffer_width;
    U32 height = renderer_state->back_buffer_height;

    if ((width != context->swapchain.width || height != context->swapchain.height) && width != 0 && height != 0)
    {
        recreate_swapchain(context, &context->swapchain, width, height, context->swapchain.present_mode);
    }

    VkResult result = vkAcquireNextImageKHR(context->logical_device, context->swapchain.handle, UINT64_MAX, context->image_available_semaphores[current_frame_in_flight_index], VK_NULL_HANDLE,&context->current_swapchain_image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        if (width != 0 && height != 0)
        {
            recreate_swapchain(context, &context->swapchain, width, height, context->swapchain.present_mode);
        }
    }
    else
    {
        HE_ASSERT(result == VK_SUCCESS);
    }

    vkResetFences(context->logical_device, 1, &context->frame_in_flight_fences[current_frame_in_flight_index]);

    VkCommandBuffer command_buffer = context->graphics_command_buffers[current_frame_in_flight_index];
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    Globals globals = {};
    globals.view = scene_data->view;
    globals.projection = scene_data->projection;
    globals.projection[1][1] *= -1;
    globals.directional_light_direction = glm::vec4(scene_data->directional_light.direction, 0.0f);
    globals.directional_light_color = srgb_to_linear(scene_data->directional_light.color) * scene_data->directional_light.intensity;

    Buffer *global_uniform_buffer = get(&renderer_state->buffers, renderer_state->globals_uniform_buffers[current_frame_in_flight_index]);
    memcpy(global_uniform_buffer->data, &globals, sizeof(Globals));

    context->command_buffer = command_buffer;
}

void vulkan_renderer_set_viewport(U32 width, U32 height)
{
    Vulkan_Context *context = &vulkan_context;

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (F32)width;
    viewport.height = (F32)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(context->command_buffer, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = width;
    scissor.extent.height = height;
    vkCmdSetScissor(context->command_buffer, 0, 1, &scissor);
}

void vulkan_renderer_set_vertex_buffers(const Array_View< Buffer_Handle > &vertex_buffer_handles, const Array_View< U64 > &offsets)
{
    HE_ASSERT(vertex_buffer_handles.count == offsets.count);
    
    Vulkan_Context *context = &vulkan_context;

    U32 current_frame_in_flight_index = context->renderer_state->current_frame_in_flight_index;
    VkCommandBuffer command_buffer = context->graphics_command_buffers[current_frame_in_flight_index];

    VkBuffer *vulkan_vertex_buffers = HE_ALLOCATE_ARRAY(&context->renderer_state->frame_arena, VkBuffer, offsets.count);
    for (U32 vertex_buffer_index = 0; vertex_buffer_index < offsets.count; vertex_buffer_index++)
    {
        Buffer_Handle vertex_buffer_handle = vertex_buffer_handles[vertex_buffer_index];
        Vulkan_Buffer *vulkan_vertex_buffer = &context->buffers[vertex_buffer_handle.index];
        vulkan_vertex_buffers[vertex_buffer_index] = vulkan_vertex_buffer->handle;
    }

    vkCmdBindVertexBuffers(command_buffer, 0, offsets.count, vulkan_vertex_buffers, offsets.data);
}

void vulkan_renderer_set_index_buffer(Buffer_Handle index_buffer_handle, U64 offset)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Buffer *vulkan_index_buffer = &context->buffers[index_buffer_handle.index];
    vkCmdBindIndexBuffer(context->command_buffer, vulkan_index_buffer->handle, offset, VK_INDEX_TYPE_UINT16);
}

void vulkan_renderer_set_pipeline_state(Pipeline_State_Handle pipeline_state_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    Vulkan_Pipeline_State *vulkan_pipeline_state = &context->pipeline_states[pipeline_state_handle.index];
    vkCmdBindPipeline(context->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_pipeline_state->handle);
}

void vulkan_renderer_draw_static_mesh(Static_Mesh_Handle static_mesh_handle, U32 first_instance)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;
    Static_Mesh *static_mesh = get(&renderer_state->static_meshes, static_mesh_handle);
    Vulkan_Static_Mesh *vulkan_static_mesh = &context->static_meshes[static_mesh_handle.index];

    U32 instance_count = 1;
    U32 first_index = vulkan_static_mesh->first_index;
    S32 first_vertex = vulkan_static_mesh->first_vertex;

    vkCmdDrawIndexed(context->command_buffer, static_mesh->index_count, instance_count, first_index, first_vertex, first_instance);
}

void vulkan_renderer_end_frame()
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    Texture_Handle color_attachment_handle = Resource_Pool< Texture >::invalid_handle;

    if (renderer_state->sample_count != 1)
    {
        color_attachment_handle = renderer_state->resolve_color_attachments[renderer_state->current_frame_in_flight_index];
    }
    else
    {
        color_attachment_handle = renderer_state->color_attachments[renderer_state->current_frame_in_flight_index];
    }

    HE_ASSERT(is_valid_handle(&renderer_state->textures, color_attachment_handle));
    
    VkImage swapchain_image = context->swapchain.images[context->current_swapchain_image_index];

    VkImageCopy region = {};

    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.srcSubresource.mipLevel = 0;
    region.srcOffset = { 0, 0, 0 };

    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;
    region.dstSubresource.mipLevel = 0;
    region.dstOffset = { 0, 0, 0 };

    region.extent = { context->swapchain.width, context->swapchain.height, 1 };

    transtion_image_to_layout(context->command_buffer, swapchain_image, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    Vulkan_Image *color_attachment = nullptr;

#if HE_RENDER_GRAPH
    // todo(amer): make the final pass / final image to be copied to the swapchain image in the definition of the graph
    S32 index = find(&renderer_state->render_graph.resource_cache, HE_STRING_LITERAL("rt0"));
    HE_ASSERT(index != -1);
    Render_Graph_Resource_Handle resource_handle = renderer_state->render_graph.resource_cache.values[index];
    Texture_Handle rt0 = renderer_state->render_graph.resources[resource_handle].info.handles[renderer_state->current_frame_in_flight_index];
    color_attachment = &context->textures[rt0.index];
#else
    // todo(amer): not optimal...
    color_attachment = &context->textures[color_attachment_handle.index];
#endif
    
    transtion_image_to_layout(context->command_buffer, color_attachment->handle, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkCmdCopyImage(context->command_buffer, color_attachment->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    transtion_image_to_layout(context->command_buffer, swapchain_image, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    vkEndCommandBuffer(context->command_buffer);

    VkPipelineStageFlags wait_stage =  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

    submit_info.pWaitDstStageMask = &wait_stage;

    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &context->image_available_semaphores[renderer_state->current_frame_in_flight_index];

    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &context->rendering_finished_semaphores[renderer_state->current_frame_in_flight_index];

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &context->command_buffer;

    vkQueueSubmit(context->graphics_queue, 1, &submit_info, context->frame_in_flight_fences[renderer_state->current_frame_in_flight_index]);

    ImGuiIO &io = ImGui::GetIO();
    if (io.ConfigFlags&ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    VkPresentInfoKHR present_info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &context->rendering_finished_semaphores[renderer_state->current_frame_in_flight_index];

    present_info.swapchainCount = 1;
    present_info.pSwapchains = &context->swapchain.handle;
    present_info.pImageIndices = &context->current_swapchain_image_index;

    VkResult result = vkQueuePresentKHR(context->present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        if (renderer_state->back_buffer_width != 0 && renderer_state->back_buffer_height != 0)
        {
            recreate_swapchain(context, &context->swapchain, renderer_state->back_buffer_width, renderer_state->back_buffer_height, context->swapchain.present_mode);
        }
    }
    else
    {
        HE_ASSERT(result == VK_SUCCESS);
    }
}

static VkFormat get_texture_format(Texture_Format texture_format)
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

bool vulkan_renderer_create_texture(Texture_Handle texture_handle, const Texture_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;
    Texture *texture = get(&renderer_state->textures, texture_handle);
    Vulkan_Image *image = &context->textures[texture_handle.index];

    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_NONE;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM;

    if (is_color_format(descriptor.format))
    {
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        if (descriptor.is_attachment)
        {
            usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        else
        {
            usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
    }
    else
    {
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
        usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }

    VkFormat format = get_texture_format(descriptor.format);
    VkSampleCountFlagBits sample_count = get_sample_count(descriptor.sample_count);

    create_image(image, context, descriptor.width, descriptor.height, format, VK_IMAGE_TILING_OPTIMAL, usage, aspect, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 descriptor.mipmapping, sample_count, descriptor.alias);

    if (descriptor.data)
    {
        // todo(amer): only supporting RGBA for now.
        U64 size = (U64)descriptor.width * (U64)descriptor.height * sizeof(U32);
        U64 transfered_data_offset = (U8 *)descriptor.data - renderer_state->transfer_allocator.base;

        Vulkan_Buffer *transfer_buffer = &context->buffers[renderer_state->transfer_buffer.index];
        copy_data_to_image_from_buffer(context, image, descriptor.width, descriptor.height, transfer_buffer, transfered_data_offset, size);
    }

    texture->width = descriptor.width;
    texture->height = descriptor.height;
    texture->is_attachment = descriptor.is_attachment;
    texture->format = descriptor.format;
    texture->sample_count = descriptor.sample_count;
    texture->alias = descriptor.alias;
    texture->size = image->memory_requirements.size;
    texture->alignment = image->memory_requirements.alignment;
    return true;
}

void vulkan_renderer_destroy_texture(Texture_Handle texture_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Image *vulkan_image = &context->textures[texture_handle.index];
    destroy_image(vulkan_image, &vulkan_context);
}

static VkSamplerAddressMode get_address_mode(Address_Mode address_mode)
{
    switch (address_mode)
    {
        case Address_Mode::REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case Address_Mode::CLAMP: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        default:
        {
            HE_ASSERT(!"unsupported address mode");
        } break;
    }

    return VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
}

static VkFilter get_filter(Filter filter)
{
    switch (filter)
    {
        case Filter::NEAREST: return VK_FILTER_NEAREST;
        case Filter::LINEAR: return VK_FILTER_LINEAR;

        default:
        {
            HE_ASSERT(!"unsupported filter");
        } break;
    }

    return VK_FILTER_MAX_ENUM;
}

static VkSamplerMipmapMode get_mipmap_mode(Filter filter)
{
    switch (filter)
    {
        case Filter::NEAREST: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case Filter::LINEAR: return VK_SAMPLER_MIPMAP_MODE_LINEAR;

        default:
        {
            HE_ASSERT(!"unsupported filter");
        } break;
    }

    return VK_SAMPLER_MIPMAP_MODE_MAX_ENUM;
}

bool vulkan_renderer_create_sampler(Sampler_Handle sampler_handle, const Sampler_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Sampler *sampler = get(&context->renderer_state->samplers, sampler_handle);
    Vulkan_Sampler *vulkan_sampler = &context->samplers[sampler_handle.index];

    VkSamplerCreateInfo sampler_create_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler_create_info.minFilter = get_filter(descriptor.min_filter);
    sampler_create_info.magFilter = get_filter(descriptor.mag_filter);
    sampler_create_info.addressModeU = get_address_mode(descriptor.address_mode_u);
    sampler_create_info.addressModeV = get_address_mode(descriptor.address_mode_v);
    sampler_create_info.addressModeW = get_address_mode(descriptor.address_mode_w);
    sampler_create_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_create_info.unnormalizedCoordinates = VK_FALSE;
    sampler_create_info.compareEnable = VK_FALSE;
    sampler_create_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_create_info.mipmapMode = get_mipmap_mode(descriptor.mip_filter);
    sampler_create_info.mipLodBias = 0.0f;
    sampler_create_info.minLod = 0.0f;
    sampler_create_info.maxLod = 16.0f;

    if (descriptor.anisotropy)
    {
        HE_ASSERT(descriptor.anisotropy <= context->physical_device_properties.limits.maxSamplerAnisotropy);
        sampler_create_info.anisotropyEnable = VK_TRUE;
        sampler_create_info.maxAnisotropy = (F32)descriptor.anisotropy;
    }

    HE_CHECK_VKRESULT(vkCreateSampler(context->logical_device, &sampler_create_info, nullptr, &vulkan_sampler->handle));
    sampler->descriptor = descriptor;
    return true;
}

void vulkan_renderer_destroy_sampler(Sampler_Handle sampler_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Sampler *sampler = get(&context->renderer_state->samplers, sampler_handle);
    Vulkan_Sampler *vulkan_sampler = &context->samplers[sampler_handle.index];
    vkDestroySampler(context->logical_device, vulkan_sampler->handle, nullptr);
}

bool vulkan_renderer_create_shader(Shader_Handle shader_handle, const Shader_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    bool loaded = load_shader(shader_handle, descriptor.path, context);
    return loaded;
}

void vulkan_renderer_destroy_shader(Shader_Handle shader_handle)
{
    Vulkan_Context *context = &vulkan_context;
    destroy_shader(shader_handle, context);
}

static void combine_stage_flags_or_add_binding_if_not_found(Dynamic_Array< Binding > &set, const Binding &binding)
{
    U32 binding_count = set.count;
    for (U32 binding_index = 0; binding_index < binding_count; binding_index++)
    {
        if (set[binding_index].number == binding.number)
        {
            set[binding_index].stage_flags = set[binding_index].stage_flags|binding.stage_flags;
            return;
        }
    }
    append(&set, binding);
}

bool vulkan_renderer_create_shader_group(Shader_Group_Handle shader_group_handle, const Shader_Group_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    Temprary_Memory_Arena temprary_arena = {};
    begin_temprary_memory_arena(&temprary_arena, &context->arena);
    HE_DEFER { end_temprary_memory_arena(&temprary_arena); };

    Shader_Group *shader_group = get(&renderer_state->shader_groups, shader_group_handle);
    Vulkan_Shader_Group *vulkan_shader_group = &context->shader_groups[shader_group_handle.index];

    Dynamic_Array< Binding > sets[HE_MAX_DESCRIPTOR_SET_COUNT];

    for (U32 set_index = 0; set_index < HE_MAX_DESCRIPTOR_SET_COUNT; set_index++)
    {
        auto &set = sets[set_index];
        init(&set, context->allocator);
    }

    for (const Shader_Handle &shader_handle : descriptor.shaders)
    {
        Shader *shader = get(&renderer_state->shaders, shader_handle);

        for (U32 set_index = 0; set_index < HE_MAX_DESCRIPTOR_SET_COUNT; set_index++)
        {
            const Bind_Group_Layout_Descriptor &set = shader->sets[set_index];
            for (U32 binding_index = 0; binding_index < set.binding_count; binding_index++)
            {
                Binding &binding = set.bindings[binding_index];
                combine_stage_flags_or_add_binding_if_not_found(sets[set_index], binding);
            }
        }
    }

    U32 set_count = HE_MAX_DESCRIPTOR_SET_COUNT;

    for (U32 set_index = 0; set_index < HE_MAX_DESCRIPTOR_SET_COUNT; set_index++)
    {
        bool is_first_empty_set = sets[set_index].count == 0;
        if (is_first_empty_set)
        {
            set_count = set_index;
            break;
        }
    }

    for (U32 set_index = 0; set_index < set_count; set_index++)
    {
        const Dynamic_Array< Binding > &set = sets[set_index];

        Bind_Group_Layout_Descriptor bind_group_layout_descriptor = {};
        bind_group_layout_descriptor.binding_count = set.count;
        bind_group_layout_descriptor.bindings = set.data;

        shader_group->bind_group_layouts[set_index] = aquire_handle(&renderer_state->bind_group_layouts);
        vulkan_renderer_create_bind_group_layout(shader_group->bind_group_layouts[set_index], bind_group_layout_descriptor);
    }

    VkDescriptorSetLayout *descriptor_set_layouts = HE_ALLOCATE_ARRAY(&temprary_arena, VkDescriptorSetLayout, set_count);
    for (U32 set_index = 0; set_index < set_count; set_index++)
    {
        Vulkan_Bind_Group_Layout *bind_group_layout = &context->bind_group_layouts[ shader_group->bind_group_layouts[set_index].index ];
        descriptor_set_layouts[set_index] = bind_group_layout->handle;
    }

    VkPipelineLayoutCreateInfo pipeline_layout_create_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipeline_layout_create_info.setLayoutCount = set_count;
    pipeline_layout_create_info.pSetLayouts = descriptor_set_layouts;
    pipeline_layout_create_info.pushConstantRangeCount = 0;
    pipeline_layout_create_info.pPushConstantRanges = nullptr;

    HE_CHECK_VKRESULT(vkCreatePipelineLayout(context->logical_device, &pipeline_layout_create_info, nullptr, &vulkan_shader_group->pipeline_layout));

    copy(&shader_group->shaders, &descriptor.shaders);
    return true;
}

void vulkan_renderer_destroy_shader_group(Shader_Group_Handle shader_group_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Shader_Group *vulkan_shader_group = &context->shader_groups[shader_group_handle.index];
    vkDestroyPipelineLayout(context->logical_device, vulkan_shader_group->pipeline_layout, nullptr);
}

bool vulkan_renderer_create_pipeline_state(Pipeline_State_Handle pipeline_state_handle, const Pipeline_State_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    return create_graphics_pipeline(pipeline_state_handle, descriptor, context);
}

void vulkan_renderer_destroy_pipeline_state(Pipeline_State_Handle pipeline_state_handle)
{
    Vulkan_Context *context = &vulkan_context;
    destroy_pipeline(pipeline_state_handle, context);
}

static VkDescriptorType get_descriptor_type(Binding_Type type)
{
    switch (type)
    {
        case Binding_Type::UNIFORM_BUFFER: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case Binding_Type::STORAGE_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case Binding_Type::COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

        default:
        {
            HE_ASSERT(!"unsupported binding type");
        } break;
    }

    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

static VkDescriptorType get_descriptor_type(Buffer_Usage usage)
{
    switch (usage)
    {
        case Buffer_Usage::UNIFORM: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case Buffer_Usage::STORAGE: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

        default:
        {
            HE_ASSERT(!"unsupported binding type");
        } break;
    }

    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

bool vulkan_renderer_create_bind_group_layout(Bind_Group_Layout_Handle bind_group_layout_handle, const Bind_Group_Layout_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;

    Bind_Group_Layout *bind_group_layout = get(&context->renderer_state->bind_group_layouts, bind_group_layout_handle);
    Vulkan_Bind_Group_Layout *vulkan_bind_group_layout = &context->bind_group_layouts[bind_group_layout_handle.index];

    Temprary_Memory_Arena temprary_arena = {};
    begin_temprary_memory_arena(&temprary_arena, &context->arena);
    HE_DEFER { end_temprary_memory_arena(&temprary_arena); };

    VkDescriptorBindingFlags *layout_bindings_flags = HE_ALLOCATE_ARRAY(&temprary_arena, VkDescriptorBindingFlags, descriptor.binding_count);
    VkDescriptorSetLayoutBinding *bindings = HE_ALLOCATE_ARRAY(&temprary_arena, VkDescriptorSetLayoutBinding, descriptor.binding_count);

    for (U32 binding_index = 0; binding_index < descriptor.binding_count; binding_index++)
    {
        layout_bindings_flags[binding_index] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT|VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT;

        VkDescriptorSetLayoutBinding &binding = bindings[binding_index];
        binding.binding = descriptor.bindings[binding_index].number;
        binding.descriptorType = get_descriptor_type(descriptor.bindings[binding_index].type);
        binding.descriptorCount = descriptor.bindings[binding_index].count;
        binding.stageFlags = descriptor.bindings[binding_index].stage_flags;
        binding.pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_descriptor_set_layout_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
    extended_descriptor_set_layout_create_info.bindingCount = descriptor.binding_count;
    extended_descriptor_set_layout_create_info.pBindingFlags = layout_bindings_flags;

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };

    descriptor_set_layout_create_info.bindingCount = descriptor.binding_count;
    descriptor_set_layout_create_info.pBindings = bindings;
    descriptor_set_layout_create_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    descriptor_set_layout_create_info.pNext = &extended_descriptor_set_layout_create_info;

    HE_CHECK_VKRESULT(vkCreateDescriptorSetLayout(context->logical_device, &descriptor_set_layout_create_info, nullptr, &vulkan_bind_group_layout->handle));
    return true;
}

void vulkan_renderer_destroy_bind_group_layout(Bind_Group_Layout_Handle bind_group_layout_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Bind_Group_Layout *vulkan_bind_group_layout = &context->bind_group_layouts[bind_group_layout_handle.index];
    vkDestroyDescriptorSetLayout(context->logical_device, vulkan_bind_group_layout->handle, nullptr);
}

bool vulkan_renderer_create_bind_group(Bind_Group_Handle bind_group_handle, const Bind_Group_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Bind_Group *bind_group = get(&context->renderer_state->bind_groups, bind_group_handle);
    Vulkan_Bind_Group *vulkan_bind_group = &context->bind_groups[bind_group_handle.index];
    Vulkan_Bind_Group_Layout *vulkan_bind_group_layout = &context->bind_group_layouts[descriptor.layout.index];

    VkDescriptorSetAllocateInfo descriptor_set_allocation_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    descriptor_set_allocation_info.descriptorPool = context->descriptor_pool;
    descriptor_set_allocation_info.descriptorSetCount = 1;
    descriptor_set_allocation_info.pSetLayouts = &vulkan_bind_group_layout->handle;

    HE_CHECK_VKRESULT(vkAllocateDescriptorSets(context->logical_device, &descriptor_set_allocation_info, &vulkan_bind_group->handle));
    bind_group->descriptor = descriptor;
    return true;
}

void vulkan_renderer_update_bind_group(Bind_Group_Handle bind_group_handle, const Array_View< Update_Binding_Descriptor > &update_binding_descriptors)
{
    Vulkan_Context *context = &vulkan_context;

    Temprary_Memory_Arena temprary_arena = {};
    begin_temprary_memory_arena(&temprary_arena, &context->arena);
    HE_DEFER { end_temprary_memory_arena(&temprary_arena); };

    Vulkan_Bind_Group *bind_group = &context->bind_groups[bind_group_handle.index];

    VkWriteDescriptorSet *write_descriptor_sets = HE_ALLOCATE_ARRAY(&temprary_arena, VkWriteDescriptorSet, update_binding_descriptors.count);

    for (U32 binding_index = 0; binding_index < update_binding_descriptors.count; binding_index++)
    {
        const Update_Binding_Descriptor *binding = &update_binding_descriptors[binding_index];

        VkWriteDescriptorSet *write_descriptor_set = &write_descriptor_sets[binding_index];
        *write_descriptor_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

        write_descriptor_set->dstSet = bind_group->handle;
        write_descriptor_set->dstBinding = binding->binding_number;
        write_descriptor_set->dstArrayElement = binding->element_index;
        write_descriptor_set->descriptorCount = binding->count;

        if (binding->buffers)
        {
            {
                Buffer *buffer = get(&context->renderer_state->buffers, binding->buffers[0]);
                write_descriptor_set->descriptorType = get_descriptor_type(buffer->usage);
            }

            VkDescriptorBufferInfo *buffer_infos = HE_ALLOCATE_ARRAY(&temprary_arena, VkDescriptorBufferInfo, binding->count);

            for (U32 buffer_index = 0; buffer_index < binding->count; buffer_index++)
            {
                Buffer *buffer = get(&context->renderer_state->buffers, binding->buffers[0]);
                Vulkan_Buffer *vulkan_buffer = &context->buffers[ binding->buffers[buffer_index].index ];

                VkDescriptorBufferInfo *buffer_info = &buffer_infos[buffer_index];
                buffer_info->buffer = vulkan_buffer->handle;
                buffer_info->offset = 0;
                buffer_info->range = buffer->size;
            }

            write_descriptor_set->pBufferInfo = buffer_infos;
        }

        if (binding->textures && binding->samplers)
        {
            write_descriptor_set->descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            VkDescriptorImageInfo *image_infos = HE_ALLOCATE_ARRAY(&temprary_arena, VkDescriptorImageInfo, binding->count);

            for (U32 image_index = 0; image_index < binding->count; image_index++)
            {
                Vulkan_Image *vulkan_image = &context->textures[ binding->textures[image_index].index ];
                Vulkan_Sampler *vulkan_sampler = &context->samplers[ binding->samplers[image_index].index ];
                VkDescriptorImageInfo *image_info = &image_infos[image_index];
                image_info->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                image_info->imageView = vulkan_image->view;
                image_info->sampler = vulkan_sampler->handle;
            }

            write_descriptor_set->pImageInfo = image_infos;
        }
    }

    vkUpdateDescriptorSets(context->logical_device, update_binding_descriptors.count, write_descriptor_sets, 0, nullptr);
}

void vulkan_renderer_set_bind_groups(U32 first_bind_group, const Array_View< Bind_Group_Handle > &bind_group_handles)
{
    Vulkan_Context *context = &vulkan_context;

    Bind_Group *bind_group = get(&context->renderer_state->bind_groups, bind_group_handles[0]);

    Vulkan_Shader_Group *vulkan_shader_group = &context->shader_groups[ bind_group->descriptor.shader_group.index ];

    VkDescriptorSet *descriptor_sets = HE_ALLOCATE_ARRAY(&context->renderer_state->frame_arena, VkDescriptorSet, bind_group_handles.count);
    for (U32 bind_group_index = 0; bind_group_index < bind_group_handles.count; bind_group_index++)
    {
        Vulkan_Bind_Group *vulkan_bind_group = &context->bind_groups[ bind_group_handles[ bind_group_index ].index ];
        descriptor_sets[bind_group_index] = vulkan_bind_group->handle;
    }

    vkCmdBindDescriptorSets(context->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_shader_group->pipeline_layout, first_bind_group, bind_group_handles.count, descriptor_sets, 0, nullptr);
}

void vulkan_renderer_destroy_bind_group(Bind_Group_Handle bind_group_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Bind_Group *vulkan_bind_group = &context->bind_groups[bind_group_handle.index];
    vkFreeDescriptorSets(context->logical_device, context->descriptor_pool, 1, &vulkan_bind_group->handle);
}

bool vulkan_renderer_create_render_pass(Render_Pass_Handle render_pass_handle, const Render_Pass_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;

    Temprary_Memory_Arena temprary_arena = {};
    begin_temprary_memory_arena(&temprary_arena, &context->arena);
    HE_DEFER { end_temprary_memory_arena(&temprary_arena); };

    Render_Pass *render_pass = get(&context->renderer_state->render_passes, render_pass_handle);
    Vulkan_Render_Pass *vulkan_render_pass = &context->render_passes[render_pass_handle.index];

    if (render_pass->color_attachments.count)
    {
        copy(&render_pass->color_attachments, &descriptor.color_attachments);
    }
    else
    {
        reset(&render_pass->color_attachments);
    }

    if (descriptor.resolve_attachments.count)
    {
        copy(&render_pass->resolve_attachments, &descriptor.resolve_attachments);
    }
    else
    {
        reset(&render_pass->resolve_attachments);
    }

    if (descriptor.depth_stencil_attachments.count)
    {
        copy(&render_pass->depth_stencil_attachments, &descriptor.depth_stencil_attachments);
    }
    else
    {
        reset(&render_pass->depth_stencil_attachments);
    }

    U32 attachment_count = descriptor.color_attachments.count + descriptor.resolve_attachments.count + descriptor.depth_stencil_attachments.count;
    VkAttachmentDescription *attachments = HE_ALLOCATE_ARRAY(&temprary_arena, VkAttachmentDescription, attachment_count);
    VkAttachmentReference *attachment_refs = HE_ALLOCATE_ARRAY(&temprary_arena, VkAttachmentReference, attachment_count);
    U32 attachment_index = 0;

    for (const Attachment_Info &attachment_info : descriptor.color_attachments)
    {
        VkAttachmentDescription *attachment = &attachments[attachment_index];
        attachment->format = get_texture_format(attachment_info.format);
        attachment->samples = get_sample_count(attachment_info.sample_count);
        attachment->storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        switch (attachment_info.operation)
        {
            case Attachment_Operation::DONT_CARE:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            } break;

            case Attachment_Operation::LOAD:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                attachment->initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            } break;

            case Attachment_Operation::CLEAR:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachment->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            } break;
        }

        attachment->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment->finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference *attachment_ref = &attachment_refs[attachment_index];
        attachment_ref->attachment = attachment_index;
        attachment_ref->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachment_index++;
    }

    for (const Attachment_Info &attachment_info : descriptor.resolve_attachments)
    {
        VkAttachmentDescription *attachment = &attachments[attachment_index];
        attachment->format = get_texture_format(attachment_info.format);
        attachment->samples = get_sample_count(attachment_info.sample_count);
        attachment->storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        switch (attachment_info.operation)
        {
            case Attachment_Operation::DONT_CARE:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            } break;

            case Attachment_Operation::LOAD:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                attachment->initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            } break;

            case Attachment_Operation::CLEAR:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachment->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            } break;
        }

        attachment->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment->finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference *attachment_ref = &attachment_refs[attachment_index];
        attachment_ref->attachment = attachment_index;
        attachment_ref->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachment_index++;
    }

    for (const Attachment_Info &attachment_info : descriptor.depth_stencil_attachments)
    {
        VkAttachmentDescription *attachment = &attachments[attachment_index];
        attachment->format = get_texture_format(attachment_info.format);
        attachment->samples = get_sample_count(attachment_info.sample_count);
        attachment->storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        switch (attachment_info.operation)
        {
            case Attachment_Operation::DONT_CARE:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            } break;

            case Attachment_Operation::LOAD:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                attachment->initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            } break;

            case Attachment_Operation::CLEAR:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachment->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            } break;
        }

        switch (descriptor.stencil_operation)
        {
            case Attachment_Operation::DONT_CARE:
            {
                attachment->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            } break;

            case Attachment_Operation::LOAD:
            {
                attachment->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            } break;

            case Attachment_Operation::CLEAR:
            {
                attachment->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            } break;
        }

        attachment->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment->finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference *attachment_ref = &attachment_refs[attachment_index];
        attachment_ref->attachment = attachment_index;
        attachment_ref->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attachment_index++;
    }

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    if (descriptor.color_attachments.count)
    {
        subpass.colorAttachmentCount = descriptor.color_attachments.count;
        subpass.pColorAttachments = attachment_refs;
    }

    if (descriptor.resolve_attachments.count)
    {
        subpass.pResolveAttachments = &attachment_refs[descriptor.color_attachments.count];
    }

    if (descriptor.depth_stencil_attachments.count)
    {
        subpass.pDepthStencilAttachment = &attachment_refs[descriptor.color_attachments.count + descriptor.resolve_attachments.count];
    }

    VkRenderPassCreateInfo render_pass_create_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    render_pass_create_info.attachmentCount = attachment_count;
    render_pass_create_info.pAttachments = attachments;

    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;

    HE_CHECK_VKRESULT(vkCreateRenderPass(context->logical_device, &render_pass_create_info, nullptr, &vulkan_render_pass->handle));
    return true;
}

void vulkan_renderer_begin_render_pass(Render_Pass_Handle render_pass_handle, Frame_Buffer_Handle frame_buffer_handle, const Array_View< Clear_Value > &clear_values)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    Frame_Buffer *frame_buffer = get(&renderer_state->frame_buffers, frame_buffer_handle);
    Vulkan_Render_Pass *vulkan_render_pass = &context->render_passes[render_pass_handle.index];
    Vulkan_Frame_Buffer *vulkan_frame_buffer = &context->frame_buffers[frame_buffer_handle.index];
    HE_ASSERT(frame_buffer->attachments.count == clear_values.count);

    Texture *attachment = get(&renderer_state->textures, frame_buffer->attachments[0]);
    
    VkClearValue *vulkan_clear_values = HE_ALLOCATE_ARRAY(&renderer_state->frame_arena, VkClearValue, clear_values.count);

    for (U32 clear_value_index = 0; clear_value_index < clear_values.count; clear_value_index++)
    {
        Texture_Handle texture_handle = frame_buffer->attachments[clear_value_index];
        Texture *texture = get(&renderer_state->textures, texture_handle);

        if (is_color_format(texture->format))
        {
            vulkan_clear_values[clear_value_index].color =
            {
                clear_values[clear_value_index].color.r,
                clear_values[clear_value_index].color.g,
                clear_values[clear_value_index].color.b,
                clear_values[clear_value_index].color.a,
            };
        }
        else
        {
            vulkan_clear_values[clear_value_index].depthStencil =
            {
                clear_values[clear_value_index].depth,
                clear_values[clear_value_index].stencil,
            };
        }
    }

    VkRenderPassBeginInfo render_pass_begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    render_pass_begin_info.renderPass = vulkan_render_pass->handle;
    render_pass_begin_info.framebuffer = vulkan_frame_buffer->handle;
    render_pass_begin_info.renderArea.offset = { 0, 0 };
    render_pass_begin_info.renderArea.extent = { context->swapchain.width, context->swapchain.height };
    render_pass_begin_info.clearValueCount = clear_values.count;
    render_pass_begin_info.pClearValues = vulkan_clear_values;

    vkCmdBeginRenderPass(context->command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

void vulkan_renderer_end_render_pass(Render_Pass_Handle render_pass_handle)
{
    Vulkan_Context *context = &vulkan_context;
    vkCmdEndRenderPass(context->command_buffer);
}

void vulkan_renderer_destroy_render_pass(Render_Pass_Handle render_pass_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Render_Pass *vulkan_render_pass = &context->render_passes[render_pass_handle.index];
    vkDestroyRenderPass(context->logical_device, vulkan_render_pass->handle, nullptr);
}

bool vulkan_renderer_create_frame_buffer(Frame_Buffer_Handle frame_buffer_handle, const Frame_Buffer_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Frame_Buffer *frame_buffer = get(&context->renderer_state->frame_buffers, frame_buffer_handle);
    copy(&frame_buffer->attachments, &descriptor.attachments);
    Vulkan_Frame_Buffer *vulkan_frame_buffer = &context->frame_buffers[frame_buffer_handle.index];

    Temprary_Memory_Arena temprary_arena = {};
    begin_temprary_memory_arena(&temprary_arena, &context->arena);
    HE_DEFER { end_temprary_memory_arena(&temprary_arena); };

    Vulkan_Render_Pass *vulkan_render_pass = &context->render_passes[ descriptor.render_pass.index ];
    VkImageView *vulkan_attachments = HE_ALLOCATE_ARRAY(&temprary_arena, VkImageView, descriptor.attachments.count);

    U32 attachment_index = 0;
    for (Texture_Handle texture_handle : descriptor.attachments)
    {
        Vulkan_Image *vulkan_texture = &context->textures[ texture_handle.index ];
        vulkan_attachments[attachment_index] = vulkan_texture->view;
        attachment_index++;
    }

    VkFramebufferCreateInfo frame_buffer_create_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    frame_buffer_create_info.renderPass = vulkan_render_pass->handle;
    frame_buffer_create_info.attachmentCount = descriptor.attachments.count;
    frame_buffer_create_info.pAttachments = vulkan_attachments;
    frame_buffer_create_info.width = descriptor.width;
    frame_buffer_create_info.height = descriptor.height;
    frame_buffer_create_info.layers = 1;

    HE_CHECK_VKRESULT(vkCreateFramebuffer(context->logical_device, &frame_buffer_create_info, nullptr, &vulkan_frame_buffer->handle));
    frame_buffer->width = descriptor.width;
    frame_buffer->height = descriptor.height;
    return true;
}

void vulkan_renderer_destroy_frame_buffer(Frame_Buffer_Handle frame_buffer_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Frame_Buffer *frame_buffer = get(&context->renderer_state->frame_buffers, frame_buffer_handle);
    Vulkan_Frame_Buffer *vulkan_frame_buffer = &context->frame_buffers[frame_buffer_handle.index];
    vkDestroyFramebuffer(context->logical_device, vulkan_frame_buffer->handle, nullptr);
}

static VkBufferUsageFlags get_buffer_usage(Buffer_Usage usage)
{
    switch (usage)
    {
        case Buffer_Usage::TRANSFER: return 0;
        case Buffer_Usage::VERTEX:   return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        case Buffer_Usage::INDEX:    return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        case Buffer_Usage::UNIFORM:  return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        case Buffer_Usage::STORAGE:  return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        default:
        {
            HE_ASSERT(!"unsupported buffer usage");
        } break;
    }

    return VK_BUFFER_USAGE_FLAG_BITS_MAX_ENUM;
}

bool vulkan_renderer_create_buffer(Buffer_Handle buffer_handle, const Buffer_Descriptor &descriptor)
{
    HE_ASSERT(descriptor.size);
    Vulkan_Context *context = &vulkan_context;

    Buffer *buffer = get(&context->renderer_state->buffers, buffer_handle);
    Vulkan_Buffer *vulkan_buffer = &context->buffers[buffer_handle.index];

    VkBufferUsageFlags usage = get_buffer_usage(descriptor.usage);
    VkMemoryPropertyFlags memory_property_flags = 0;

    if (descriptor.is_device_local)
    {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        memory_property_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    else
    {
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        memory_property_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    VkBufferCreateInfo buffer_create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_create_info.size = descriptor.size;
    buffer_create_info.usage = usage;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_create_info.flags = 0;

    HE_CHECK_VKRESULT(vkCreateBuffer(context->logical_device, &buffer_create_info, nullptr, &vulkan_buffer->handle));

    VkMemoryRequirements memory_requirements = {};
    vkGetBufferMemoryRequirements(context->logical_device, vulkan_buffer->handle, &memory_requirements);

    S32 memory_type_index = find_memory_type_index(memory_requirements, memory_property_flags, context);
    HE_ASSERT(memory_type_index != -1);

    VkMemoryAllocateInfo memory_allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memory_allocate_info.allocationSize = memory_requirements.size;
    memory_allocate_info.memoryTypeIndex = memory_type_index;

    HE_CHECK_VKRESULT(vkAllocateMemory(context->logical_device, &memory_allocate_info, nullptr, &vulkan_buffer->memory));
    HE_CHECK_VKRESULT(vkBindBufferMemory(context->logical_device, vulkan_buffer->handle, vulkan_buffer->memory, 0));

    if ((memory_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        vkMapMemory(context->logical_device, vulkan_buffer->memory, 0, descriptor.size, 0, &buffer->data);
    }

    buffer->usage = descriptor.usage;
    buffer->size = descriptor.size;
    return true;
}

void vulkan_renderer_destroy_buffer(Buffer_Handle buffer_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Buffer *vulkan_buffer = &context->buffers[buffer_handle.index];

    vkFreeMemory(context->logical_device, vulkan_buffer->memory, nullptr);
    vkDestroyBuffer(context->logical_device, vulkan_buffer->handle, nullptr);
}

bool vulkan_renderer_create_static_mesh(Static_Mesh_Handle static_mesh_handle, const Static_Mesh_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;
    Static_Mesh *static_mesh = get(&renderer_state->static_meshes, static_mesh_handle);

    U64 position_size = descriptor.vertex_count * sizeof(glm::vec3);
    U64 normal_size = descriptor.vertex_count * sizeof(glm::vec3);
    U64 uv_size = descriptor.vertex_count * sizeof(glm::vec2);
    U64 tangent_size = descriptor.vertex_count * sizeof(glm::vec4);
    U64 index_size = descriptor.index_count * sizeof(U16);

    HE_ASSERT(renderer_state->vertex_count + descriptor.vertex_count <= renderer_state->max_vertex_count);
    static_mesh->index_count = descriptor.index_count;
    static_mesh->vertex_count = descriptor.vertex_count;

    Vulkan_Static_Mesh *vulkan_static_mesh = &context->static_meshes[static_mesh_handle.index];

    U64 position_offset = (U8 *)descriptor.positions - renderer_state->transfer_allocator.base;
    U64 normal_offset = (U8 *)descriptor.normals - renderer_state->transfer_allocator.base;
    U64 uv_offset = (U8 *)descriptor.uvs - renderer_state->transfer_allocator.base;
    U64 tangent_offset = (U8 *)descriptor.tangents - renderer_state->transfer_allocator.base;
    U64 indicies_offset = (U8 *)descriptor.indices - renderer_state->transfer_allocator.base;

    VkCommandBufferAllocateInfo command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    command_buffer_allocate_info.commandPool = context->transfer_command_pool;
    command_buffer_allocate_info.commandBufferCount = 1;
    command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VkCommandBuffer command_buffer = {};
    vkAllocateCommandBuffers(context->logical_device, &command_buffer_allocate_info, &command_buffer); // @Leak
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    Vulkan_Buffer *transfer_buffer = &context->buffers[renderer_state->transfer_buffer.index];
    Vulkan_Buffer *position_buffer = &context->buffers[renderer_state->position_buffer.index];
    Vulkan_Buffer *normal_buffer = &context->buffers[renderer_state->normal_buffer.index];
    Vulkan_Buffer *uv_buffer = &context->buffers[renderer_state->uv_buffer.index];
    Vulkan_Buffer *tangent_buffer = &context->buffers[renderer_state->tangent_buffer.index];

    VkBufferCopy position_copy_region = {};
    position_copy_region.srcOffset = position_offset;
    position_copy_region.dstOffset = renderer_state->vertex_count * sizeof(glm::vec3);
    position_copy_region.size = position_size;
    vkCmdCopyBuffer(command_buffer, transfer_buffer->handle, position_buffer->handle, 1, &position_copy_region);

    VkBufferCopy normal_copy_region = {};
    normal_copy_region.srcOffset = normal_offset;
    normal_copy_region.dstOffset = renderer_state->vertex_count * sizeof(glm::vec3);
    normal_copy_region.size = normal_size;
    vkCmdCopyBuffer(command_buffer, transfer_buffer->handle, normal_buffer->handle, 1, &normal_copy_region);

    VkBufferCopy uv_copy_region = {};
    uv_copy_region.srcOffset = uv_offset;
    uv_copy_region.dstOffset = renderer_state->vertex_count * sizeof(glm::vec2);
    uv_copy_region.size = uv_size;
    vkCmdCopyBuffer(command_buffer, transfer_buffer->handle, uv_buffer->handle, 1, &uv_copy_region);

    VkBufferCopy tangent_copy_region = {};
    tangent_copy_region.srcOffset = tangent_offset;
    tangent_copy_region.dstOffset = renderer_state->vertex_count * sizeof(glm::vec4);
    tangent_copy_region.size = tangent_size;
    vkCmdCopyBuffer(command_buffer, transfer_buffer->handle, tangent_buffer->handle, 1, &tangent_copy_region);

    Vulkan_Buffer *index_buffer = &context->buffers[renderer_state->index_buffer.index];

    VkBufferCopy index_copy_region = {};
    index_copy_region.srcOffset = indicies_offset;
    index_copy_region.dstOffset = renderer_state->index_offset;
    index_copy_region.size = index_size;
    vkCmdCopyBuffer(command_buffer, transfer_buffer->handle, index_buffer->handle, 1, &index_copy_region);

    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    vkQueueSubmit(context->transfer_queue, 1, &submit_info, VK_NULL_HANDLE);

    vulkan_static_mesh->first_vertex = (S32)u64_to_u32(renderer_state->vertex_count);
    vulkan_static_mesh->first_index = u64_to_u32(renderer_state->index_offset / sizeof(U16));

    renderer_state->vertex_count += descriptor.vertex_count;
    renderer_state->index_offset += index_size;
    return true;
}

// todo(amer): static mesh allocator...
void vulkan_renderer_destroy_static_mesh(Static_Mesh_Handle static_mesh_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Static_Mesh *vulkan_static_mesh = &context->static_meshes[static_mesh_handle.index];
}

bool vulkan_renderer_init_imgui()
{
    Vulkan_Context *context = &vulkan_context;

    constexpr U32 IMGUI_MAX_DESCRIPTOR_COUNT = 1024;

    VkDescriptorPoolSize pool_sizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_MAX_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_MAX_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_MAX_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, IMGUI_MAX_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, IMGUI_MAX_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, IMGUI_MAX_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, IMGUI_MAX_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, IMGUI_MAX_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, IMGUI_MAX_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, IMGUI_MAX_DESCRIPTOR_COUNT },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, IMGUI_MAX_DESCRIPTOR_COUNT }
    };

    VkDescriptorPoolCreateInfo descriptor_pool_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descriptor_pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptor_pool_create_info.maxSets = IMGUI_MAX_DESCRIPTOR_COUNT * HE_ARRAYCOUNT(pool_sizes);
    descriptor_pool_create_info.poolSizeCount = HE_ARRAYCOUNT(pool_sizes);
    descriptor_pool_create_info.pPoolSizes = pool_sizes;

    HE_CHECK_VKRESULT(vkCreateDescriptorPool(context->logical_device, &descriptor_pool_create_info, nullptr, &context->imgui_descriptor_pool));

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = context->instance;
    init_info.PhysicalDevice = context->physical_device;
    init_info.Device = context->logical_device;
    init_info.Queue = context->graphics_queue;
    init_info.QueueFamily = context->graphics_queue_family_index;
    init_info.DescriptorPool = context->imgui_descriptor_pool;
    init_info.MinImageCount = context->swapchain.image_count;
    init_info.ImageCount = context->swapchain.image_count;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineCache = context->pipeline_cache;

    Renderer_State *renderer_state = context->renderer_state;
    Vulkan_Render_Pass *vulkan_render_pass = &context->render_passes[ renderer_state->ui_render_pass.index ];
    ImGui_ImplVulkan_Init(&init_info, vulkan_render_pass->handle);

    VkCommandBuffer command_buffer = context->graphics_command_buffers[0];
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
    ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(context->graphics_queue);
    ImGui_ImplVulkan_DestroyFontUploadObjects();

    return true;
}

void vulkan_renderer_imgui_new_frame()
{
    ImGui_ImplVulkan_NewFrame();
}

void vulkan_renderer_imgui_render()
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((F32)(renderer_state->back_buffer_width), (F32)(renderer_state->back_buffer_height));

    if (renderer_state->imgui_docking)
    {
        ImGui::End();
    }

    ImGui::Render();

    if (renderer_state->engine->show_imgui)
    {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), context->command_buffer);
    }
}
// todo(amer): make this a utility function and use it in vulkan_renderer_create_texture...
Memory_Requirements vulkan_renderer_get_texture_memory_requirements(const Texture_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;

    U32 mip_levels = 1;
    
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_NONE;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM;

    if (is_color_format(descriptor.format))
    {
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        if (descriptor.is_attachment)
        {
            usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        else
        {
            usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
    }
    else
    {
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
        usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }

    VkFormat format = get_texture_format(descriptor.format);
    VkSampleCountFlagBits sample_count = get_sample_count(descriptor.sample_count);

    if (descriptor.mipmapping)
    {
        mip_levels = (U32)glm::floor(glm::log2((F32)glm::max(descriptor.width, descriptor.height)));
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkImageCreateInfo image_create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.extent.width = descriptor.width;
    image_create_info.extent.height = descriptor.height;
    image_create_info.extent.depth = 1;
    image_create_info.mipLevels = mip_levels;
    image_create_info.arrayLayers = 1;
    image_create_info.format = format;
    image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create_info.usage = usage;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_create_info.samples = sample_count;
    image_create_info.flags = 0;

    VkImage image = VK_NULL_HANDLE;
    HE_CHECK_VKRESULT(vkCreateImage(context->logical_device, &image_create_info, nullptr, &image));

    VkMemoryRequirements vulkan_memory_requirements = {};
    vkGetImageMemoryRequirements(context->logical_device, image, &vulkan_memory_requirements);    
    vkDestroyImage(context->logical_device, image, nullptr);

    Memory_Requirements result = {};
    result.size = vulkan_memory_requirements.size;
    result.alignment = vulkan_memory_requirements.alignment;

    return result;
}