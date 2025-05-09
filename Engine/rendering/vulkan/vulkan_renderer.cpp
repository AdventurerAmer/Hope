#include <string.h>

#include "vulkan_renderer.h"
#include "vulkan_swapchain.h"
#include "vulkan_shader.h"
#include "vulkan_utils.h"

#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"

#include "core/platform.h"
#include "core/logging.h"
#include "core/memory.h"
#include "core/file_system.h"
#include "core/engine.h"
#include "core/cvars.h"

#include "containers/dynamic_array.h"
#include "ImGui/backends/imgui_impl_vulkan.cpp"

#include <filesystem> // todo(amer): to be removed

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

static Vulkan_Context vulkan_context;

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data)
{
    (void)message_severity;
    (void)message_type;
    (void)user_data;

    // todo(amer): validation layer bugs...
    const char *black_list[] =
    {
        "VUID-VkSwapchainCreateInfoKHR-imageFormat-01778",
        "VUID-VkImageViewCreateInfo-usage-02275",
        "UNASSIGNED-Threading-MultipleThreads-Write"
    };

    for (U32 i = 0; i < HE_ARRAYCOUNT(black_list); i++)
    {
        if (strcmp(callback_data->pMessageIdName, black_list[i]) == 0)
        {
            return VK_FALSE;
        }
    }
    
    HE_LOG(Rendering, Trace, "%s\n", callback_data->pMessage);
    HE_ASSERT(message_severity != VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
    return VK_FALSE;
}

static bool is_physical_device_supports_all_features(VkPhysicalDevice physical_device)
{
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
    
    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
    descriptor_indexing_features.pNext = &sync2_features;
    
    VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features2.pNext = &descriptor_indexing_features;
    vkGetPhysicalDeviceFeatures2(physical_device, &features2);
    
    if (!features2.features.samplerAnisotropy)
    {
        return false;
    }

    if (!features2.features.sampleRateShading)
    {
        return false;
    }

    if (!features2.features.fragmentStoresAndAtomics)
    {
        return false;
    }

    if (!descriptor_indexing_features.runtimeDescriptorArray)
    {
        return false;
    }

    if (!sync2_features.synchronization2)
    {
        return false;
    }

    return true;
}

static VkPhysicalDevice pick_physical_device(VkInstance instance, VkSurfaceKHR surface)
{
    Memory_Context memory_context = grab_memory_context();

    U32 physical_device_count = 0;
    HE_CHECK_VKRESULT(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));

    if (!physical_device_count)
    {
        return VK_NULL_HANDLE;
    }

    VkPhysicalDevice *physical_devices = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkPhysicalDevice, physical_device_count);

    HE_CHECK_VKRESULT(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices));

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    U32 best_physical_device_score_so_far = 0;

    for (U32 physical_device_index = 0; physical_device_index < physical_device_count; physical_device_index++)
    {
        VkPhysicalDevice *current_physical_device = &physical_devices[physical_device_index];

        if (!is_physical_device_supports_all_features(*current_physical_device))
        {
            continue;
        }

        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(*current_physical_device, &properties);

        U32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(*current_physical_device, &queue_family_count, nullptr);

        bool can_physical_device_do_graphics = false;
        bool can_physical_device_present = false;

        VkQueueFamilyProperties *queue_families = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkQueueFamilyProperties, queue_family_count);

        vkGetPhysicalDeviceQueueFamilyProperties(*current_physical_device, &queue_family_count, queue_families);

        for (U32 queue_family_index = 0; queue_family_index < queue_family_count; queue_family_index++)
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

static void* vulkan_allocate(void *user_data, size_t size, size_t alignment, VkSystemAllocationScope allocation_scope)
{
    HE_ASSERT(alignment <= HE_MAX_U16);
    return allocate((Free_List_Allocator *)user_data, size, (U16)alignment);
}

static void vulkan_deallocate(void *user_data, void *memory)
{
    return deallocate((Free_List_Allocator *)user_data, memory);
}

static void* vulkan_reallocate(void *user_data, void *original, size_t size, size_t alignment, VkSystemAllocationScope allocation_scope)
{
    HE_ASSERT(alignment <= HE_MAX_U16);
    return reallocate((Free_List_Allocator *)user_data, original, 0, size, (U16)alignment);
}

static VkDescriptorPool create_descriptor_bool(U32 set_count)
{
    Vulkan_Context *context = &vulkan_context;
    Array< VkDescriptorPoolSize, HE_MAX_DESCRIPTOR_POOL_SIZE_RATIO_COUNT > descriptor_pool_sizes;

    for (U32 i = 0; i < context->descriptor_pool_ratios.count; i++)
    {
        VkDescriptorPoolSize *pool_size = &descriptor_pool_sizes[i];
        pool_size->type = context->descriptor_pool_ratios[i].type;
        pool_size->descriptorCount = (U32)(set_count * context->descriptor_pool_ratios[i].ratio);
    }

    VkDescriptorPoolCreateInfo descriptor_pool_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descriptor_pool_create_info.flags = 0;
    descriptor_pool_create_info.maxSets = set_count;
    descriptor_pool_create_info.poolSizeCount = context->descriptor_pool_ratios.count;
    descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes.data;
    
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    HE_CHECK_VKRESULT(vkCreateDescriptorPool(context->logical_device, &descriptor_pool_create_info, &context->allocation_callbacks, &descriptor_pool));
    
    return descriptor_pool;
}

Vulkan_Descriptor_Pool_Allocator create_descriptor_pool_allocator(U32 initial_set_count, Allocator allocator = {})
{
    Vulkan_Descriptor_Pool_Allocator descriptor_pool_allocator = {};
    descriptor_pool_allocator.set_count_per_pool    = initial_set_count;
    descriptor_pool_allocator.ready_pools.allocator = allocator;
    descriptor_pool_allocator.full_pools.allocator  = allocator;
    return descriptor_pool_allocator;
}

void destroy_descriptor_pool_allocator(Vulkan_Descriptor_Pool_Allocator *allocator)
{
    Vulkan_Context *context = &vulkan_context;

    for (U32 i = 0; i < allocator->ready_pools.count; i++)
    {
        vkDestroyDescriptorPool(context->logical_device, allocator->ready_pools[i], &context->allocation_callbacks);
    }

    for (U32 i = 0; i < allocator->full_pools.count; i++)
    {
        vkDestroyDescriptorPool(context->logical_device, allocator->full_pools[i], &context->allocation_callbacks);
    }

    deinit(&allocator->ready_pools);
    deinit(&allocator->full_pools);
}

static bool init_vulkan(Vulkan_Context *context, Engine *engine, Renderer_State *renderer_state)
{
    Memory_Context memory_context = grab_memory_context();

    context->renderer_state = renderer_state;
    
    VkAllocationCallbacks *allocation_callbacks = &context->allocation_callbacks;
    allocation_callbacks->pUserData = memory_context.general_allocator.data;
    allocation_callbacks->pfnAllocation = &vulkan_allocate;
    allocation_callbacks->pfnReallocation = &vulkan_reallocate;
    allocation_callbacks->pfnFree = &vulkan_deallocate;
    allocation_callbacks->pfnInternalAllocation = nullptr;
    allocation_callbacks->pfnInternalFree = nullptr;

    context->buffers = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.permenent_allocator, Vulkan_Buffer, HE_MAX_BUFFER_COUNT);
    context->textures = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.permenent_allocator, Vulkan_Image, HE_MAX_TEXTURE_COUNT);
    context->samplers = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.permenent_allocator, Vulkan_Sampler, HE_MAX_SAMPLER_COUNT);
    context->shaders = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.permenent_allocator, Vulkan_Shader, HE_MAX_SHADER_COUNT);
    context->pipeline_states = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.permenent_allocator, Vulkan_Pipeline_State, HE_MAX_PIPELINE_STATE_COUNT);
    context->bind_groups = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.permenent_allocator, Vulkan_Bind_Group, HE_MAX_BIND_GROUP_COUNT);
    context->render_passes = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.permenent_allocator, Vulkan_Render_Pass, HE_MAX_RENDER_PASS_COUNT);
    context->frame_buffers = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.permenent_allocator, Vulkan_Frame_Buffer, HE_MAX_FRAME_BUFFER_COUNT);
    context->semaphores = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.permenent_allocator, Vulkan_Semaphore, HE_MAX_SEMAPHORE_COUNT);
    context->upload_requests = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.permenent_allocator, Vulkan_Upload_Request, HE_MAX_UPLOAD_REQUEST_COUNT);

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

    U32 required_api_version = VK_API_VERSION_1_2;
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

    HE_CHECK_VKRESULT(vkCreateInstance(&instance_create_info, &context->allocation_callbacks, &context->instance));

#if HE_GRAPHICS_DEBUGGING

    auto vkCreateDebugUtilsMessengerExt = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance, "vkCreateDebugUtilsMessengerEXT");
    HE_ASSERT(vkCreateDebugUtilsMessengerExt);

    HE_CHECK_VKRESULT(vkCreateDebugUtilsMessengerExt(context->instance, &debug_messenger_create_info, &context->allocation_callbacks, &context->debug_messenger));

#endif

    context->surface = (VkSurfaceKHR)platform_create_vulkan_surface(engine, context->instance, &context->allocation_callbacks);
    HE_ASSERT(context->surface);

    VkPhysicalDeviceVulkan12Features features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features.descriptorIndexing = VK_TRUE;
    features.runtimeDescriptorArray = VK_TRUE;
    features.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    features.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    features.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    features.descriptorBindingPartiallyBound = VK_TRUE;
    features.timelineSemaphore = VK_TRUE;
    features.uniformBufferStandardLayout = VK_TRUE;

    VkPhysicalDeviceRobustness2FeaturesEXT robustness2_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
    robustness2_features.robustBufferAccess2 = VK_TRUE;
    robustness2_features.pNext = &features;

    VkPhysicalDeviceSynchronization2FeaturesKHR physical_device_sync2_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
    physical_device_sync2_features.synchronization2 = VK_TRUE;
    physical_device_sync2_features.pNext = &robustness2_features;

    VkPhysicalDeviceFeatures2 physical_device_features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    physical_device_features2.features.samplerAnisotropy = VK_TRUE;
    physical_device_features2.features.sampleRateShading = VK_TRUE;
    physical_device_features2.features.robustBufferAccess = VK_TRUE;
    physical_device_features2.features.fragmentStoresAndAtomics = VK_TRUE;
    physical_device_features2.features.independentBlend = VK_TRUE;
    physical_device_features2.pNext = &physical_device_sync2_features;

    context->physical_device = pick_physical_device(context->instance, context->surface);
    HE_ASSERT(context->physical_device != VK_NULL_HANDLE);

    vkGetPhysicalDeviceMemoryProperties(context->physical_device, &context->physical_device_memory_properties);
    vkGetPhysicalDeviceProperties(context->physical_device, &context->physical_device_properties);

    context->graphics_queue_family_index = 0;
    context->present_queue_family_index = 0;

    U32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device, &queue_family_count, nullptr);

    VkQueueFamilyProperties *queue_families = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkQueueFamilyProperties, queue_family_count);
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

    context->compute_queue_family_index = context->graphics_queue_family_index;

    for (U32 queue_family_index = 0; queue_family_index < queue_family_count; queue_family_index++)
    {
        VkQueueFamilyProperties *queue_family = &queue_families[queue_family_index];
        if ((queue_family->queueFlags & VK_QUEUE_COMPUTE_BIT) && !(queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            context->compute_queue_family_index = queue_family_index;
            break;
        }
    }

    F32 queue_priority = 1.0f;
    VkDeviceQueueCreateInfo *queue_create_infos = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkDeviceQueueCreateInfo, 3);

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

    if ((context->compute_queue_family_index != context->graphics_queue_family_index)
        && (context->compute_queue_family_index != context->present_queue_family_index)
        && (context->compute_queue_family_index != context->transfer_queue_family_index))
    {
        U32 queue_create_info_index = queue_create_info_count++;
        queue_create_infos[queue_create_info_index].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[queue_create_info_index].queueFamilyIndex = context->compute_queue_family_index;
        queue_create_infos[queue_create_info_index].queueCount = 1;
        queue_create_infos[queue_create_info_index].pQueuePriorities = &queue_priority;
    }

    const char *required_device_extensions[] =
    {
        "VK_KHR_swapchain",
        "VK_KHR_push_descriptor",
        "VK_EXT_descriptor_indexing",
        "VK_KHR_timeline_semaphore",
        "VK_KHR_synchronization2",
        "VK_EXT_robustness2",
        "VK_KHR_uniform_buffer_standard_layout",
    };

    U32 extension_property_count = 0;
    vkEnumerateDeviceExtensionProperties(context->physical_device, nullptr, &extension_property_count, nullptr);

    VkExtensionProperties *extension_properties = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkExtensionProperties, extension_property_count);

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

    HE_CHECK_VKRESULT(vkCreateDevice(context->physical_device, &device_create_info, &context->allocation_callbacks, &context->logical_device));

    vkGetDeviceQueue(context->logical_device, context->graphics_queue_family_index, 0, &context->graphics_queue);
    vkGetDeviceQueue(context->logical_device, context->present_queue_family_index, 0, &context->present_queue);
    vkGetDeviceQueue(context->logical_device, context->transfer_queue_family_index, 0, &context->transfer_queue);
    vkGetDeviceQueue(context->logical_device, context->compute_queue_family_index, 0, &context->compute_queue);

    VmaVulkanFunctions vulkan_functions = {};
    vulkan_functions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    vulkan_functions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocator_create_info = {};
    allocator_create_info.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    allocator_create_info.vulkanApiVersion = VK_API_VERSION_1_2;
    allocator_create_info.physicalDevice = context->physical_device;
    allocator_create_info.device = context->logical_device;
    allocator_create_info.instance = context->instance;
    allocator_create_info.pVulkanFunctions = &vulkan_functions;
    allocator_create_info.pAllocationCallbacks = &context->allocation_callbacks;

    vmaCreateAllocator(&allocator_create_info, &context->allocator);

    VkFormat image_formats[] =
    {
        VK_FORMAT_R8G8B8A8_UNORM,
    };

    init_swapchain_support(context, image_formats, HE_ARRAYCOUNT(image_formats), &context->swapchain_support);

    VkPresentModeKHR present_mode = pick_present_mode(renderer_state->vsync, &context->swapchain_support);
    U32 min_image_count = HE_MAX_FRAMES_IN_FLIGHT;
    U32 width = renderer_state->back_buffer_width;
    U32 height = renderer_state->back_buffer_height;

    bool swapchain_created = create_swapchain(context, width, height, min_image_count, present_mode, &context->swapchain);
    HE_ASSERT(swapchain_created);

    VkPipelineCacheCreateInfo pipeline_cache_create_info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };

    Read_Entire_File_Result result = read_entire_file(HE_STRING_LITERAL(HE_VULKAN_PIPELINE_CACHE_FILE_PATH), memory_context.temp_allocator);
    if (result.success)
    {
        VkPipelineCacheHeaderVersionOne *pipeline_cache_header = (VkPipelineCacheHeaderVersionOne *)result.data;
        if (pipeline_cache_header->deviceID == context->physical_device_properties.deviceID && pipeline_cache_header->vendorID == context->physical_device_properties.vendorID)
        {
            pipeline_cache_create_info.initialDataSize = result.size;
            pipeline_cache_create_info.pInitialData    = result.data;
        }
    }

    HE_CHECK_VKRESULT(vkCreatePipelineCache(context->logical_device, &pipeline_cache_create_info, &context->allocation_callbacks, &context->pipeline_cache));

    init(&context->thread_states, get_effective_thread_count(),  memory_context.permenent_allocator);
    
    S32 slot_index = insert(&context->thread_states, platform_get_current_thread_id());
    HE_ASSERT(slot_index != -1);
    Vulkan_Thread_State *main_thread_state = &context->thread_states.values[slot_index];

    VkCommandPoolCreateInfo graphics_command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    graphics_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    graphics_command_pool_create_info.queueFamilyIndex = context->graphics_queue_family_index;
    HE_CHECK_VKRESULT(vkCreateCommandPool(context->logical_device, &graphics_command_pool_create_info, &context->allocation_callbacks, &main_thread_state->graphics_command_pool));

    VkCommandPoolCreateInfo compute_command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    compute_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    compute_command_pool_create_info.queueFamilyIndex = context->compute_queue_family_index;
    HE_CHECK_VKRESULT(vkCreateCommandPool(context->logical_device, &compute_command_pool_create_info, &context->allocation_callbacks, &main_thread_state->compute_command_pool));

    context->graphics_command_pool = main_thread_state->graphics_command_pool;
    context->compute_command_pool = main_thread_state->compute_command_pool;

    VkCommandPoolCreateInfo transfer_command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    transfer_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    transfer_command_pool_create_info.queueFamilyIndex = context->transfer_queue_family_index;
    HE_CHECK_VKRESULT(vkCreateCommandPool(context->logical_device, &transfer_command_pool_create_info, &context->allocation_callbacks, &main_thread_state->transfer_command_pool));

    VkCommandBufferAllocateInfo graphics_command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    graphics_command_buffer_allocate_info.commandPool = main_thread_state->graphics_command_pool;
    graphics_command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    graphics_command_buffer_allocate_info.commandBufferCount = HE_MAX_FRAMES_IN_FLIGHT;
    HE_CHECK_VKRESULT(vkAllocateCommandBuffers(context->logical_device, &graphics_command_buffer_allocate_info, context->graphics_command_buffers));

    VkCommandBufferAllocateInfo compute_command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    compute_command_buffer_allocate_info.commandPool = main_thread_state->compute_command_pool;
    compute_command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    compute_command_buffer_allocate_info.commandBufferCount = HE_MAX_FRAMES_IN_FLIGHT;
    HE_CHECK_VKRESULT(vkAllocateCommandBuffers(context->logical_device, &compute_command_buffer_allocate_info, context->compute_command_buffers));

    context->descriptor_pool_ratios = {{ 
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .ratio = 3.0f },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .ratio = 1.0f },
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .ratio = 4.0f },
    }};

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        context->descriptor_pool_allocators[frame_index] = create_descriptor_pool_allocator(1024);
    }

    context->timeline_value = HE_MAX_FRAMES_IN_FLIGHT;

    VkSemaphoreTypeCreateInfo timeline_semaphore_type_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    timeline_semaphore_type_create_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timeline_semaphore_type_create_info.initialValue = context->timeline_value;
    
    VkSemaphoreCreateInfo timeline_semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    timeline_semaphore_create_info.pNext = &timeline_semaphore_type_create_info;
    timeline_semaphore_create_info.flags = 0;

    HE_CHECK_VKRESULT(vkCreateSemaphore(context->logical_device, &timeline_semaphore_create_info, &context->allocation_callbacks, &context->frame_timeline_semaphore));
    HE_CHECK_VKRESULT(vkCreateSemaphore(context->logical_device, &timeline_semaphore_create_info, &context->allocation_callbacks, &context->compute_timeline_semaphore));

    VkSemaphoreCreateInfo binary_semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    binary_semaphore_create_info.flags = 0;

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        HE_CHECK_VKRESULT(vkCreateSemaphore(context->logical_device, &binary_semaphore_create_info, &context->allocation_callbacks, &context->image_available_semaphores[frame_index]));
        HE_CHECK_VKRESULT(vkCreateSemaphore(context->logical_device, &binary_semaphore_create_info, &context->allocation_callbacks, &context->rendering_finished_semaphores[frame_index]));
    }

    context->vkQueueSubmit2KHR = (PFN_vkQueueSubmit2KHR)vkGetDeviceProcAddr(context->logical_device, "vkQueueSubmit2KHR");
    HE_ASSERT(context->vkQueueSubmit2KHR);
    
    context->vkCmdPipelineBarrier2KHR = (PFN_vkCmdPipelineBarrier2KHR)vkGetDeviceProcAddr(context->logical_device, "vkCmdPipelineBarrier2KHR");
    HE_ASSERT(context->vkCmdPipelineBarrier2KHR);

    return true;
}

void deinit_vulkan(Vulkan_Context *context)
{
    Memory_Context memory_context = grab_memory_context();

    vkDeviceWaitIdle(context->logical_device);
    
    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        Vulkan_Descriptor_Pool_Allocator *descriptor_pool_allocator = &context->descriptor_pool_allocators[frame_index];
        
        for (U32 i = 0; i < descriptor_pool_allocator->ready_pools.count; i++)
        {
            vkDestroyDescriptorPool(context->logical_device, descriptor_pool_allocator->ready_pools[i], &context->allocation_callbacks);
        }

        for (U32 i = 0; i < descriptor_pool_allocator->full_pools.count; i++)
        {
            vkDestroyDescriptorPool(context->logical_device, descriptor_pool_allocator->full_pools[i], &context->allocation_callbacks);
        }

        vulkan_renderer_destroy_resources_at_frame(frame_index);
    }

    vkDestroyDescriptorPool(context->logical_device, context->imgui_descriptor_pool, &context->allocation_callbacks);

    ImGui_ImplVulkan_Shutdown();
    
    vkDestroySemaphore(context->logical_device, context->frame_timeline_semaphore, &context->allocation_callbacks);
    vkDestroySemaphore(context->logical_device, context->compute_timeline_semaphore, &context->allocation_callbacks);
    
    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        vkDestroySemaphore(context->logical_device, context->image_available_semaphores[frame_index], &context->allocation_callbacks);
        vkDestroySemaphore(context->logical_device, context->rendering_finished_semaphores[frame_index], &context->allocation_callbacks);
    }

    for (U32 slot_index = 0; slot_index < context->thread_states.capacity; slot_index++)
    {
        if (context->thread_states.states[slot_index] != Slot_State::OCCUPIED)
        {
            continue;
        }
        Vulkan_Thread_State *thread_state = &context->thread_states.values[slot_index];
        vkDestroyCommandPool(context->logical_device, thread_state->graphics_command_pool, &context->allocation_callbacks);
        vkDestroyCommandPool(context->logical_device, thread_state->transfer_command_pool, &context->allocation_callbacks);
        vkDestroyCommandPool(context->logical_device, thread_state->compute_command_pool, &context->allocation_callbacks);
    }

    destroy_swapchain(context, &context->swapchain);

    U64 pipeline_cache_size = 0;
    vkGetPipelineCacheData(context->logical_device, context->pipeline_cache, &pipeline_cache_size, &context->allocation_callbacks);

    if (pipeline_cache_size)
    {
        U8 *pipeline_cache_data = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, U8, pipeline_cache_size);
        vkGetPipelineCacheData(context->logical_device, context->pipeline_cache, &pipeline_cache_size, pipeline_cache_data);
        
        std::filesystem::path pipeline_cache_file_path(HE_VULKAN_PIPELINE_CACHE_FILE_PATH);
        std::filesystem::create_directories(pipeline_cache_file_path.parent_path()); // todo(amer): to be removed

        write_entire_file(HE_STRING_LITERAL(HE_VULKAN_PIPELINE_CACHE_FILE_PATH), pipeline_cache_data, pipeline_cache_size);
    }

    vkDestroyPipelineCache(context->logical_device, context->pipeline_cache, &context->allocation_callbacks);

    vkDestroySurfaceKHR(context->instance, context->surface, &context->allocation_callbacks);

    vmaDestroyAllocator(context->allocator);
    vkDestroyDevice(context->logical_device, &context->allocation_callbacks);

#if HE_GRAPHICS_DEBUGGING
    auto vkDestroyDebugUtilsMessengerExt = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance, "vkDestroyDebugUtilsMessengerEXT");
    HE_ASSERT(vkDestroyDebugUtilsMessengerExt);
    vkDestroyDebugUtilsMessengerExt(context->instance, context->debug_messenger, &context->allocation_callbacks);
#endif

    vkDestroyInstance(context->instance, &context->allocation_callbacks);
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


void vulkan_renderer_destroy_resources_at_frame(U32 frame_index)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;
    
    Dynamic_Array<Vulkan_Buffer> &buffers = context->pending_delete_buffers[frame_index];

    for (U32 buffer_index = 0; buffer_index < buffers.count; buffer_index++)
    {
        Vulkan_Buffer &buffer = buffers[buffer_index];
        vmaDestroyBuffer(context->allocator, buffer.handle, buffer.allocation);
    }
    
    reset(&buffers);

    Dynamic_Array<Vulkan_Image> &images = context->pending_delete_textures[frame_index];

    for (U32 image_index = 0; image_index < images.count; image_index++)
    {
        Vulkan_Image &image = images[image_index];
        
        vkDestroyImageView(context->logical_device, image.view, &context->allocation_callbacks);
        vmaDestroyImage(context->allocator, image.handle, image.allocation);

        if (image.imgui_handle != VK_NULL_HANDLE)
        {
            ImGui_ImplVulkan_RemoveTexture(image.imgui_handle);
        }
    }

    reset(&images);

    Dynamic_Array<Vulkan_Shader> &shaders = context->pending_delete_shaders[frame_index];
    
    for (U32 shader_index = 0; shader_index < shaders.count; shader_index++)
    {
        Vulkan_Shader *vulkan_shader = &shaders[shader_index];
        destroy_shader(vulkan_shader, context);
    }

    reset(&shaders);

    Dynamic_Array<Vulkan_Pipeline_State> &pipeline_states = context->pending_delete_pipeline_states[frame_index];

    for (U32 pipeline_state_index = 0; pipeline_state_index < pipeline_states.count; pipeline_state_index++)
    {
        Vulkan_Pipeline_State *pipeline_state = &pipeline_states[pipeline_state_index];
        destroy_pipeline(pipeline_state, context);
    }

    reset(&pipeline_states);

    Dynamic_Array<Vulkan_Sampler> &samplers = context->pending_delete_samplers[frame_index];

    for (U32 sampler_index = 0; sampler_index < samplers.count; sampler_index++)
    {
        Vulkan_Sampler &sampler = samplers[sampler_index];
        vkDestroySampler(context->logical_device, sampler.handle, &context->allocation_callbacks);
    }

    reset(&samplers);

    Dynamic_Array<Vulkan_Render_Pass> &render_passes = context->pending_delete_render_passes[frame_index];
    
    for (U32 render_pass_index = 0; render_pass_index < render_passes.count; render_pass_index++)
    {
        Vulkan_Render_Pass &render_pass = render_passes[render_pass_index];
        vkDestroyRenderPass(context->logical_device, render_pass.handle, &context->allocation_callbacks);
    }

    reset(&render_passes);

    Dynamic_Array<Vulkan_Frame_Buffer> &frame_buffers = context->pending_delete_frame_buffers[frame_index];
    
    for (U32 frame_buffer_index = 0; frame_buffer_index < frame_buffers.count; frame_buffer_index++)
    {
        Vulkan_Frame_Buffer &frame_buffer = frame_buffers[frame_buffer_index];
        vkDestroyFramebuffer(context->logical_device, frame_buffer.handle, &context->allocation_callbacks);
    }

    reset(&frame_buffers);
}

void vulkan_renderer_dispatch_compute(U32 group_size_x, U32 group_size_y, U32 group_size_z)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);
    vkCmdDispatch(command_buffer.handle, group_size_x, group_size_y, group_size_z);
}

void vulkan_renderer_on_resize(U32 width, U32 height)
{
    Vulkan_Context *context = &vulkan_context;
    if (width != 0 && height != 0)
    {
        vkDeviceWaitIdle(context->logical_device);
        recreate_swapchain(context, &context->swapchain, width, height, context->swapchain.present_mode);
    }
}

void vulkan_renderer_begin_frame()
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;
    U32 frame_index = renderer_state->current_frame_in_flight_index;

    U64 wait_value = context->timeline_value - (renderer_state->frames_in_flight - 1);
    VkSemaphoreWaitInfo wait_info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    wait_info.semaphoreCount = 1;
    wait_info.pSemaphores = &context->frame_timeline_semaphore;
    wait_info.pValues = &wait_value;
    vkWaitSemaphores(context->logical_device, &wait_info, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(context->logical_device, context->swapchain.handle, UINT64_MAX, context->image_available_semaphores[frame_index], VK_NULL_HANDLE, &context->current_swapchain_image_index);

    VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    command_buffer_begin_info.flags = 0;

    VkCommandBuffer graphics_command_buffer = context->graphics_command_buffers[frame_index];
    vkResetCommandBuffer(graphics_command_buffer, 0);
    vkBeginCommandBuffer(graphics_command_buffer, &command_buffer_begin_info);

    VkCommandBuffer compute_command_buffer = context->compute_command_buffers[frame_index];
    vkResetCommandBuffer(compute_command_buffer, 0);
    vkBeginCommandBuffer(compute_command_buffer, &command_buffer_begin_info);

    context->graphics_command_buffer = graphics_command_buffer;
    context->compute_command_buffer = compute_command_buffer;
    context->command_buffer = graphics_command_buffer;

    vulkan_renderer_destroy_resources_at_frame((frame_index + 1) % renderer_state->frames_in_flight);

    Dynamic_Array< Vulkan_Command_Buffer > &secondary_command_buffers = context->secondary_command_buffers[frame_index];

    for (U32 i = 0; i < secondary_command_buffers.count; i++)
    {
        Vulkan_Command_Buffer command_buffer = secondary_command_buffers[i];
        vkFreeCommandBuffers(context->logical_device, command_buffer.pool, 1, &command_buffer.handle);
    }

    reset(&secondary_command_buffers);

    Vulkan_Descriptor_Pool_Allocator *descriptor_pool_allocator = &context->descriptor_pool_allocators[frame_index];

    // @Bug(amer): we are getting a memory leak here it seems vkResetDescriptorPool doesn't reset descriptor set handles but allocates a new one.
#define VK_RESET_DESCRIPTOR_POOL_BUG 1
#if VK_RESET_DESCRIPTOR_POOL_BUG
    
    for (U32 i = 0; i < descriptor_pool_allocator->ready_pools.count; i++)
    {
        vkDestroyDescriptorPool(context->logical_device, descriptor_pool_allocator->ready_pools[i], &context->allocation_callbacks);
    }

    for (U32 i = 0; i < descriptor_pool_allocator->full_pools.count; i++)
    {
        vkDestroyDescriptorPool(context->logical_device, descriptor_pool_allocator->full_pools[i], &context->allocation_callbacks);
    }

    reset(&descriptor_pool_allocator->ready_pools);
    reset(&descriptor_pool_allocator->full_pools);

    VkDescriptorPool descriptor_pool = create_descriptor_bool(descriptor_pool_allocator->set_count_per_pool);
    append(&descriptor_pool_allocator->ready_pools, descriptor_pool);

#else
    
    for (U32 i = 0; i < descriptor_pool_allocator->ready_pools.count; i++)
    {
        VkResult result = vkResetDescriptorPool(context->logical_device, descriptor_pool_allocator->ready_pools[i], 0);
        HE_ASSERT(result == VK_SUCCESS);
    }

    for (U32 i = 0; i < descriptor_pool_allocator->full_pools.count; i++)
    {
        VkResult result = vkResetDescriptorPool(context->logical_device, descriptor_pool_allocator->full_pools[i], 0);
        HE_ASSERT(result == VK_SUCCESS);

        append(&descriptor_pool_allocator->ready_pools, descriptor_pool_allocator->full_pools[i]);
    }
    
    reset(&descriptor_pool_allocator->full_pools);

#endif
}

static void internal_set_viewport(VkCommandBuffer command_buffer, U32 width, U32 height)
{
    Vulkan_Context *context = &vulkan_context;

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (F32)width;
    viewport.height = (F32)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = width;
    scissor.extent.height = height;
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
}

void vulkan_renderer_set_viewport(U32 width, U32 height)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);
    internal_set_viewport(command_buffer.handle, width, height);
}

static void internal_set_vertex_buffers(VkCommandBuffer command_buffer, const Array_View< Buffer_Handle >& vertex_buffer_handles, const Array_View< U64 >& offsets)
{
    HE_ASSERT(vertex_buffer_handles.count == offsets.count);
    
    Memory_Context memory_context = grab_memory_context();

    Vulkan_Context *context = &vulkan_context;

    U32 current_frame_in_flight_index = context->renderer_state->current_frame_in_flight_index;

    VkBuffer* vulkan_vertex_buffers = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkBuffer, offsets.count);
    for (U32 vertex_buffer_index = 0; vertex_buffer_index < offsets.count; vertex_buffer_index++)
    {
        Buffer_Handle vertex_buffer_handle = vertex_buffer_handles[vertex_buffer_index];
        Vulkan_Buffer *vulkan_vertex_buffer = &context->buffers[vertex_buffer_handle.index];
        vulkan_vertex_buffers[vertex_buffer_index] = vulkan_vertex_buffer->handle;
    }

    vkCmdBindVertexBuffers(command_buffer, 0, offsets.count, vulkan_vertex_buffers, offsets.data);
}

void vulkan_renderer_set_vertex_buffers(const Array_View< Buffer_Handle > &vertex_buffer_handles, const Array_View< U64 > &offsets)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);
    internal_set_vertex_buffers(command_buffer.handle, vertex_buffer_handles, offsets);
}

static void internal_set_index_buffer(VkCommandBuffer command_buffer, Buffer_Handle index_buffer_handle, U64 offset)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Buffer *vulkan_index_buffer = &context->buffers[index_buffer_handle.index];
    vkCmdBindIndexBuffer(command_buffer, vulkan_index_buffer->handle, offset, VK_INDEX_TYPE_UINT16);
}

void vulkan_renderer_set_index_buffer(Buffer_Handle index_buffer_handle, U64 offset)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);
    internal_set_index_buffer(command_buffer.handle, index_buffer_handle, offset);
}

static void internal_set_pipeline_state(VkCommandBuffer command_buffer, Pipeline_State_Handle pipeline_state_handle, VkPipelineBindPoint bind_point)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    Vulkan_Pipeline_State *vulkan_pipeline_state = &context->pipeline_states[pipeline_state_handle.index];
    vkCmdBindPipeline(command_buffer, bind_point, vulkan_pipeline_state->handle);
}

void vulkan_renderer_set_pipeline_state(Pipeline_State_Handle pipeline_state_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);
    VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    if (command_buffer.usage == Command_Buffer_Usage::COMPUTE)
    {
        bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    }
    internal_set_pipeline_state(command_buffer.handle, pipeline_state_handle, bind_point);
}

static void internal_draw_sub_mesh(VkCommandBuffer command_buffer, Static_Mesh_Handle static_mesh_handle, U32 first_instance, U32 sub_mesh_index)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;
    Static_Mesh *static_mesh = get(&renderer_state->static_meshes, static_mesh_handle);

    Sub_Mesh *sub_mesh = &static_mesh->sub_meshes[sub_mesh_index];

    U32 instance_count = 1;
    S32 first_vertex = sub_mesh->vertex_offset;
    U32 first_index = sub_mesh->index_offset;
    vkCmdDrawIndexed(command_buffer, sub_mesh->index_count, instance_count, first_index, first_vertex, first_instance);
}

void vulkan_renderer_draw_sub_mesh(Static_Mesh_Handle static_mesh_handle, U32 first_instance, U32 sub_mesh_index)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);
    internal_draw_sub_mesh(command_buffer.handle, static_mesh_handle, first_instance, sub_mesh_index);
}

static void internal_draw_fullscreen_triangle(VkCommandBuffer command_buffer)
{
    Vulkan_Context *context = &vulkan_context;
    vkCmdDraw(command_buffer, 3, 1, 0, 0);
}

void vulkan_renderer_draw_fullscreen_triangle()
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);
    internal_draw_fullscreen_triangle(command_buffer.handle);
}

void vulkan_renderer_fill_buffer(Buffer_Handle buffer_handle, U32 value)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);

    Vulkan_Buffer *vulkan_buffer = &context->buffers[buffer_handle.index];
    vkCmdFillBuffer(command_buffer.handle, vulkan_buffer->handle, 0, VK_WHOLE_SIZE, value);

    VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    barrier.buffer = vulkan_buffer->handle;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(command_buffer.handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void vulkan_renderer_clear_texture(Texture_Handle texture_handle, Clear_Value clear_value)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);

    Texture *texture = renderer_get_texture(texture_handle);
    Vulkan_Image *vulkan_image = &context->textures[texture_handle.index];

    VkImageSubresourceRange sub_resource_range = {};
    sub_resource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    sub_resource_range.baseMipLevel = 0;
    sub_resource_range.levelCount = texture->mip_levels;
    sub_resource_range.baseArrayLayer = 0;
    sub_resource_range.layerCount = texture->layer_count;

    VkImageLayout image_layout = get_image_layout(texture->state, texture->format);

    if (is_color_format(texture->format))
    {
        VkClearColorValue color_clear_value = {};

        if (is_color_format_int(texture->format))
        {
            for (U32 i = 0; i < 4; i++)
            {
                color_clear_value.int32[i] = clear_value.icolor[i];
            }
        }
        else if (is_color_format_uint(texture->format))
        {
            for (U32 i = 0; i < 4; i++)
            {
                color_clear_value.uint32[i] = clear_value.ucolor[i];
            }
        }
        else
        {
            for (U32 i = 0; i < 4; i++)
            {
                color_clear_value.float32[i] = clear_value.color[i];
            }
        }

        vkCmdClearColorImage(command_buffer.handle, vulkan_image->handle, image_layout, &color_clear_value, 1, &sub_resource_range);
    }
    else
    {
        VkClearDepthStencilValue depth_stencil_clear_value = {};
        depth_stencil_clear_value.depth = clear_value.depth;
        depth_stencil_clear_value.stencil = clear_value.stencil;
        vkCmdClearDepthStencilImage(command_buffer.handle, vulkan_image->handle, image_layout, &depth_stencil_clear_value, 1, &sub_resource_range);
    }

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = vulkan_image->handle;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = image_layout;
    barrier.newLayout = image_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange = sub_resource_range;
    VkPipelineStageFlags stage_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (command_buffer.usage == Command_Buffer_Usage::GRAPHICS)
    {
        stage_flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(command_buffer.handle, VK_PIPELINE_STAGE_TRANSFER_BIT, stage_flags, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void vulkan_renderer_change_texture_state(Texture_Handle texture_handle, Resource_State new_resource_state)
{
    Vulkan_Context *context = &vulkan_context;

    Texture *texture = renderer_get_texture(texture_handle);

    Vulkan_Image *vulkan_image = &context->textures[texture_handle.index];

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = vulkan_image->handle;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.subresourceRange.aspectMask = is_color_format(texture->format) ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = texture->mip_levels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = texture->layer_count;

    Resource_State old_resource_state = texture->state;

    barrier.oldLayout = get_image_layout(old_resource_state, texture->format);
    barrier.newLayout = get_image_layout(new_resource_state, texture->format);

    barrier.srcAccessMask = get_access_flags(old_resource_state, texture->format);
    barrier.dstAccessMask = get_access_flags(new_resource_state, texture->format);

    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);

    bool compute_only = command_buffer.usage == Command_Buffer_Usage::COMPUTE;
    VkPipelineStageFlags src_stage = get_pipeline_stage_flags(barrier.srcAccessMask, compute_only);
    VkPipelineStageFlags dst_stage = get_pipeline_stage_flags(barrier.dstAccessMask, compute_only);
    vkCmdPipelineBarrier(command_buffer.handle, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    texture->state = new_resource_state;
}

void vulkan_renderer_invalidate_buffer(Buffer_Handle buffer_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Buffer *vulkan_buffer = &context->buffers[buffer_handle.index];

    VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    barrier.buffer = vulkan_buffer->handle;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;

    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);

    VkPipelineStageFlags stage_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (command_buffer.usage == Command_Buffer_Usage::GRAPHICS)
    {
        stage_flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(command_buffer.handle, stage_flags, stage_flags, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void vulkan_renderer_begin_compute_pass()
{
    Vulkan_Context *context = &vulkan_context;
    U32 frame_index = context->renderer_state->current_frame_in_flight_index;
    context->command_buffer = context->compute_command_buffers[frame_index];
}

void vulkan_renderer_end_compute_pass()
{
    Vulkan_Context *context = &vulkan_context;
    U32 frame_index = context->renderer_state->current_frame_in_flight_index;
    context->command_buffer = context->graphics_command_buffers[frame_index];
}

void vulkan_renderer_end_frame()
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;
    
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
    
    transtion_image_to_layout(context->graphics_command_buffer, swapchain_image, 0, 1, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    Texture_Handle presentable_attachment = get_presentable_attachment(&renderer_state->render_graph, renderer_state);
    Vulkan_Image *vulkan_presentable_attachment = &context->textures[presentable_attachment.index];
    
    transtion_image_to_layout(context->graphics_command_buffer, vulkan_presentable_attachment->handle, 0, 1, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    vkCmdCopyImage(context->graphics_command_buffer, vulkan_presentable_attachment->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    transtion_image_to_layout(context->graphics_command_buffer, swapchain_image, 0, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    transtion_image_to_layout(context->graphics_command_buffer, vulkan_presentable_attachment->handle, 0, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    Texture_Handle scene_texture = get_texture_resource(&renderer_state->render_graph, renderer_state, HE_STRING_LITERAL("scene"));

    Vulkan_Image *scene_image = &context->textures[scene_texture.index];
    transtion_image_to_layout(context->graphics_command_buffer, scene_image->handle, 0, 1, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    Buffer_Handle scene_buffer = renderer_state->render_data.scene_buffers[renderer_state->current_frame_in_flight_index];
    Vulkan_Buffer *vulkan_scene_buffer = &context->buffers[scene_buffer.index];

    U32 x = glm::clamp((U32)renderer_state->engine->input.mouse_x, 0u, renderer_state->back_buffer_width - 1);
    U32 y = glm::clamp((U32)renderer_state->engine->input.mouse_y, 0u, renderer_state->back_buffer_height - 1);

    VkBufferImageCopy buffer_image_copy =
    {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageOffset = { (S32)x, (S32)y, 0 },
        .imageExtent = { 1, 1, 1 }
    };

    vkCmdCopyImageToBuffer(context->graphics_command_buffer, scene_image->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vulkan_scene_buffer->handle, 1, &buffer_image_copy);
    transtion_image_to_layout(context->graphics_command_buffer, scene_image->handle, 0, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    vkEndCommandBuffer(context->graphics_command_buffer);

    vkEndCommandBuffer(context->compute_command_buffer);

    platform_lock_mutex(&renderer_state->render_commands_mutex);

    {
        // VkSemaphoreSubmitInfoKHR wait_semaphore_infos[] =
        // {
        //     {
        //         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
        //         .semaphore = context->compute_timeline_semaphore,
        //         .value = context->timeline_value,
        //         .stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
        //     }
        // };

        VkSemaphoreSubmitInfoKHR signal_semaphore_infos[] =
        {
            {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
                .semaphore = context->compute_timeline_semaphore,
                .value = context->timeline_value + 1,
                .stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            },
        };

        VkCommandBufferSubmitInfoKHR command_buffer_submit_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR };
        command_buffer_submit_info.commandBuffer = context->compute_command_buffer;

        VkSubmitInfo2KHR submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
        // submit_info.waitSemaphoreInfoCount = HE_ARRAYCOUNT(wait_semaphore_infos);
        // submit_info.pWaitSemaphoreInfos = wait_semaphore_infos;

        submit_info.signalSemaphoreInfoCount = HE_ARRAYCOUNT(signal_semaphore_infos);
        submit_info.pSignalSemaphoreInfos = signal_semaphore_infos;
        submit_info.commandBufferInfoCount = 1;
        submit_info.pCommandBufferInfos = &command_buffer_submit_info;

        context->vkQueueSubmit2KHR(context->compute_queue, 1, &submit_info, VK_NULL_HANDLE);
    }

    {
        VkSemaphoreSubmitInfoKHR wait_semaphore_infos[] =
        {
            {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
                .semaphore = context->image_available_semaphores[renderer_state->current_frame_in_flight_index],
                .value = 0,
                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR
            },
            {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
                .semaphore = context->compute_timeline_semaphore,
                .value = context->timeline_value + 1,
                .stageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT
            },
            {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
                .semaphore = context->frame_timeline_semaphore,
                .value = context->timeline_value - (renderer_state->frames_in_flight - 1),
                .stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR
            }
        };

        VkSemaphoreSubmitInfoKHR signal_semaphore_infos[] =
        {
            {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
                .semaphore = context->rendering_finished_semaphores[renderer_state->current_frame_in_flight_index],
                .value = 0,
                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
            },
            {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
                .semaphore = context->frame_timeline_semaphore,
                .value = context->timeline_value + 1,
                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
            },
        };

        VkCommandBufferSubmitInfoKHR command_buffer_submit_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR };
        command_buffer_submit_info.commandBuffer = context->graphics_command_buffer;

        VkSubmitInfo2KHR submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
        submit_info.waitSemaphoreInfoCount = HE_ARRAYCOUNT(wait_semaphore_infos);
        submit_info.pWaitSemaphoreInfos = wait_semaphore_infos;
        submit_info.signalSemaphoreInfoCount = HE_ARRAYCOUNT(signal_semaphore_infos);
        submit_info.pSignalSemaphoreInfos = signal_semaphore_infos;
        submit_info.commandBufferInfoCount = 1;
        submit_info.pCommandBufferInfos = &command_buffer_submit_info;

        context->vkQueueSubmit2KHR(context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    }

    platform_unlock_mutex(&renderer_state->render_commands_mutex);

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
            vkDeviceWaitIdle(context->logical_device);
            recreate_swapchain(context, &context->swapchain, renderer_state->back_buffer_width, renderer_state->back_buffer_height, context->swapchain.present_mode);
        }
    }
    else
    {
        HE_ASSERT(result == VK_SUCCESS);
    }

    context->timeline_value++;
}


void vulkan_renderer_begin_command_list(const Command_List_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer frame_buffer = VK_NULL_HANDLE;
    if (is_valid_handle(&renderer_state->render_passes, descriptor.render_pass))
    {
        Vulkan_Render_Pass *vulkan_render_pass = &context->render_passes[descriptor.render_pass.index];
        render_pass = vulkan_render_pass->handle;
    }

    if (is_valid_handle(&renderer_state->frame_buffers, descriptor.frame_buffer))
    {
        Vulkan_Frame_Buffer *vulkan_frame_buffer = &context->frame_buffers[descriptor.frame_buffer.index];
        frame_buffer = vulkan_frame_buffer->handle;
    }

    push_command_buffer(descriptor.usage, descriptor.submit, context, render_pass, frame_buffer);
}

Command_List vulkan_renderer_end_command_list(Upload_Request_Handle upload_request_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer command_buffer = pop_command_buffer(context, upload_request_handle);
    return { .handle = command_buffer.handle };
}

void vulkan_renderer_execute_command_list(Command_List command_list)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer vulkan_command_buffer = get_commnad_buffer(context);
    VkCommandBuffer command_buffer = (VkCommandBuffer)command_list.handle;
    vkCmdExecuteCommands(vulkan_command_buffer.handle, 1, &command_buffer);
}

static void internal_create_texture(const Texture_Descriptor &descriptor, Vulkan_Image *image, Upload_Request_Handle upload_request_handle = Resource_Pool<Upload_Request>::invalid_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_NONE;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM;

    if (is_color_format(descriptor.format))
    {
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        if (descriptor.is_attachment)
        {
            usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        else if (descriptor.is_storage)
        {
            usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_STORAGE_BIT;
        }
        else
        {
            usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
    }
    else
    {
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;

        if (descriptor.is_attachment)
        {
            usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        else
        {
            usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
    }

    VkFormat format = get_texture_format(descriptor.format);
    VkSampleCountFlagBits sample_count = get_sample_count(descriptor.sample_count);

    U32 mip_levels = 1;

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
    image_create_info.arrayLayers = descriptor.layer_count;
    image_create_info.format = format;
    image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create_info.usage = usage;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_create_info.samples = get_sample_count(descriptor.sample_count);
    image_create_info.flags = descriptor.is_cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;

    VmaAllocationCreateInfo allocation_create_info = {};
    allocation_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocationInfo allocation_info = {};
    vmaCreateImage(context->allocator, &image_create_info, &allocation_create_info, &image->handle, &image->allocation, &image->allocation_info);

    VkImageViewCreateInfo image_view_create_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    image_view_create_info.image = image->handle;
    image_view_create_info.viewType = descriptor.is_cubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    image_view_create_info.format = format;
    image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_create_info.subresourceRange.aspectMask = aspect;
    image_view_create_info.subresourceRange.baseMipLevel = 0;
    image_view_create_info.subresourceRange.levelCount = mip_levels;
    image_view_create_info.subresourceRange.baseArrayLayer = 0;
    image_view_create_info.subresourceRange.layerCount = descriptor.layer_count;
    HE_CHECK_VKRESULT(vkCreateImageView(context->logical_device, &image_view_create_info, &context->allocation_callbacks, &image->view));

    if (descriptor.data_array.count > 0)
    {
        Vulkan_Command_Buffer command_buffer = push_command_buffer(Command_Buffer_Usage::GRAPHICS, true, context);
        copy_data_to_image(context, &command_buffer, image, descriptor, mip_levels);
        pop_command_buffer(context, upload_request_handle);
    }
    else if (descriptor.is_storage)
    {
        Vulkan_Command_Buffer command_buffer = push_command_buffer(Command_Buffer_Usage::GRAPHICS, true, context);
        transtion_image_to_layout(command_buffer.handle, image->handle, 0, mip_levels, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        pop_command_buffer(context);
    }
}

bool vulkan_renderer_create_texture(Texture_Handle texture_handle, const Texture_Descriptor &descriptor, Upload_Request_Handle upload_request_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    Texture *texture = get(&renderer_state->textures, texture_handle);
    Vulkan_Image *image = &context->textures[texture_handle.index];
    image->imgui_handle = VK_NULL_HANDLE;

    internal_create_texture(descriptor, image, upload_request_handle);

    texture->size = image->allocation_info.size;
    texture->alignment = image->allocation->GetAlignment();
    texture->layer_count = descriptor.layer_count;

    U32 mip_levels = 1;

    if (descriptor.mipmapping)
    {
        mip_levels = (U32)glm::floor(glm::log2((F32)glm::max(descriptor.width, descriptor.height)));
    }

    texture->mip_levels = mip_levels;
    return true;
}

void vulkan_renderer_imgui_add_texture(Texture_Handle texture)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Image *image = &context->textures[texture.index];

    Vulkan_Sampler *sampler = &context->samplers[context->renderer_state->default_texture_sampler.index];
    image->imgui_handle = ImGui_ImplVulkan_AddTexture(sampler->handle, image->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

ImTextureID vulkan_renderer_imgui_get_texture_id(Texture_Handle texture)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Image *image = &context->textures[texture.index];
    return (ImTextureID)image->imgui_handle;
}

void vulkan_renderer_destroy_texture(Texture_Handle texture_handle, bool immediate)
{
    Vulkan_Context *context = &vulkan_context;

    Vulkan_Image &image = context->textures[texture_handle.index];
    if (immediate)
    {
        vkDestroyImageView(context->logical_device, image.view, &context->allocation_callbacks);
        vmaDestroyImage(context->allocator, image.handle, image.allocation);

        if (image.imgui_handle != VK_NULL_HANDLE)
        {
            ImGui_ImplVulkan_RemoveTexture(image.imgui_handle);
        }
    }
    else
    {
        U32 frame_index = context->renderer_state->current_frame_in_flight_index;
        append(&context->pending_delete_textures[frame_index], image);
    }
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

    HE_CHECK_VKRESULT(vkCreateSampler(context->logical_device, &sampler_create_info, &context->allocation_callbacks, &vulkan_sampler->handle));
    sampler->descriptor = descriptor;
    return true;
}

void vulkan_renderer_destroy_sampler(Sampler_Handle sampler_handle, bool immediate)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Sampler &sampler = context->samplers[sampler_handle.index];
    if (immediate)
    {
        vkDestroySampler(context->logical_device, sampler.handle, &context->allocation_callbacks);
    }
    else
    {
        U32 frame_index = context->renderer_state->current_frame_in_flight_index;
        append(&context->pending_delete_samplers[frame_index], sampler);
    }
}

bool vulkan_renderer_create_shader(Shader_Handle shader_handle, const Shader_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    bool loaded = create_shader(shader_handle, descriptor, context);
    return loaded;
}

void vulkan_renderer_destroy_shader(Shader_Handle shader_handle, bool immediate)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Shader &shader = context->shaders[shader_handle.index];
    if (immediate)
    {
        destroy_shader(&shader, context);
    }
    else
    {
        U32 frame_index = context->renderer_state->current_frame_in_flight_index;
        append(&context->pending_delete_shaders[frame_index], shader);
    }
}

bool vulkan_renderer_create_pipeline_state(Pipeline_State_Handle pipeline_state_handle, const Pipeline_State_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Shader *shader = renderer_get_shader(descriptor.shader);
    if (shader->type == Shader_Type::COMPUTE)
    {
        return create_compute_pipeline(pipeline_state_handle, descriptor, context);
    }

    return create_graphics_pipeline(pipeline_state_handle, descriptor, context);
}

void vulkan_renderer_destroy_pipeline_state(Pipeline_State_Handle pipeline_state_handle, bool immediate)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Pipeline_State &pipeline_state = context->pipeline_states[pipeline_state_handle.index];
    if (immediate)
    {
        destroy_pipeline(&pipeline_state, context);
    }
    else
    {
        U32 frame_index = context->renderer_state->current_frame_in_flight_index;
        append(&context->pending_delete_pipeline_states[frame_index], pipeline_state);
    }
}

static VkDescriptorType get_descriptor_type(Buffer_Usage usage)
{
    switch (usage)
    {
        case Buffer_Usage::UNIFORM: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

        case Buffer_Usage::STORAGE_CPU_SIDE:
        case Buffer_Usage::STORAGE_GPU_SIDE:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

        default:
        {
            HE_ASSERT(!"unsupported binding type");
        } break;
    }

    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

static VkDescriptorPool get_descriptor_pool(Vulkan_Descriptor_Pool_Allocator *allocator)
{
    Vulkan_Context *context = &vulkan_context;

    if (allocator->ready_pools.count)
    {
        VkDescriptorPool descriptor_pool = back(&allocator->ready_pools);
        remove_back(&allocator->ready_pools);
        return descriptor_pool;
    }

    VkDescriptorPool descriptor_pool = create_descriptor_bool(allocator->set_count_per_pool);
    allocator->set_count_per_pool = (U32)(allocator->set_count_per_pool * 1.5f);
    return descriptor_pool;
}

static VkDescriptorSet allocate_descriptor_set(Vulkan_Descriptor_Pool_Allocator *allocator, VkDescriptorSetLayout layout)
{
    Vulkan_Context *context = &vulkan_context;
    U32 frame_index = context->renderer_state->current_frame_in_flight_index;
    Vulkan_Descriptor_Pool_Allocator *descriptor_pool_allocator = &context->descriptor_pool_allocators[frame_index];
    VkDescriptorPool descriptor_pool = get_descriptor_pool(descriptor_pool_allocator);

    VkDescriptorSetAllocateInfo descriptor_set_allocation_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    descriptor_set_allocation_info.descriptorPool = descriptor_pool;
    descriptor_set_allocation_info.descriptorSetCount = 1;
    descriptor_set_allocation_info.pSetLayouts = &layout;

    VkDescriptorSet descriptor_set;
    VkResult result = vkAllocateDescriptorSets(context->logical_device, &descriptor_set_allocation_info, &descriptor_set);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
    {
        append(&descriptor_pool_allocator->full_pools, descriptor_pool);
        descriptor_pool = get_descriptor_pool(descriptor_pool_allocator);
        descriptor_set_allocation_info.descriptorPool = descriptor_pool;
        HE_CHECK_VKRESULT(vkAllocateDescriptorSets(context->logical_device, &descriptor_set_allocation_info, &descriptor_set));
    }

    append(&descriptor_pool_allocator->ready_pools, descriptor_pool);
    return descriptor_set;
}

void internal_update_bind_group(Vulkan_Descriptor_Pool_Allocator *allocator, Bind_Group_Handle bind_group_handle, const Array_View< Update_Binding_Descriptor > &update_binding_descriptors)
{
    Vulkan_Context *context = &vulkan_context;
    U32 frame_index = context->renderer_state->current_frame_in_flight_index;

    Memory_Context memory_context = grab_memory_context();

    Bind_Group *bind_group = renderer_get_bind_group(bind_group_handle);
    Vulkan_Bind_Group *vulkan_bind_group = &context->bind_groups[bind_group_handle.index];
    Vulkan_Shader *vulkan_shader = &context->shaders[bind_group->shader.index];

    vulkan_bind_group->handle = allocate_descriptor_set(allocator, vulkan_shader->descriptor_set_layouts[bind_group->group_index]);
    HE_ASSERT(vulkan_bind_group->handle != VK_NULL_HANDLE);

    VkWriteDescriptorSet *write_descriptor_sets = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkWriteDescriptorSet, update_binding_descriptors.count);

    for (U32 binding_index = 0; binding_index < update_binding_descriptors.count; binding_index++)
    {
        const Update_Binding_Descriptor *binding = &update_binding_descriptors[binding_index];

        VkWriteDescriptorSet *write_descriptor_set = &write_descriptor_sets[binding_index];
        *write_descriptor_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

        write_descriptor_set->dstSet = vulkan_bind_group->handle;
        write_descriptor_set->dstBinding = binding->binding_number;
        write_descriptor_set->dstArrayElement = binding->element_index;
        write_descriptor_set->descriptorCount = binding->count;

        if (binding->buffers)
        {
            {
                Buffer *buffer = get(&context->renderer_state->buffers, binding->buffers[0]);
                write_descriptor_set->descriptorType = get_descriptor_type(buffer->usage);
            }

            VkDescriptorBufferInfo *buffer_infos = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkDescriptorBufferInfo, binding->count);

            for (U32 buffer_index = 0; buffer_index < binding->count; buffer_index++)
            {
                Buffer *buffer = get(&context->renderer_state->buffers, binding->buffers[buffer_index]);
                Vulkan_Buffer *vulkan_buffer = &context->buffers[ binding->buffers[buffer_index].index ];

                VkDescriptorBufferInfo *buffer_info = &buffer_infos[buffer_index];
                buffer_info->buffer = vulkan_buffer->handle;
                buffer_info->offset = 0;
                buffer_info->range = buffer->size;
            }

            write_descriptor_set->pBufferInfo = buffer_infos;
        }

        if (binding->textures)
        {
            VkDescriptorImageInfo *image_infos = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkDescriptorImageInfo, binding->count);

            {
                Texture_Handle texture_handle = binding->textures[0];
                Texture *texture = renderer_get_texture(texture_handle);
                if (texture->is_storage)
                {
                    write_descriptor_set->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                }
                else
                {
                    write_descriptor_set->descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                }
            }

            for (U32 image_index = 0; image_index < binding->count; image_index++)
            {
                Texture *texture = renderer_get_texture(binding->textures[image_index]);
                Vulkan_Image *vulkan_image = &context->textures[ binding->textures[image_index].index ];

                VkDescriptorImageInfo *image_info = &image_infos[image_index];
                *image_info = {};

                image_info->imageLayout = texture->is_storage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                image_info->imageView = vulkan_image->view;

                if (binding->samplers)
                {
                    Vulkan_Sampler *vulkan_sampler = &context->samplers[ binding->samplers[image_index].index ];
                    image_info->sampler = vulkan_sampler->handle;
                }
            }

            write_descriptor_set->pImageInfo = image_infos;
        }
    }

    vkUpdateDescriptorSets(context->logical_device, update_binding_descriptors.count, write_descriptor_sets, 0, nullptr);
}

void vulkan_renderer_update_bind_group(Bind_Group_Handle bind_group_handle, const Array_View< Update_Binding_Descriptor > &update_binding_descriptors)
{
    Vulkan_Context *context = &vulkan_context;
    U32 frame_index = context->renderer_state->current_frame_in_flight_index;
    internal_update_bind_group(&context->descriptor_pool_allocators[frame_index], bind_group_handle, update_binding_descriptors);
}

static void internal_set_bind_groups(VkCommandBuffer command_buffer, U32 first_bind_group, const Array_View< Bind_Group_Handle > &bind_group_handles, VkPipelineBindPoint bind_point)
{
    Vulkan_Context *context = &vulkan_context;
    Memory_Context memory_context = grab_memory_context();

    Bind_Group *bind_group = get(&context->renderer_state->bind_groups, bind_group_handles[0]);
    Vulkan_Shader *vulkan_shader = &context->shaders[bind_group->shader.index];

    VkDescriptorSet *descriptor_sets = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkDescriptorSet, bind_group_handles.count);

    for (U32 bind_group_index = 0; bind_group_index < bind_group_handles.count; bind_group_index++)
    {
        Vulkan_Bind_Group *vulkan_bind_group = &context->bind_groups[ bind_group_handles[ bind_group_index ].index ];
        descriptor_sets[bind_group_index] = vulkan_bind_group->handle;
    }

    vkCmdBindDescriptorSets(command_buffer, bind_point, vulkan_shader->pipeline_layout, first_bind_group, bind_group_handles.count, descriptor_sets, 0, nullptr);
}

void vulkan_renderer_set_bind_groups(U32 first_bind_group, const Array_View< Bind_Group_Handle > &bind_group_handles)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);
    VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    if (command_buffer.usage == Command_Buffer_Usage::COMPUTE)
    {
        bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    }
    internal_set_bind_groups(command_buffer.handle, first_bind_group, bind_group_handles, bind_point);
}

bool vulkan_renderer_create_render_pass(Render_Pass_Handle render_pass_handle, const Render_Pass_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Memory_Context memory_context = grab_memory_context();

    Render_Pass *render_pass = get(&context->renderer_state->render_passes, render_pass_handle);
    Vulkan_Render_Pass *vulkan_render_pass = &context->render_passes[render_pass_handle.index];

    if (descriptor.color_attachments.count)
    {
        copy(&render_pass->color_attachments, &descriptor.color_attachments);
    }
    else
    {
        reset(&render_pass->color_attachments);
    }

    if (descriptor.depth_stencil_attachments.count)
    {
        copy(&render_pass->depth_stencil_attachments, &descriptor.depth_stencil_attachments);
    }
    else
    {
        reset(&render_pass->depth_stencil_attachments);
    }

    U32 attachment_count = descriptor.color_attachments.count + descriptor.depth_stencil_attachments.count;
    VkAttachmentDescription *attachments = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkAttachmentDescription, attachment_count);
    VkAttachmentReference *attachment_refs = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkAttachmentReference, attachment_count);
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

    for (const Attachment_Info &attachment_info : descriptor.depth_stencil_attachments)
    {
        VkAttachmentDescription *attachment = &attachments[attachment_index];
        attachment->format = get_texture_format(attachment_info.format);
        attachment->samples = get_sample_count(attachment_info.sample_count);

        attachment->storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment->stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;

        switch (attachment_info.operation)
        {
            case Attachment_Operation::DONT_CARE:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            } break;

            case Attachment_Operation::LOAD:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                attachment->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                attachment->initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            } break;

            case Attachment_Operation::CLEAR:
            {
                attachment->loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachment->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachment->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            } break;
        }

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

    if (descriptor.depth_stencil_attachments.count)
    {
        subpass.pDepthStencilAttachment = &attachment_refs[descriptor.color_attachments.count];
    }

    VkRenderPassCreateInfo render_pass_create_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    render_pass_create_info.attachmentCount = attachment_count;
    render_pass_create_info.pAttachments = attachments;

    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;

    HE_CHECK_VKRESULT(vkCreateRenderPass(context->logical_device, &render_pass_create_info, &context->allocation_callbacks, &vulkan_render_pass->handle));
    return true;
}

void vulkan_renderer_begin_render_pass(Render_Pass_Handle render_pass_handle, Frame_Buffer_Handle frame_buffer_handle, const Array_View< Clear_Value > &clear_values)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    Memory_Context memory_context = grab_memory_context();

    Frame_Buffer *frame_buffer = get(&renderer_state->frame_buffers, frame_buffer_handle);
    Vulkan_Render_Pass *vulkan_render_pass = &context->render_passes[render_pass_handle.index];
    Vulkan_Frame_Buffer *vulkan_frame_buffer = &context->frame_buffers[frame_buffer_handle.index];
    HE_ASSERT(frame_buffer->attachments.count <= clear_values.count);

    Texture *attachment = get(&renderer_state->textures, frame_buffer->attachments[0]);

    VkClearValue *vulkan_clear_values = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkClearValue, clear_values.count);

    for (U32 clear_value_index = 0; clear_value_index < clear_values.count; clear_value_index++)
    {
        Texture_Handle texture_handle = frame_buffer->attachments[clear_value_index];
        Texture *texture = get(&renderer_state->textures, texture_handle);

        if (is_color_format(texture->format))
        {
            if (is_color_format_int(texture->format))
            {
                for (U32 i = 0; i < 4; i++)
                {
                    vulkan_clear_values[clear_value_index].color.int32[i] = clear_values[clear_value_index].icolor[i];
                }
            }
            else if (is_color_format_uint(texture->format))
            {
                for (U32 i = 0; i < 4; i++)
                {
                    vulkan_clear_values[clear_value_index].color.uint32[i] = clear_values[clear_value_index].ucolor[i];
                }
            }
            else
            {
                for (U32 i = 0; i < 4; i++)
                {
                    vulkan_clear_values[clear_value_index].color.float32[i] = clear_values[clear_value_index].color[i];
                }
            }
        }
        else
        {
            vulkan_clear_values[clear_value_index].depthStencil = { clear_values[clear_value_index].depth, clear_values[clear_value_index].stencil };
        }
    }

    VkRenderPassBeginInfo render_pass_begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    render_pass_begin_info.renderPass = vulkan_render_pass->handle;
    render_pass_begin_info.framebuffer = vulkan_frame_buffer->handle;
    render_pass_begin_info.renderArea.offset = { 0, 0 };
    render_pass_begin_info.renderArea.extent = { context->swapchain.width, context->swapchain.height };
    render_pass_begin_info.clearValueCount = clear_values.count;
    render_pass_begin_info.pClearValues = vulkan_clear_values;

    // Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);
    vkCmdBeginRenderPass(context->graphics_command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
}

void vulkan_renderer_end_render_pass(Render_Pass_Handle render_pass_handle)
{
    Vulkan_Context *context = &vulkan_context;
    // Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);
    vkCmdEndRenderPass(context->graphics_command_buffer);
}

void vulkan_renderer_destroy_render_pass(Render_Pass_Handle render_pass_handle, bool immediate)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Render_Pass &render_pass = context->render_passes[render_pass_handle.index];
    if (immediate)
    {
        vkDestroyRenderPass(context->logical_device, render_pass.handle, &context->allocation_callbacks);
    }
    else
    {
        U32 frame_index = context->renderer_state->current_frame_in_flight_index;
        append(&context->pending_delete_render_passes[frame_index], render_pass);
    }
}

bool vulkan_renderer_create_frame_buffer(Frame_Buffer_Handle frame_buffer_handle, const Frame_Buffer_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Memory_Context memory_context = grab_memory_context();

    Frame_Buffer *frame_buffer = get(&context->renderer_state->frame_buffers, frame_buffer_handle);
    copy(&frame_buffer->attachments, &descriptor.attachments);
    frame_buffer->width = descriptor.width;
    frame_buffer->height = descriptor.height;
    frame_buffer->render_pass = descriptor.render_pass;

    Vulkan_Frame_Buffer *vulkan_frame_buffer = &context->frame_buffers[frame_buffer_handle.index];

    Vulkan_Render_Pass *vulkan_render_pass = &context->render_passes[ descriptor.render_pass.index ];
    VkImageView *vulkan_attachments = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, VkImageView, descriptor.attachments.count);

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

    HE_CHECK_VKRESULT(vkCreateFramebuffer(context->logical_device, &frame_buffer_create_info, &context->allocation_callbacks, &vulkan_frame_buffer->handle));
    return true;
}

void vulkan_renderer_destroy_frame_buffer(Frame_Buffer_Handle frame_buffer_handle, bool immediate)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Frame_Buffer &frame_buffer = context->frame_buffers[frame_buffer_handle.index];
    if (immediate)
    {
        vkDestroyFramebuffer(context->logical_device, frame_buffer.handle, &context->allocation_callbacks);
    }
    else
    {
        U32 frame_index = context->renderer_state->current_frame_in_flight_index;
        append(&context->pending_delete_frame_buffers[frame_index], frame_buffer);
    }
}

static VkBufferUsageFlags get_buffer_usage_flags(Buffer_Usage usage)
{
    switch (usage)
    {
        case Buffer_Usage::TRANSFER:
        {
            return VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        } break;

        case Buffer_Usage::VERTEX:
        {
            return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        } break;

        case Buffer_Usage::INDEX:
        {
            return VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        } break;

        case Buffer_Usage::UNIFORM:
        {
            return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        } break;

        case Buffer_Usage::STORAGE_CPU_SIDE:
        {
            return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        } break;

        case Buffer_Usage::STORAGE_GPU_SIDE:
        {
            return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        } break;

        default:
        {
            HE_ASSERT(!"unsupported buffer usage");
        } break;
    }

    return VK_BUFFER_USAGE_FLAG_BITS_MAX_ENUM;
}

static VmaAllocationCreateInfo get_vma_allocation_create_info(Buffer_Usage usage)
{
    VmaAllocationCreateInfo result = {};

    switch (usage)
    {
        case Buffer_Usage::TRANSFER:
        {
            result.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            result.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        } break;

        case Buffer_Usage::VERTEX:
        case Buffer_Usage::INDEX:
        case Buffer_Usage::STORAGE_GPU_SIDE:
        {
            result.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        } break;

        case Buffer_Usage::UNIFORM:
        {
            result.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            result.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        } break;

        case Buffer_Usage::STORAGE_CPU_SIDE:
        {
            result.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            result.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        } break;

        default:
        {
            HE_ASSERT(!"unsupported buffer usage");
        } break;
    }

    return result;
}

bool vulkan_renderer_create_buffer(Buffer_Handle buffer_handle, const Buffer_Descriptor &descriptor)
{
    HE_ASSERT(descriptor.size);
    Vulkan_Context *context = &vulkan_context;

    Buffer *buffer = get(&context->renderer_state->buffers, buffer_handle);
    Vulkan_Buffer *vulkan_buffer = &context->buffers[buffer_handle.index];

    VkBufferCreateInfo buffer_create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_create_info.size = descriptor.size;
    buffer_create_info.usage = get_buffer_usage_flags(descriptor.usage);
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_create_info.flags = 0;

    VmaAllocationCreateInfo allocation_create_info = get_vma_allocation_create_info(descriptor.usage);
    vmaCreateBuffer(context->allocator, &buffer_create_info, &allocation_create_info, &vulkan_buffer->handle, &vulkan_buffer->allocation, &vulkan_buffer->allocation_info);

    buffer->usage = descriptor.usage;
    buffer->size = vulkan_buffer->allocation_info.size;
    buffer->data = vulkan_buffer->allocation_info.pMappedData;
    return true;
}

void vulkan_renderer_destroy_buffer(Buffer_Handle buffer_handle, bool immediate)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Buffer &vulkan_buffer = context->buffers[buffer_handle.index];
    if (immediate)
    {
        vmaDestroyBuffer(context->allocator, vulkan_buffer.handle, vulkan_buffer.allocation);
    }
    else
    {
        U32 frame_index = context->renderer_state->current_frame_in_flight_index;
        append(&context->pending_delete_buffers[frame_index], vulkan_buffer);
    }
}

bool vulkan_renderer_create_static_mesh(Static_Mesh_Handle static_mesh_handle, const Static_Mesh_Descriptor &descriptor, Upload_Request_Handle upload_request_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;
    Static_Mesh *static_mesh = get(&renderer_state->static_meshes, static_mesh_handle);

    U64 position_size = descriptor.vertex_count * sizeof(glm::vec3);
    U64 normal_size = descriptor.vertex_count * sizeof(glm::vec3);
    U64 uv_size = descriptor.vertex_count * sizeof(glm::vec2);
    U64 tangent_size = descriptor.vertex_count * sizeof(glm::vec4);
    U64 index_size = descriptor.index_count * sizeof(U16);

    U64 position_offset = (U8 *)descriptor.positions - renderer_state->transfer_allocator.base;
    U64 normal_offset = (U8 *)descriptor.normals - renderer_state->transfer_allocator.base;
    U64 uv_offset = (U8 *)descriptor.uvs - renderer_state->transfer_allocator.base;
    U64 tangent_offset = (U8 *)descriptor.tangents - renderer_state->transfer_allocator.base;
    U64 indicies_offset = (U8 *)descriptor.indices - renderer_state->transfer_allocator.base;

    Vulkan_Command_Buffer command_buffer = push_command_buffer(Command_Buffer_Usage::TRANSFER, true, context);

    Vulkan_Buffer *transfer_buffer = &context->buffers[renderer_state->transfer_buffer.index];

    Vulkan_Buffer *positions_buffer = &context->buffers[static_mesh->positions_buffer.index];
    Vulkan_Buffer *normals_buffer = &context->buffers[static_mesh->normals_buffer.index];
    Vulkan_Buffer *uvs_buffer = &context->buffers[static_mesh->uvs_buffer.index];
    Vulkan_Buffer *tangents_buffer = &context->buffers[static_mesh->tangents_buffer.index];
    Vulkan_Buffer *indices_buffer = &context->buffers[static_mesh->indices_buffer.index];

    VkBufferCopy position_copy_region = {};
    position_copy_region.srcOffset = position_offset;
    position_copy_region.dstOffset = 0;
    position_copy_region.size = position_size;
    vkCmdCopyBuffer(command_buffer.handle, transfer_buffer->handle, positions_buffer->handle, 1, &position_copy_region);

    VkBufferCopy normal_copy_region = {};
    normal_copy_region.srcOffset = normal_offset;
    normal_copy_region.dstOffset = 0;
    normal_copy_region.size = normal_size;
    vkCmdCopyBuffer(command_buffer.handle, transfer_buffer->handle, normals_buffer->handle, 1, &normal_copy_region);

    VkBufferCopy uv_copy_region = {};
    uv_copy_region.srcOffset = uv_offset;
    uv_copy_region.dstOffset = 0;
    uv_copy_region.size = uv_size;
    vkCmdCopyBuffer(command_buffer.handle, transfer_buffer->handle, uvs_buffer->handle, 1, &uv_copy_region);

    VkBufferCopy tangent_copy_region = {};
    tangent_copy_region.srcOffset = tangent_offset;
    tangent_copy_region.dstOffset = 0;
    tangent_copy_region.size = tangent_size;
    vkCmdCopyBuffer(command_buffer.handle, transfer_buffer->handle, tangents_buffer->handle, 1, &tangent_copy_region);

    VkBufferCopy index_copy_region = {};
    index_copy_region.srcOffset = indicies_offset;
    index_copy_region.dstOffset = 0;
    index_copy_region.size = index_size;
    vkCmdCopyBuffer(command_buffer.handle, transfer_buffer->handle, indices_buffer->handle, 1, &index_copy_region);

    pop_command_buffer(context, upload_request_handle);

    return true;
}

bool vulkan_renderer_create_semaphore(Semaphore_Handle semaphore_handle, const Renderer_Semaphore_Descriptor &descriptor)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Semaphore *vulkan_semaphore = &context->semaphores[semaphore_handle.index];

    VkSemaphoreTypeCreateInfo semaphore_type_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    semaphore_type_create_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    semaphore_type_create_info.initialValue = descriptor.initial_value;

    VkSemaphoreCreateInfo semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    semaphore_create_info.pNext = &semaphore_type_create_info;
    semaphore_create_info.flags = 0;

    HE_CHECK_VKRESULT(vkCreateSemaphore(context->logical_device, &semaphore_create_info, &context->allocation_callbacks, &vulkan_semaphore->handle));
    return true;
}

U64 vulkan_renderer_get_semaphore_value(Semaphore_Handle semaphore_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Semaphore *vulkan_semaphore = &context->semaphores[semaphore_handle.index];
    U64 value = 0;
    HE_CHECK_VKRESULT(vkGetSemaphoreCounterValue(context->logical_device, vulkan_semaphore->handle, &value));
    return value;
}

void vulkan_renderer_destroy_semaphore(Semaphore_Handle semaphore_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Semaphore *vulkan_semaphore = &context->semaphores[semaphore_handle.index];
    vkDestroySemaphore(context->logical_device, vulkan_semaphore->handle, &context->allocation_callbacks);
    vulkan_semaphore->handle = VK_NULL_HANDLE;
}

void vulkan_renderer_destroy_upload_request(Upload_Request_Handle upload_request_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Vulkan_Upload_Request *vulkan_upload_request = &context->upload_requests[upload_request_handle.index];
    HE_ASSERT(vulkan_upload_request->command_pool != VK_NULL_HANDLE);
    HE_ASSERT(vulkan_upload_request->command_buffer != VK_NULL_HANDLE);

    vkFreeCommandBuffers(context->logical_device, vulkan_upload_request->command_pool, 1, &vulkan_upload_request->command_buffer);
    vulkan_upload_request->command_pool = VK_NULL_HANDLE;
    vulkan_upload_request->command_buffer = VK_NULL_HANDLE;
}

void vulkan_renderer_set_vsync(bool enabled)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    VkPresentModeKHR present_mode = pick_present_mode(enabled, &context->swapchain_support);

    if (present_mode == context->swapchain.present_mode)
    {
        return;
    }

    recreate_swapchain(context, &context->swapchain, renderer_state->back_buffer_width, renderer_state->back_buffer_height, present_mode);
}

struct Cubemap_Face_Render_Info
{
    VkImageView *image_views;
    VkFramebuffer *frame_buffers;
};

Array<Cubemap_Face_Render_Info, 6> create_cubemap_face_info_array(Texture *texture, Vulkan_Image *image, Vulkan_Render_Pass *render_pass, U32 mip_levels, Allocator allocator)
{
    Vulkan_Context *context = &vulkan_context;
    Array<Cubemap_Face_Render_Info, 6> faces = {};

    for (U32 face_index = 0; face_index < 6; face_index++)
    {
        Cubemap_Face_Render_Info *face_info = &faces[face_index];
        face_info->image_views = HE_ALLOCATOR_ALLOCATE_ARRAY(allocator, VkImageView, mip_levels);
        face_info->frame_buffers = HE_ALLOCATOR_ALLOCATE_ARRAY(allocator, VkFramebuffer, mip_levels);

        U32 width = texture->width;
        U32 height = texture->height;

        for (U32 mip_index = 0; mip_index < mip_levels; mip_index++)
        {
            VkImageViewCreateInfo image_view_create_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            image_view_create_info.image = image->handle;
            image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            image_view_create_info.format = get_texture_format(texture->format);
            image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_view_create_info.subresourceRange.baseMipLevel = mip_index;
            image_view_create_info.subresourceRange.levelCount = 1;
            image_view_create_info.subresourceRange.baseArrayLayer = face_index;
            image_view_create_info.subresourceRange.layerCount = 1;

            HE_CHECK_VKRESULT(vkCreateImageView(context->logical_device, &image_view_create_info, &context->allocation_callbacks, &face_info->image_views[mip_index]));

            VkFramebufferCreateInfo frame_buffer_create_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            frame_buffer_create_info.width = width;
            frame_buffer_create_info.height = height;
            frame_buffer_create_info.layers = 1;
            frame_buffer_create_info.attachmentCount = 1;
            frame_buffer_create_info.pAttachments = &face_info->image_views[mip_index];
            frame_buffer_create_info.renderPass = render_pass->handle;

            HE_CHECK_VKRESULT(vkCreateFramebuffer(context->logical_device, &frame_buffer_create_info, &context->allocation_callbacks, &face_info->frame_buffers[mip_index]));

            width /= 2;
            height /= 2;
        }
    }

    return faces;
}

void free_cubemap_face_info_array(Array<Cubemap_Face_Render_Info, 6> face_infos, U32 mip_levels)
{
    Vulkan_Context *context = &vulkan_context;

    for (U32 face_index = 0; face_index < 6; face_index++)
    {
        Cubemap_Face_Render_Info *face_info = &face_infos[face_index];
        for (U32 mip_index = 0; mip_index < mip_levels; mip_index++)
        {
            vkDestroyImageView(context->logical_device, face_info->image_views[mip_index], &context->allocation_callbacks);
            vkDestroyFramebuffer(context->logical_device, face_info->frame_buffers[mip_index], &context->allocation_callbacks);
        }
    }
}

void vulkan_renderer_fill_brdf_lut(Texture_Handle brdf_lut_texture_handle)
{
    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    Vulkan_Render_Pass *cubemap_render_pass = &context->render_passes[renderer_state->cubemap_render_pass.index];

    Texture *brdf_lut_texture = renderer_get_texture(brdf_lut_texture_handle);
    Vulkan_Image *vulkan_brdf_lut_texture = &context->textures[brdf_lut_texture_handle.index];

    Vulkan_Shader *brdf_lut_shader = &context->shaders[renderer_state->brdf_lut_shader.index];

    VkFramebufferCreateInfo brdf_lut_frame_buffer_create_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    brdf_lut_frame_buffer_create_info.renderPass = cubemap_render_pass->handle;
    brdf_lut_frame_buffer_create_info.attachmentCount = 1;
    brdf_lut_frame_buffer_create_info.pAttachments = &vulkan_brdf_lut_texture->view;
    brdf_lut_frame_buffer_create_info.width = brdf_lut_texture->width;
    brdf_lut_frame_buffer_create_info.height = brdf_lut_texture->height;
    brdf_lut_frame_buffer_create_info.layers = 1;

    VkFramebuffer brdf_lut_frame_buffer = {};
    HE_CHECK_VKRESULT(vkCreateFramebuffer(context->logical_device, &brdf_lut_frame_buffer_create_info, &context->allocation_callbacks, &brdf_lut_frame_buffer));

    Vulkan_Command_Buffer command_buffer = push_command_buffer(Command_Buffer_Usage::GRAPHICS, true, context);

    transtion_image_to_layout(command_buffer.handle, vulkan_brdf_lut_texture->handle, 0, 1, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkClearValue clear_value = { .color = { 1.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderPassBeginInfo begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    begin_info.framebuffer = brdf_lut_frame_buffer;
    begin_info.renderArea = { .offset = { 0, 0 }, .extent { brdf_lut_texture->width, brdf_lut_texture->height } };
    begin_info.clearValueCount = 1;
    begin_info.pClearValues = &clear_value;
    begin_info.renderPass = cubemap_render_pass->handle;
    vkCmdBeginRenderPass(command_buffer.handle, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

    internal_set_viewport(command_buffer.handle, brdf_lut_texture->width, brdf_lut_texture->height);
    internal_set_pipeline_state(command_buffer.handle, renderer_state->brdf_lut_pipeline_state, VK_PIPELINE_BIND_POINT_GRAPHICS);
    internal_draw_fullscreen_triangle(command_buffer.handle);

    vkCmdEndRenderPass(command_buffer.handle);

    transtion_image_to_layout(command_buffer.handle, vulkan_brdf_lut_texture->handle, 0, 1, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    pop_command_buffer(context);

    vkDestroyFramebuffer(context->logical_device, brdf_lut_frame_buffer, &context->allocation_callbacks);
    brdf_lut_texture->is_attachment = false;
    brdf_lut_texture->state = Resource_State::SHADER_READ_ONLY;
}

void vulkan_renderer_hdr_to_environment_map(const Enviornment_Map_Render_Data &render_data)
{
    Memory_Context memory_context = grab_memory_context();

    Vulkan_Context *context = &vulkan_context;
    Renderer_State *renderer_state = context->renderer_state;

    Vulkan_Image hdr_texture = {};

    {
        void *data_array[] = { render_data.hdr_data };

        Texture_Descriptor hdr_texture_descriptor =
        {
            .name = HE_STRING_LITERAL("HDR"),
            .width = (U32)render_data.hdr_width,
            .height = (U32)render_data.hdr_height,
            .format = Texture_Format::R32G32B32A32_SFLOAT,
            .data_array = to_array_view(data_array),
            .mipmapping = false,
            .sample_count = 1,
        };

        internal_create_texture(hdr_texture_descriptor, &hdr_texture);
    }

    Texture *hdr_cubemap = renderer_get_texture(render_data.hdr_cubemap_handle);
    Texture *irradiance_cubemap = renderer_get_texture(render_data.irradiance_cubemap_handle);
    Texture *prefilter_cubemap = renderer_get_texture(render_data.prefilter_cubemap_handle);

    Vulkan_Image *vulkan_hdr_cubemap = &context->textures[render_data.hdr_cubemap_handle.index];
    Vulkan_Image *vulkan_irradiance_cubemap = &context->textures[render_data.irradiance_cubemap_handle.index];
    Vulkan_Image *vulkan_prefilter_cubemap = &context->textures[render_data.prefilter_cubemap_handle.index];

    Vulkan_Shader *hdr_shader = &context->shaders[renderer_state->hdr_shader.index];
    Vulkan_Shader *irradiance_shader = &context->shaders[renderer_state->irradiance_shader.index];
    Vulkan_Shader *prefilter_shader = &context->shaders[renderer_state->prefilter_shader.index];

    Vulkan_Render_Pass *cubemap_render_pass = &context->render_passes[renderer_state->cubemap_render_pass.index];

    Array<Cubemap_Face_Render_Info, 6> hdr_cubemap_faces = create_cubemap_face_info_array(hdr_cubemap, vulkan_hdr_cubemap, cubemap_render_pass, 1, memory_context.temp_allocator);

    Array<Cubemap_Face_Render_Info, 6> irrdiance_cubemap_faces = create_cubemap_face_info_array(irradiance_cubemap, vulkan_irradiance_cubemap, cubemap_render_pass, 1, memory_context.temp_allocator);

    const U32 prefileter_map_mip_levels = prefilter_cubemap->mip_levels;

    Array<Cubemap_Face_Render_Info, 6> prefilter_cubemap_faces = create_cubemap_face_info_array(prefilter_cubemap, vulkan_prefilter_cubemap, cubemap_render_pass, prefileter_map_mip_levels, memory_context.temp_allocator);

    Vulkan_Descriptor_Pool_Allocator descriptor_pool_allocator = create_descriptor_pool_allocator(16, memory_context.temp_allocator);

    VkDescriptorSet hdr_descriptor_set = allocate_descriptor_set(&descriptor_pool_allocator, hdr_shader->descriptor_set_layouts[0]);

    {
        VkWriteDescriptorSet write_descriptor_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write_descriptor_set.dstSet = hdr_descriptor_set;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write_descriptor_set.dstArrayElement = 0;
        write_descriptor_set.dstBinding = 0;

        Vulkan_Sampler *vulkan_sampler = &context->samplers[renderer_state->default_texture_sampler.index];

        VkDescriptorImageInfo image_info = {};
        image_info.sampler = vulkan_sampler->handle;
        image_info.imageView = hdr_texture.view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        write_descriptor_set.pImageInfo = &image_info;

        vkUpdateDescriptorSets(context->logical_device, 1, &write_descriptor_set, 0, nullptr);
    }

    Bind_Group_Descriptor irradiance_bind_group_descriptor =
    {
        .shader = renderer_state->irradiance_shader,
        .group_index = 0,
    };

    Bind_Group_Handle irradiance_bind_group_handle = renderer_create_bind_group(irradiance_bind_group_descriptor);
    HE_ASSERT(is_valid_handle(&renderer_state->bind_groups, irradiance_bind_group_handle));

    Buffer *globals_uniform_buffer = renderer_get_buffer(render_data.globals_uniform_buffer);
    glm::mat4 *projection = (glm::mat4 *)globals_uniform_buffer->data;
    *projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

    {
        Update_Binding_Descriptor update_bindings[] =
        {
            {
                .binding_number = 0,
                .element_index = 0,
                .count = 1,
                .buffers = &render_data.globals_uniform_buffer
            },
            {
                .binding_number = 1,
                .element_index = 0,
                .count = 1,
                .textures = &render_data.hdr_cubemap_handle,
                .samplers = &renderer_state->default_cubemap_sampler
            }
        };

        internal_update_bind_group(&descriptor_pool_allocator, irradiance_bind_group_handle, to_array_view(update_bindings));
    }

    Bind_Group_Descriptor prefilter_bind_group_descriptor =
    {
        .shader = renderer_state->prefilter_shader,
        .group_index = 0,
    };

    Bind_Group_Handle prefilter_bind_group_handle = renderer_create_bind_group(prefilter_bind_group_descriptor);
    HE_ASSERT(is_valid_handle(&renderer_state->bind_groups, prefilter_bind_group_handle));

    {
        Update_Binding_Descriptor update_bindings[] =
        {
            {
                .binding_number = 0,
                .element_index = 0,
                .count = 1,
                .buffers = &render_data.globals_uniform_buffer
            },
            {
                .binding_number = 1,
                .element_index = 0,
                .count = 1,
                .textures = &render_data.hdr_cubemap_handle,
                .samplers = &renderer_state->default_cubemap_sampler
            }
        };

        internal_update_bind_group(&descriptor_pool_allocator, prefilter_bind_group_handle, to_array_view(update_bindings));
    }

    Static_Mesh_Handle cube_mesh_handle = renderer_state->default_static_mesh;
    Static_Mesh *cube_mesh = renderer_get_static_mesh(cube_mesh_handle);

    Vulkan_Command_Buffer command_buffer = push_command_buffer(Command_Buffer_Usage::GRAPHICS, true, context);

    // hdr pass
    transtion_image_to_layout(command_buffer.handle, vulkan_hdr_cubemap->handle, 0, 1, 0, 6, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    for (U32 face_index = 0; face_index < 6; face_index++)
    {
        VkClearValue clear_value = { .color = { 1.0f, 0.0f, 0.0f, 1.0f } };

        VkRenderPassBeginInfo begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        begin_info.framebuffer = hdr_cubemap_faces[face_index].frame_buffers[0];
        begin_info.renderArea = { .offset = { 0, 0 }, .extent { hdr_cubemap->width, hdr_cubemap->height } };
        begin_info.clearValueCount = 1;
        begin_info.pClearValues = &clear_value;
        begin_info.renderPass = cubemap_render_pass->handle;
        vkCmdBeginRenderPass(command_buffer.handle, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

        internal_set_viewport(command_buffer.handle, hdr_cubemap->width, hdr_cubemap->height);
        vkCmdBindDescriptorSets(command_buffer.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, hdr_shader->pipeline_layout, 0, 1, &hdr_descriptor_set, 0, nullptr);
        internal_set_pipeline_state(command_buffer.handle, renderer_state->hdr_pipeline_state, VK_PIPELINE_BIND_POINT_GRAPHICS);

        Buffer_Handle buffers[] = { cube_mesh->positions_buffer };
        U64 offsets[] = { 0 };
        internal_set_vertex_buffers(command_buffer.handle, to_array_view(buffers), to_array_view(offsets));
        internal_set_index_buffer(command_buffer.handle, cube_mesh->indices_buffer, 0);

        glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        projection[1][1] *= -1;

        glm::mat4 views[] =
        {
            // POSITIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        };

        struct Push_Constants
        {
            glm::mat4 view;
            alignas(16) glm::mat4 projection;
        };
        static_assert(sizeof(Push_Constants) <= 128);

        Push_Constants constants =
        {
            .view = views[face_index],
            .projection = projection,
        };

        vkCmdPushConstants(command_buffer.handle, hdr_shader->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Push_Constants), &constants);

        internal_draw_sub_mesh(command_buffer.handle, cube_mesh_handle, 0, 0);

        vkCmdEndRenderPass(command_buffer.handle);
    }

    // wait for hdr pass to finish
    vkCmdPipelineBarrier(command_buffer.handle, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);

    transtion_image_to_layout(command_buffer.handle, vulkan_hdr_cubemap->handle, 0, 1, 0, 6, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    for (U32 face_index = 0; face_index < 6; face_index++)
    {
        U32 width = hdr_cubemap->width;
        U32 height = hdr_cubemap->height;

        for (U32 mip_index = 1; mip_index < hdr_cubemap->mip_levels; mip_index++)
        {
            transtion_image_to_layout(command_buffer.handle, vulkan_hdr_cubemap->handle, mip_index, 1, face_index, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageBlit blit = {};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { (S32)width, (S32)height, 1 };
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = mip_index - 1;
            blit.srcSubresource.baseArrayLayer = face_index;
            blit.srcSubresource.layerCount = 1;

            width /= 2;
            height /= 2;

            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = { (S32)width, (S32)height, 1 };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = mip_index;
            blit.dstSubresource.baseArrayLayer = face_index;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(command_buffer.handle, vulkan_hdr_cubemap->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vulkan_hdr_cubemap->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            transtion_image_to_layout(command_buffer.handle, vulkan_hdr_cubemap->handle, mip_index, 1, face_index, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        }
    }

    transtion_image_to_layout(command_buffer.handle, vulkan_hdr_cubemap->handle, 0, hdr_cubemap->mip_levels, 0, 6, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    transtion_image_to_layout(command_buffer.handle, vulkan_irradiance_cubemap->handle, 0, 1, 0, 6, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    transtion_image_to_layout(command_buffer.handle, vulkan_prefilter_cubemap->handle, 0, prefileter_map_mip_levels, 0, 6, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // irradiance pass
    for (U32 face_index = 0; face_index < 6; face_index++)
    {
        VkClearValue clear_value = { .color = { 1.0f, 0.0f, 0.0f, 1.0f } };

        VkRenderPassBeginInfo begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        begin_info.framebuffer = irrdiance_cubemap_faces[face_index].frame_buffers[0];
        begin_info.renderArea = { .offset = { 0, 0 }, .extent { irradiance_cubemap->width, irradiance_cubemap->height } };
        begin_info.clearValueCount = 1;
        begin_info.pClearValues = &clear_value;
        begin_info.renderPass = cubemap_render_pass->handle;
        vkCmdBeginRenderPass(command_buffer.handle, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

        internal_set_viewport(command_buffer.handle, irradiance_cubemap->width, irradiance_cubemap->height);
        internal_set_bind_groups(command_buffer.handle, 0, { .count = 1, .data = &irradiance_bind_group_handle }, VK_PIPELINE_BIND_POINT_GRAPHICS);
        internal_set_pipeline_state(command_buffer.handle, renderer_state->irradiance_pipeline_state, VK_PIPELINE_BIND_POINT_GRAPHICS);

        Buffer_Handle buffers[] = { cube_mesh->positions_buffer };
        U64 offsets[] = { 0 };
        internal_set_vertex_buffers(command_buffer.handle, to_array_view(buffers), to_array_view(offsets));
        internal_set_index_buffer(command_buffer.handle, cube_mesh->indices_buffer, 0);

        struct Push_Constants
        {
            glm::mat4 view;
        };
        static_assert(sizeof(Push_Constants) <= 128);

        glm::mat4 views[] =
        {
            // POSITIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        };

        Push_Constants constants =
        {
            .view = views[face_index],
        };

        vkCmdPushConstants(command_buffer.handle, irradiance_shader->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Push_Constants), &constants);

        internal_draw_sub_mesh(command_buffer.handle, cube_mesh_handle, 0, 0);

        vkCmdEndRenderPass(command_buffer.handle);
    }

    // prefilter pass

    for (U32 face_index = 0; face_index < 6; face_index++)
    {
        Cubemap_Face_Render_Info *face_info = &prefilter_cubemap_faces[face_index];

        glm::mat4 views[] =
        {
            // POSITIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        };

        U32 width = prefilter_cubemap->width;
        U32 height = prefilter_cubemap->height;

        for (U32 mip_index = 0; mip_index < prefileter_map_mip_levels; mip_index++)
        {
            VkClearValue clear_value = { .color = { 1.0f, 0.0f, 0.0f, 1.0f } };

            VkRenderPassBeginInfo begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            begin_info.framebuffer = face_info->frame_buffers[mip_index];
            begin_info.renderArea = { .offset = { 0, 0 }, .extent { width, height } };
            begin_info.clearValueCount = 1;
            begin_info.pClearValues = &clear_value;
            begin_info.renderPass = cubemap_render_pass->handle;
            vkCmdBeginRenderPass(command_buffer.handle, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

            internal_set_viewport(command_buffer.handle, width, height);
            internal_set_bind_groups(command_buffer.handle, 0, { .count = 1, .data = &prefilter_bind_group_handle }, VK_PIPELINE_BIND_POINT_GRAPHICS);
            internal_set_pipeline_state(command_buffer.handle, renderer_state->prefilter_pipeline_state, VK_PIPELINE_BIND_POINT_GRAPHICS);

            Buffer_Handle buffers[] = { cube_mesh->positions_buffer };
            U64 offsets[] = { 0 };
            internal_set_vertex_buffers(command_buffer.handle, to_array_view(buffers), to_array_view(offsets));
            internal_set_index_buffer(command_buffer.handle, cube_mesh->indices_buffer, 0);

            struct Push_Constants
            {
                F32 roughness;
                alignas(16) glm::mat4 view;
            };
            static_assert(sizeof(Push_Constants) <= 128);

            Push_Constants constants =
            {
                .roughness = (F32)mip_index / F32(prefileter_map_mip_levels - 1),
                .view = views[face_index],
            };

            vkCmdPushConstants(command_buffer.handle, prefilter_shader->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push_Constants), &constants);

            internal_draw_sub_mesh(command_buffer.handle, cube_mesh_handle, 0, 0);

            vkCmdEndRenderPass(command_buffer.handle);

            width /= 2;
            height /= 2;
        }
    }

    transtion_image_to_layout(command_buffer.handle, vulkan_irradiance_cubemap->handle, 0, 1, 0, 6, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    transtion_image_to_layout(command_buffer.handle, vulkan_prefilter_cubemap->handle, 0, prefileter_map_mip_levels, 0, 6, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    pop_command_buffer(context);

    destroy_descriptor_pool_allocator(&descriptor_pool_allocator);

    free_cubemap_face_info_array(hdr_cubemap_faces, 1);
    free_cubemap_face_info_array(irrdiance_cubemap_faces, 1);
    free_cubemap_face_info_array(prefilter_cubemap_faces, prefileter_map_mip_levels);
    
    vkDestroyImageView(context->logical_device, hdr_texture.view, &context->allocation_callbacks);
    vmaDestroyImage(context->allocator, hdr_texture.handle, hdr_texture.allocation);

    hdr_cubemap->is_attachment = false;
    hdr_cubemap->state = Resource_State::SHADER_READ_ONLY;

    irradiance_cubemap->is_attachment = false;
    irradiance_cubemap->state = Resource_State::SHADER_READ_ONLY;

    prefilter_cubemap->is_attachment = false;
    prefilter_cubemap->state = Resource_State::SHADER_READ_ONLY;
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

    HE_CHECK_VKRESULT(vkCreateDescriptorPool(context->logical_device, &descriptor_pool_create_info, &context->allocation_callbacks, &context->imgui_descriptor_pool));

    ImGui_ImplVulkan_InitInfo imgui_impl_vulkan_init_info = {};
    imgui_impl_vulkan_init_info.Allocator = &context->allocation_callbacks;
    imgui_impl_vulkan_init_info.Instance = context->instance;
    imgui_impl_vulkan_init_info.PhysicalDevice = context->physical_device;
    imgui_impl_vulkan_init_info.Device = context->logical_device;
    imgui_impl_vulkan_init_info.Queue = context->graphics_queue;
    imgui_impl_vulkan_init_info.QueueFamily = context->graphics_queue_family_index;
    imgui_impl_vulkan_init_info.DescriptorPool = context->imgui_descriptor_pool;
    imgui_impl_vulkan_init_info.MinImageCount = context->swapchain.image_count;
    imgui_impl_vulkan_init_info.ImageCount = context->swapchain.image_count;
    imgui_impl_vulkan_init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    imgui_impl_vulkan_init_info.PipelineCache = context->pipeline_cache;

    // todo(amer): we should refer to the ui pass from the render graph
    Render_Pass_Descriptor render_pass_descriptor =
    {
        .name = HE_STRING_LITERAL("imgui_pass"),
        .color_attachments =
        {{
            {
                .format = Texture_Format::R8G8B8A8_UNORM,
                .sample_count = 1,
                .operation = Attachment_Operation::LOAD
            },
        }},
        .depth_stencil_attachments =
        {{
            {
                .format = Texture_Format::DEPTH_F32_STENCIL_U8,
                .sample_count = 1,
                .operation = Attachment_Operation::CLEAR
            }
        }}
    };
    
    Render_Pass_Handle imgui_render_pass = renderer_create_render_pass(render_pass_descriptor);

    Vulkan_Render_Pass *vulkan_render_pass = &context->render_passes[ imgui_render_pass.index ];
    ImGui_ImplVulkan_Init(&imgui_impl_vulkan_init_info, vulkan_render_pass->handle);

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
    Vulkan_Command_Buffer command_buffer = get_commnad_buffer(context);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer.handle);
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
            usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
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
    HE_CHECK_VKRESULT(vkCreateImage(context->logical_device, &image_create_info, &context->allocation_callbacks, &image));

    VkMemoryRequirements vulkan_memory_requirements = {};
    vkGetImageMemoryRequirements(context->logical_device, image, &vulkan_memory_requirements);    
    vkDestroyImage(context->logical_device, image, &context->allocation_callbacks);

    Memory_Requirements result = {};
    result.size = vulkan_memory_requirements.size;
    result.alignment = vulkan_memory_requirements.alignment;

    return result;
}