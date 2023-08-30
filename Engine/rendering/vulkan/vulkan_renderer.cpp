#include <string.h>

#include "vulkan_renderer.h"
#include "core/platform.h"
#include "core/debugging.h"
#include "core/memory.h"
#include "core/file_system.h"
#include "core/engine.h"
#include "rendering/renderer.h"

#include "vulkan_image.h"
#include "vulkan_buffer.h"
#include "vulkan_swapchain.h"
#include "vulkan_shader.h"
#include "core/cvars.h"

#include "ImGui/backends/imgui_impl_vulkan.cpp"

static Vulkan_Context vulkan_context;

static VKAPI_ATTR VkBool32 VKAPI_CALL
vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                      VkDebugUtilsMessageTypeFlagsEXT message_type,
                      const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                      void *user_data)
{
    (void)message_severity;
    (void)message_type;
    (void)user_data;
    HOPE_DebugPrintf(Rendering, Trace, "%s\n", callback_data->pMessage);
    HOPE_Assert(message_severity != VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
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

static VkPhysicalDevice
pick_physical_device(VkInstance instance, VkSurfaceKHR surface,
                     VkPhysicalDeviceFeatures2 features,
                     VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features,
                     Memory_Arena *arena)
{
    Scoped_Temprary_Memory_Arena temp_arena(arena);

    U32 physical_device_count = 0;
    HOPE_CheckVkResult(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));

    if (!physical_device_count)
    {
        return VK_NULL_HANDLE;
    }

    VkPhysicalDevice *physical_devices = AllocateArray(&temp_arena,
                                                       VkPhysicalDevice,
                                                       physical_device_count);
    HOPE_Assert(physical_devices);

    HOPE_CheckVkResult(vkEnumeratePhysicalDevices(instance,
                                                  &physical_device_count,
                                                  physical_devices));

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

static bool
init_imgui(Vulkan_Context *context)
{
    VkDescriptorPoolSize pool_sizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1024 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1024 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1024 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1024 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1024 }
    };

    VkDescriptorPoolCreateInfo descriptor_pool_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descriptor_pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptor_pool_create_info.maxSets = 1024;
    descriptor_pool_create_info.poolSizeCount = HOPE_ArrayCount(pool_sizes);
    descriptor_pool_create_info.pPoolSizes = pool_sizes;

    HOPE_CheckVkResult(vkCreateDescriptorPool(context->logical_device,
                                              &descriptor_pool_create_info,
                                              nullptr,
                                              &context->imgui_descriptor_pool));

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = context->instance;
    init_info.PhysicalDevice = context->physical_device;
    init_info.Device = context->logical_device;
    init_info.Queue = context->graphics_queue;
    init_info.QueueFamily = context->graphics_queue_family_index;
    init_info.DescriptorPool = context->imgui_descriptor_pool;
    init_info.MinImageCount = context->swapchain.image_count;
    init_info.ImageCount = context->swapchain.image_count;
    init_info.MSAASamples = context->msaa_samples;
    init_info.PipelineCache = context->pipeline_cache;
    ImGui_ImplVulkan_Init(&init_info, context->render_pass);

    VkCommandBuffer command_buffer = context->graphics_command_buffers[0];
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info =
        { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    vkBeginCommandBuffer(command_buffer,
                         &command_buffer_begin_info);
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

static bool
init_vulkan(Vulkan_Context *context, Engine *engine, Memory_Arena *arena)
{
    context->engine = engine;
    context->allocator = &engine->memory.free_list_allocator;

    context->textures = AllocateArray(arena, Vulkan_Image, MAX_TEXTURE_COUNT);
    context->materials = AllocateArray(arena, Vulkan_Material, MAX_MATERIAL_COUNT);
    context->static_meshes = AllocateArray(arena, Vulkan_Static_Mesh, MAX_STATIC_MESH_COUNT);
    context->shaders = AllocateArray(arena, Vulkan_Shader, MAX_SHADER_COUNT);
    context->pipeline_states = AllocateArray(arena, Vulkan_Pipeline_State, MAX_PIPELINE_STATE_COUNT);

    const char *required_instance_extensions[] =
    {
#if HOPE_OS_WINDOWS
        "VK_KHR_win32_surface",
#endif

#if HOPE_VULKAN_DEBUGGING
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

    HOPE_Assert(required_api_version <= driver_api_version);

    HOPE_CVarGetString(engine_name, "platform");
    HOPE_CVarGetString(app_name, "platform");

    VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app_info.pApplicationName = app_name->data;
    app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName = engine_name->data;
    app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.apiVersion = required_api_version;

    VkInstanceCreateInfo instance_create_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instance_create_info.pApplicationInfo = &app_info;
    instance_create_info.enabledExtensionCount = HOPE_ArrayCount(required_instance_extensions);
    instance_create_info.ppEnabledExtensionNames = required_instance_extensions;

#if HOPE_VULKAN_DEBUGGING

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

    instance_create_info.enabledLayerCount = HOPE_ArrayCount(layers);
    instance_create_info.ppEnabledLayerNames = layers;
    instance_create_info.pNext = &debug_messenger_create_info;

#endif

    HOPE_CheckVkResult(vkCreateInstance(&instance_create_info, nullptr, &context->instance));

#if HOPE_VULKAN_DEBUGGING

    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerExt =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance,
                                                                  "vkCreateDebugUtilsMessengerEXT");
    HOPE_Assert(vkCreateDebugUtilsMessengerExt);

    HOPE_CheckVkResult(vkCreateDebugUtilsMessengerExt(context->instance,
                                                      &debug_messenger_create_info,
                                                      nullptr,
                                                      &context->debug_messenger));

#endif

    context->surface = (VkSurfaceKHR)platform_create_vulkan_surface(engine,
                                                                    context->instance);
    HOPE_Assert(context->surface);

    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features =
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };

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

    context->physical_device = pick_physical_device(context->instance,
                                                    context->surface,
                                                    physical_device_features2,
                                                    descriptor_indexing_features,
                                                    arena);
    HOPE_Assert(context->physical_device != VK_NULL_HANDLE);

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

    context->msaa_samples = VK_SAMPLE_COUNT_4_BIT;

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
             extension_index < HOPE_ArrayCount(required_device_extensions);
             extension_index++)
        {
            String device_extension = HOPE_String(required_device_extensions[extension_index]);
            bool is_extension_supported = false;

            for (U32 extension_property_index = 0;
                 extension_property_index < extension_property_count;
                 extension_property_index++)
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
        device_create_info.enabledExtensionCount = HOPE_ArrayCount(required_device_extensions);

        HOPE_CheckVkResult(vkCreateDevice(context->physical_device,
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
                           HOPE_ArrayCount(image_formats),
                           depth_stencil_formats,
                           HOPE_ArrayCount(depth_stencil_formats),
                           VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                           arena,
                           &context->swapchain_support);

    VkAttachmentDescription attachments_msaa[3] = {};

    attachments_msaa[0].format = context->swapchain_support.image_format;
    attachments_msaa[0].samples = context->msaa_samples;
    attachments_msaa[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments_msaa[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments_msaa[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments_msaa[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments_msaa[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments_msaa[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments_msaa[1].format = context->swapchain_support.image_format;
    attachments_msaa[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments_msaa[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments_msaa[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments_msaa[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments_msaa[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments_msaa[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments_msaa[1].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments_msaa[2].format = context->swapchain_support.depth_stencil_format;
    attachments_msaa[2].samples = context->msaa_samples;
    attachments_msaa[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments_msaa[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments_msaa[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments_msaa[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments_msaa[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments_msaa[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription attachments[2] = {};

    attachments[0].format = context->swapchain_support.image_format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = context->swapchain_support.depth_stencil_format;
    attachments[1].samples = context->msaa_samples;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


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

    if (context->msaa_samples != VK_SAMPLE_COUNT_1_BIT)
    {
        subpass.pResolveAttachments = &resolve_color_attachment_ref;
    }
    else
    {
        depth_stencil_attachment_ref.attachment = 1;
    }

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
    if (context->msaa_samples != VK_SAMPLE_COUNT_1_BIT)
    {
        render_pass_create_info.attachmentCount = HOPE_ArrayCount(attachments_msaa);
        render_pass_create_info.pAttachments = attachments_msaa;
    }
    else
    {
        render_pass_create_info.attachmentCount = HOPE_ArrayCount(attachments);
        render_pass_create_info.pAttachments = attachments;
    }

    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;

    render_pass_create_info.dependencyCount = 1;
    render_pass_create_info.pDependencies = &dependency;

    HOPE_CheckVkResult(vkCreateRenderPass(context->logical_device,
                                          &render_pass_create_info,
                                          nullptr, &context->render_pass));

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    U32 min_image_count = HOPE_MAX_FRAMES_IN_FLIGHT;
    U32 width = engine->window.width;
    U32 height = engine->window.height;
    bool swapchain_created = create_swapchain(context, width, height,
                                              min_image_count, present_mode, &context->swapchain);
    HOPE_Assert(swapchain_created);

    {
        Scoped_Temprary_Memory_Arena temp_arena(arena);

        U64 pipeline_cache_size = 0;
        U8 *pipeline_cache_data = nullptr;

        Read_Entire_File_Result result = read_entire_file(PIPELINE_CACHE_FILENAME, &temp_arena);
        if (result.success)
        {
            VkPipelineCacheHeaderVersionOne *pipeline_cache_header = (VkPipelineCacheHeaderVersionOne *)result.data;
            if (pipeline_cache_header->deviceID == context->physical_device_properties.deviceID &&
                pipeline_cache_header->vendorID == context->physical_device_properties.vendorID)
            {
                pipeline_cache_data = result.data;
                pipeline_cache_size = result.size;
            }
        }

        VkPipelineCacheCreateInfo pipeline_cache_create_info
            = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        pipeline_cache_create_info.initialDataSize = pipeline_cache_size;
        pipeline_cache_create_info.pInitialData = pipeline_cache_data;
        HOPE_CheckVkResult(vkCreatePipelineCache(context->logical_device, &pipeline_cache_create_info,
                                                 nullptr, &context->pipeline_cache));
    }

    Renderer_State *renderer_state = &context->engine->renderer_state;
    renderer_state->mesh_vertex_shader = allocate_shader(renderer_state);
    bool shader_loaded = load_shader(renderer_state->mesh_vertex_shader,
                                     "shaders/bin/mesh.vert.spv",
                                     context);
    HOPE_Assert(shader_loaded);

    renderer_state->mesh_fragment_shader = allocate_shader(renderer_state);
    shader_loaded = load_shader(renderer_state->mesh_fragment_shader,
                                "shaders/bin/mesh.frag.spv",
                                context);
    HOPE_Assert(shader_loaded);

    renderer_state->mesh_pipeline = allocate_pipeline_state(renderer_state);
    bool pipeline_created = create_graphics_pipeline(renderer_state->mesh_pipeline,
                                                     { renderer_state->mesh_vertex_shader,
                                                     renderer_state->mesh_fragment_shader },
                                                     context->render_pass,
                                                     context);
    HOPE_Assert(pipeline_created);

    VkCommandPoolCreateInfo graphics_command_pool_create_info
        = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };

    graphics_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    graphics_command_pool_create_info.queueFamilyIndex = context->graphics_queue_family_index;

    HOPE_CheckVkResult(vkCreateCommandPool(context->logical_device,
                                           &graphics_command_pool_create_info,
                                           nullptr, &context->graphics_command_pool));

    VkCommandBufferAllocateInfo graphics_command_buffer_allocate_info
        = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };

    graphics_command_buffer_allocate_info.commandPool = context->graphics_command_pool;
    graphics_command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    graphics_command_buffer_allocate_info.commandBufferCount = HOPE_MAX_FRAMES_IN_FLIGHT;
    HOPE_CheckVkResult(vkAllocateCommandBuffers(context->logical_device,
                                                &graphics_command_buffer_allocate_info,
                                                context->graphics_command_buffers));

    VkCommandPoolCreateInfo transfer_command_pool_create_info
        = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };

    transfer_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    transfer_command_pool_create_info.queueFamilyIndex = context->transfer_queue_family_index;
    HOPE_CheckVkResult(vkCreateCommandPool(context->logical_device,
                                           &transfer_command_pool_create_info,
                                           nullptr, &context->transfer_command_pool));

    // todo(amer): temprary
    U64 max_vertex_count = 1'000'000;
    U64 position_size = max_vertex_count * sizeof(glm::vec3);
    create_buffer(&context->position_buffer, context, position_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    U64 normal_size = max_vertex_count * sizeof(glm::vec3);
    create_buffer(&context->normal_buffer, context, normal_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    U64 uv_size = max_vertex_count * sizeof(glm::vec2);
    create_buffer(&context->uv_buffer, context, uv_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    U64 tangent_size = max_vertex_count * sizeof(glm::vec4);
    create_buffer(&context->tangent_buffer, context, tangent_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    context->max_vertex_count = max_vertex_count;

    U64 index_size = HOPE_MegaBytes(128);
    create_buffer(&context->index_buffer, context, index_size,
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    create_buffer(&context->transfer_buffer, context, HOPE_MegaBytes(512),
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    init_free_list_allocator(&context->transfer_allocator, context->transfer_buffer.data, context->transfer_buffer.size);
    renderer_state->transfer_allocator = &context->transfer_allocator;

    for (U32 frame_index = 0; frame_index < HOPE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        Vulkan_Buffer *global_uniform_buffer = &context->globals_uniform_buffers[frame_index];
        create_buffer(global_uniform_buffer, context, sizeof(Globals),
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        Vulkan_Buffer *object_storage_buffer = &context->object_storage_buffers[frame_index];
        create_buffer(object_storage_buffer, context, sizeof(Object_Data) * MAX_OBJECT_DATA_COUNT,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    VkDescriptorPoolSize descriptor_pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, HOPE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT }
    };

    VkDescriptorPoolCreateInfo descriptor_pool_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descriptor_pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    descriptor_pool_create_info.poolSizeCount = HOPE_ArrayCount(descriptor_pool_sizes);
    descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes;
    descriptor_pool_create_info.maxSets = (16 + 16 + HOPE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT) * HOPE_ArrayCount(descriptor_pool_sizes);

    HOPE_CheckVkResult(vkCreateDescriptorPool(context->logical_device,
                                              &descriptor_pool_create_info,
                                              nullptr, &context->descriptor_pool));

    // set 0
    {
        VkDescriptorSetLayout level0_descriptor_set_layouts[HOPE_MAX_FRAMES_IN_FLIGHT] = {};

        for (U32 frame_index = 0;
             frame_index < HOPE_MAX_FRAMES_IN_FLIGHT;
             frame_index++)
        {
            Vulkan_Pipeline_State *mesh_pipeline = context->pipeline_states + index_of(renderer_state, renderer_state->mesh_pipeline);
            level0_descriptor_set_layouts[frame_index] = mesh_pipeline->descriptor_set_layouts[0];
        }

        VkDescriptorSetAllocateInfo descriptor_set_allocation_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        descriptor_set_allocation_info.descriptorPool = context->descriptor_pool;
        descriptor_set_allocation_info.descriptorSetCount = HOPE_MAX_FRAMES_IN_FLIGHT;
        descriptor_set_allocation_info.pSetLayouts = level0_descriptor_set_layouts;

        HOPE_CheckVkResult(vkAllocateDescriptorSets(context->logical_device,
                                                    &descriptor_set_allocation_info,
                                                    context->descriptor_sets[0]));

        for (U32 frame_index = 0;
             frame_index < HOPE_MAX_FRAMES_IN_FLIGHT;
             frame_index++)
        {
            VkDescriptorBufferInfo globals_uniform_buffer_descriptor_info = {};
            globals_uniform_buffer_descriptor_info.buffer = context->globals_uniform_buffers[frame_index].handle;
            globals_uniform_buffer_descriptor_info.offset = 0;
            globals_uniform_buffer_descriptor_info.range = sizeof(Globals);

            VkWriteDescriptorSet globals_uniform_buffer_write_descriptor_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            globals_uniform_buffer_write_descriptor_set.dstSet = context->descriptor_sets[0][frame_index];
            globals_uniform_buffer_write_descriptor_set.dstBinding = 0;
            globals_uniform_buffer_write_descriptor_set.dstArrayElement = 0;
            globals_uniform_buffer_write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            globals_uniform_buffer_write_descriptor_set.descriptorCount = 1;
            globals_uniform_buffer_write_descriptor_set.pBufferInfo = &globals_uniform_buffer_descriptor_info;

            VkDescriptorBufferInfo object_data_storage_buffer_descriptor_info = {};
            object_data_storage_buffer_descriptor_info.buffer = context->object_storage_buffers[frame_index].handle;
            object_data_storage_buffer_descriptor_info.offset = 0;
            object_data_storage_buffer_descriptor_info.range = sizeof(Object_Data) * MAX_OBJECT_DATA_COUNT;

            VkWriteDescriptorSet object_data_storage_buffer_write_descriptor_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            object_data_storage_buffer_write_descriptor_set.dstSet = context->descriptor_sets[0][frame_index];
            object_data_storage_buffer_write_descriptor_set.dstBinding = 1;
            object_data_storage_buffer_write_descriptor_set.dstArrayElement = 0;
            object_data_storage_buffer_write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            object_data_storage_buffer_write_descriptor_set.descriptorCount = 1;
            object_data_storage_buffer_write_descriptor_set.pBufferInfo = &object_data_storage_buffer_descriptor_info;

            VkWriteDescriptorSet write_descriptor_sets[] =
            {
                globals_uniform_buffer_write_descriptor_set,
                object_data_storage_buffer_write_descriptor_set
            };

            vkUpdateDescriptorSets(context->logical_device,
                                   HOPE_ArrayCount(write_descriptor_sets),
                                   write_descriptor_sets, 0, nullptr);
        }
    }

    // set 1
    {
        VkDescriptorSetLayout level1_descriptor_set_layouts[HOPE_MAX_FRAMES_IN_FLIGHT] = {};

        for (U32 frame_index = 0;
             frame_index < HOPE_MAX_FRAMES_IN_FLIGHT;
             frame_index++)
        {
            Vulkan_Pipeline_State *mesh_pipeline = context->pipeline_states + index_of(renderer_state, renderer_state->mesh_pipeline);
            level1_descriptor_set_layouts[frame_index] = mesh_pipeline->descriptor_set_layouts[1];
        }

        VkDescriptorSetAllocateInfo descriptor_set_allocation_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        descriptor_set_allocation_info.descriptorPool = context->descriptor_pool;
        descriptor_set_allocation_info.descriptorSetCount = U32(HOPE_MAX_FRAMES_IN_FLIGHT);
        descriptor_set_allocation_info.pSetLayouts = level1_descriptor_set_layouts;

        HOPE_CheckVkResult(vkAllocateDescriptorSets(context->logical_device,
                                                    &descriptor_set_allocation_info,
                                                    context->descriptor_sets[1]));
    }

    VkSemaphoreCreateInfo semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (U32 sync_primitive_index = 0;
         sync_primitive_index < HOPE_MAX_FRAMES_IN_FLIGHT;
         sync_primitive_index++)
    {
        HOPE_CheckVkResult(vkCreateSemaphore(context->logical_device,
                                             &semaphore_create_info,
                                             nullptr,
                                             &context->image_available_semaphores[sync_primitive_index]));

        HOPE_CheckVkResult(vkCreateSemaphore(context->logical_device,
                                             &semaphore_create_info,
                                             nullptr,
                                             &context->rendering_finished_semaphores[sync_primitive_index]));


        HOPE_CheckVkResult(vkCreateFence(context->logical_device,
                                         &fence_create_info,
                                         nullptr,
                                         &context->frame_in_flight_fences[sync_primitive_index]));
    }

    context->current_frame_in_flight_index = 0;
    context->frames_in_flight = 2;
    HOPE_Assert(context->frames_in_flight <= HOPE_MAX_FRAMES_IN_FLIGHT);

    init_imgui(context);
    return true;
}

void deinit_vulkan(Vulkan_Context *context)
{
    vkDeviceWaitIdle(context->logical_device);
    vkDestroyDescriptorPool(context->logical_device, context->descriptor_pool, nullptr);

    vkDestroyDescriptorPool(context->logical_device, context->imgui_descriptor_pool, nullptr);

    ImGui_ImplVulkan_Shutdown();

    destroy_buffer(&context->transfer_buffer, context->logical_device);
    destroy_buffer(&context->position_buffer, context->logical_device);
    destroy_buffer(&context->normal_buffer, context->logical_device);
    destroy_buffer(&context->uv_buffer, context->logical_device);
    destroy_buffer(&context->tangent_buffer, context->logical_device);
    destroy_buffer(&context->index_buffer, context->logical_device);

    for (U32 frame_index = 0;
         frame_index < HOPE_MAX_FRAMES_IN_FLIGHT;
         frame_index++)
    {
        destroy_buffer(&context->globals_uniform_buffers[frame_index], context->logical_device);
        destroy_buffer(&context->object_storage_buffers[frame_index], context->logical_device);

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

    U64 pipeline_cache_size = 0;
    vkGetPipelineCacheData(context->logical_device,
                           context->pipeline_cache,
                           &pipeline_cache_size,
                           nullptr);


    if (pipeline_cache_size)
    {
        U8 *pipeline_cache_data = AllocateArray(context->allocator, U8, pipeline_cache_size);
        vkGetPipelineCacheData(context->logical_device,
                               context->pipeline_cache,
                               &pipeline_cache_size,
                               pipeline_cache_data);

        write_entire_file(PIPELINE_CACHE_FILENAME, pipeline_cache_data, pipeline_cache_size);
    }

    vkDestroyPipelineCache(context->logical_device, context->pipeline_cache, nullptr);

    vkDestroyRenderPass(context->logical_device, context->render_pass, nullptr);

    vkDestroySurfaceKHR(context->instance, context->surface, nullptr);
    vkDestroyDevice(context->logical_device, nullptr);

#if HOPE_VULKAN_DEBUGGING
     PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerExt =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance,
                                                                  "vkDestroyDebugUtilsMessengerEXT");
    HOPE_Assert(vkDestroyDebugUtilsMessengerExt);
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

    if (width != 0 && height != 0)
    {
        recreate_swapchain(&vulkan_context,
                           &vulkan_context.swapchain,
                           width,
                           height,
                           vulkan_context.swapchain.present_mode);
    }
}

void vulkan_renderer_imgui_new_frame()
{
    ImGui_ImplVulkan_NewFrame();
}

void vulkan_renderer_begin_frame(struct Renderer_State *renderer_state, const Scene_Data *scene_data)
{
    Vulkan_Context *context = &vulkan_context;
    U32 current_frame_in_flight_index = context->current_frame_in_flight_index;

    vkWaitForFences(context->logical_device,
                    1, &context->frame_in_flight_fences[current_frame_in_flight_index],
                    VK_TRUE, UINT64_MAX);

    Globals globals = {};
    globals.view = scene_data->view;
    globals.projection = scene_data->projection;
    globals.projection[1][1] *= -1;
    globals.directional_light_direction = glm::vec4(scene_data->directional_light.direction, 0.0f);
    globals.directional_light_color = sRGB_to_linear(scene_data->directional_light.color) * scene_data->directional_light.intensity;

    Vulkan_Buffer *global_uniform_buffer = &context->globals_uniform_buffers[current_frame_in_flight_index];
    memcpy(global_uniform_buffer->data, &globals, sizeof(Globals));

    context->object_data_base = (Object_Data *)context->object_storage_buffers[current_frame_in_flight_index].data;
    context->object_data_count = 0;

    U32 width = renderer_state->back_buffer_width;
    U32 height = renderer_state->back_buffer_height;

    if ((width != context->swapchain.width || height != context->swapchain.height) &&
        width != 0 && height != 0)
    {
        recreate_swapchain(context,
                           &context->swapchain,
                           width,
                           height,
                           context->swapchain.present_mode);
    }

    VkResult result = vkAcquireNextImageKHR(context->logical_device,
                                            context->swapchain.handle,
                                            UINT64_MAX,
                                            context->image_available_semaphores[current_frame_in_flight_index],
                                            VK_NULL_HANDLE,
                                            &context->current_swapchain_image_index);

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
        HOPE_Assert(result == VK_SUCCESS);
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
    render_pass_begin_info.clearValueCount = HOPE_ArrayCount(clear_values);
    render_pass_begin_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(command_buffer,
                         &render_pass_begin_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    VkDescriptorImageInfo descriptor_image_infos[MAX_TEXTURE_COUNT] = {};

    for (U32 texture_index = 0;
         texture_index < renderer_state->texture_count;
         texture_index++)
    {
        Texture *texture = &renderer_state->textures[texture_index];
        Vulkan_Image *vulkan_image = context->textures + index_of(renderer_state, texture);

        descriptor_image_infos[texture_index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptor_image_infos[texture_index].imageView = vulkan_image->view;
        descriptor_image_infos[texture_index].sampler = vulkan_image->sampler;
    }

    VkWriteDescriptorSet write_descriptor_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write_descriptor_set.dstSet = context->descriptor_sets[1][current_frame_in_flight_index];
    write_descriptor_set.dstBinding = 0;
    write_descriptor_set.dstArrayElement = 0;
    write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_descriptor_set.descriptorCount = renderer_state->texture_count;
    write_descriptor_set.pImageInfo = descriptor_image_infos;

    vkUpdateDescriptorSets(context->logical_device, 1, &write_descriptor_set, 0, nullptr);

    VkDescriptorSet descriptor_sets[] =
    {
        context->descriptor_sets[0][current_frame_in_flight_index],
        context->descriptor_sets[1][current_frame_in_flight_index]
    };

    Vulkan_Pipeline_State *mesh_pipeline = context->pipeline_states + index_of(renderer_state, renderer_state->mesh_pipeline);

    vkCmdBindDescriptorSets(command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            mesh_pipeline->layout,
                            0, HOPE_ArrayCount(descriptor_sets),
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

    VkBuffer vertex_buffers[] =
    {
        context->position_buffer.handle,
        context->normal_buffer.handle,
        context->uv_buffer.handle,
        context->tangent_buffer.handle
    };

    VkDeviceSize offsets[] = { 0, 0, 0, 0 };

    vkCmdBindVertexBuffers(command_buffer,
                           0, HOPE_ArrayCount(vertex_buffers), vertex_buffers, offsets);

    vkCmdBindIndexBuffer(command_buffer,
                         context->index_buffer.handle, 0, VK_INDEX_TYPE_UINT16);
}

void vulkan_renderer_submit_static_mesh(struct Renderer_State *renderer_state,
                                        const struct Static_Mesh *static_mesh, const glm::mat4 &transform)
{
    Vulkan_Context *context = &vulkan_context;
    HOPE_Assert(context->object_data_count < MAX_OBJECT_DATA_COUNT);
    U32 object_data_index = context->object_data_count++;
    Object_Data *object_data = &context->object_data_base[object_data_index];
    object_data->model = transform;
    U32 current_frame_in_flight_index = context->current_frame_in_flight_index;
    VkCommandBuffer command_buffer = context->graphics_command_buffers[current_frame_in_flight_index];

    Vulkan_Static_Mesh *vulkan_static_mesh = context->static_meshes + index_of(renderer_state, static_mesh);

    Material *material = static_mesh->material;
    Vulkan_Material *vulkan_material = context->materials + index_of(renderer_state, material);

    Vulkan_Buffer *material_buffer = &vulkan_material->buffers[current_frame_in_flight_index];
    copy_memory(material_buffer->data, material->data, material->size);

    Vulkan_Pipeline_State *vulkan_pipeline_state = context->pipeline_states + index_of(renderer_state, material->pipeline_state);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_pipeline_state->handle);

    VkDescriptorSet descriptor_sets[] =
    {
        vulkan_material->descriptor_sets[current_frame_in_flight_index]
    };

    vkCmdBindDescriptorSets(command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vulkan_pipeline_state->layout,
                            2, HOPE_ArrayCount(descriptor_sets),
                            descriptor_sets,
                            0, nullptr);

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

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((F32)(renderer_state->back_buffer_width),
                            (F32)(renderer_state->back_buffer_height));

    if (renderer_state->engine->imgui_docking)
    {
        ImGui::End();
    }

    ImGui::Render();

    if (renderer_state->engine->show_imgui)
    {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
    }

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

    if (io.ConfigFlags&ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

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
        HOPE_Assert(result == VK_SUCCESS);
    }

    context->current_frame_in_flight_index++;
    if (context->current_frame_in_flight_index == context->frames_in_flight)
    {
        context->current_frame_in_flight_index = 0;
    }
}

bool vulkan_renderer_create_texture(Texture *texture, const Texture_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Image *image = context->textures + index_of(&context->engine->renderer_state, texture);

    HOPE_Assert(descriptor.format == TextureFormat_RGBA); // todo(amer): only supporting RGBA for now.
    create_image(image, context, descriptor.width, descriptor.height,
                 VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT|
                 VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, descriptor.mipmapping);

    // todo(amer): only supporting RGBA for now.
    U64 size = (U64)descriptor.width * (U64)descriptor.height * sizeof(U32);
    U64 transfered_data_offset = (U8 *)descriptor.data - context->transfer_allocator.base;

    copy_data_to_image_from_buffer(context, image, descriptor.width, descriptor.height, &context->transfer_buffer,
                                   transfered_data_offset, size);

    texture->width = descriptor.width;
    texture->height = descriptor.height;

    return true;
}

void vulkan_renderer_destroy_texture(Texture *texture)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Image *vulkan_image = context->textures + index_of(&context->engine->renderer_state, texture);
    destroy_image(vulkan_image, &vulkan_context);
}

bool vulkan_renderer_create_shader(Shader *shader, const Shader_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    bool loaded = load_shader(shader, descriptor.path, context);
    return loaded;
}

void vulkan_renderer_destroy_shader(Shader *shader)
{
    Vulkan_Context *context = &vulkan_context;
    destroy_shader(shader, context);
}

bool vulkan_renderer_create_pipeline_state(Pipeline_State *pipeline_state, const Pipeline_State_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    return create_graphics_pipeline(pipeline_state, descriptor.shaders, context->render_pass, context);
}

void vulkan_renderer_destroy_pipeline_state(Pipeline_State *pipeline_state)
{
    Vulkan_Context *context = &vulkan_context;
    destroy_pipeline(pipeline_state, context);
}


bool vulkan_renderer_create_material(Material *material, const Material_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Material *vulkan_material = context->materials + index_of(&context->engine->renderer_state, material);
    Vulkan_Pipeline_State *vulkan_pipeline_state = context->pipeline_states + index_of(&context->engine->renderer_state, descriptor.pipeline_state);
    Shader_Struct *properties = nullptr;

    for (U32 shader_index = 0; shader_index < descriptor.pipeline_state->shader_count; shader_index++)
    {
        Shader *shader = descriptor.pipeline_state->shaders[shader_index];
        for (U32 struct_index = 0; struct_index < shader->struct_count; struct_index++)
        {
            Shader_Struct *shader_struct = &shader->structs[struct_index];
            if (shader_struct->name == "Material_Properties")
            {
                properties = shader_struct;
                break;
            }
        }
    }

    HOPE_Assert(properties);

    Shader_Struct_Member *last_member = &properties->members[properties->member_count - 1];
    U32 last_member_size = get_size_of_shader_data_type(last_member->data_type);
    U32 size = last_member->offset + last_member_size;

    VkDescriptorSetLayout level2_descriptor_set_layouts[HOPE_MAX_FRAMES_IN_FLIGHT] = {};

    for (U32 frame_index = 0; frame_index < HOPE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        Vulkan_Buffer *buffer = &vulkan_material->buffers[frame_index];
        create_buffer(buffer, context, size,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        level2_descriptor_set_layouts[frame_index] = vulkan_pipeline_state->descriptor_set_layouts[2];
    }

    VkDescriptorSetAllocateInfo descriptor_set_allocation_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    descriptor_set_allocation_info.descriptorPool = context->descriptor_pool;
    descriptor_set_allocation_info.descriptorSetCount = HOPE_MAX_FRAMES_IN_FLIGHT;
    descriptor_set_allocation_info.pSetLayouts = level2_descriptor_set_layouts;

    HOPE_CheckVkResult(vkAllocateDescriptorSets(context->logical_device,
                                                &descriptor_set_allocation_info,
                                                vulkan_material->descriptor_sets));

    for (U32 frame_index = 0; frame_index < HOPE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        VkDescriptorBufferInfo material_uniform_buffer_descriptor_info = {};
        material_uniform_buffer_descriptor_info.buffer = vulkan_material->buffers[frame_index].handle;
        material_uniform_buffer_descriptor_info.offset = 0;
        material_uniform_buffer_descriptor_info.range = size;

        VkWriteDescriptorSet material_uniform_buffer_write_descriptor_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        material_uniform_buffer_write_descriptor_set.dstSet = vulkan_material->descriptor_sets[frame_index];
        material_uniform_buffer_write_descriptor_set.dstBinding = 0;
        material_uniform_buffer_write_descriptor_set.dstArrayElement = 0;
        material_uniform_buffer_write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        material_uniform_buffer_write_descriptor_set.descriptorCount = 1;
        material_uniform_buffer_write_descriptor_set.pBufferInfo = &material_uniform_buffer_descriptor_info;

        VkWriteDescriptorSet write_descriptor_sets[] =
        {
            material_uniform_buffer_write_descriptor_set,
        };

        vkUpdateDescriptorSets(context->logical_device,
                               HOPE_ArrayCount(write_descriptor_sets),
                               write_descriptor_sets, 0, nullptr);
    }

    material->pipeline_state = descriptor.pipeline_state;
    material->data = AllocateArray(context->allocator, U8, size);
    material->size = size;

    material->properties = properties;

    return true;
}

void vulkan_renderer_destroy_material(Material *material)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Material *vulkan_material = context->materials + index_of(&context->engine->renderer_state, material);
    for (U32 frame_index = 0; frame_index < HOPE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        destroy_buffer(&vulkan_material->buffers[frame_index], context->logical_device);
    }
}

bool vulkan_renderer_create_static_mesh(Static_Mesh *static_mesh, const Static_Mesh_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;

    U64 position_size = descriptor.vertex_count * sizeof(glm::vec3);
    U64 normal_size = descriptor.vertex_count * sizeof(glm::vec3);
    U64 uv_size = descriptor.vertex_count * sizeof(glm::vec2);
    U64 tangent_size = descriptor.vertex_count * sizeof(glm::vec4);
    U64 index_size = descriptor.index_count * sizeof(U16);

    HOPE_Assert(context->vertex_count + descriptor.vertex_count <= context->max_vertex_count);
    static_mesh->index_count = descriptor.index_count;
    static_mesh->vertex_count = descriptor.vertex_count;

    Vulkan_Static_Mesh *vulkan_static_mesh = context->static_meshes + index_of(&context->engine->renderer_state, static_mesh);

    U64 position_offset = (U8 *)descriptor.positions - context->transfer_allocator.base;
    U64 normal_offset = (U8*)descriptor.normals - context->transfer_allocator.base;
    U64 uv_offset = (U8*)descriptor.uvs - context->transfer_allocator.base;
    U64 tangent_offset = (U8*)descriptor.tangents - context->transfer_allocator.base;

    U64 indicies_offset = (U8 *)descriptor.indices - context->transfer_allocator.base;

    copy_data_to_buffer_from_buffer(context, &context->position_buffer, context->vertex_count * sizeof(glm::vec3), &context->transfer_buffer, position_offset, position_size);

    copy_data_to_buffer_from_buffer(context, &context->normal_buffer, context->vertex_count * sizeof(glm::vec3), &context->transfer_buffer, normal_offset, normal_size);

    copy_data_to_buffer_from_buffer(context, &context->uv_buffer, context->vertex_count * sizeof(glm::vec2), &context->transfer_buffer, uv_offset, uv_size);

    copy_data_to_buffer_from_buffer(context, &context->tangent_buffer, context->vertex_count * sizeof(glm::vec4), &context->transfer_buffer, tangent_offset, position_size);

    copy_data_to_buffer_from_buffer(context, &context->index_buffer, context->index_offset, &context->transfer_buffer, indicies_offset, index_size);

    vulkan_static_mesh->first_vertex = (S32)u64_to_u32(context->vertex_count);
    vulkan_static_mesh->first_index = u64_to_u32(context->index_offset / sizeof(U16));

    context->vertex_count += descriptor.vertex_count;
    context->index_offset += index_size;
    return true;
}

void vulkan_renderer_destroy_static_mesh(Static_Mesh *static_mesh)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Static_Mesh *vulkan_static_mesh = context->static_meshes + index_of(&context->engine->renderer_state, static_mesh);
    // todo(amer): static mesh allocator...
}