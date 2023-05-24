#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <string.h>

#include "vulkan.h"
#include "core/platform.h"
#include "core/debugging.h"
#include "core/memory.h"
#include "rendering/renderer.h"
#include "core/engine.h"

#include "vulkan_images_and_buffers.h"
#include "vulkan_swapchain.h"
#include "vulkan_shader.h"

#include "rendering/camera.h"

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
load_static_mesh(const char *path, Static_Mesh *static_mesh, Vulkan_Context *context, Memory_Arena *arena)
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
    stbi_uc *pixels = stbi_load("models/Default_albedo.jpg",
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
#if HE_OS_WINDOWS
        "VK_KHR_win32_surface",
#endif

#if HE_VULKAN_DEBUGGING
        "VK_EXT_debug_utils",
#endif
        "VK_KHR_surface",
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
    
    U32 width = 1280;
    U32 height = 720;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    U32 min_image_count = MAX_FRAMES_IN_FLIGHT;
    bool swapchain_created = create_swapchain(context, width, height,
                                              min_image_count, present_mode, &context->swapchain);
    Assert(swapchain_created);

    bool shader_loaded = load_shader(&context->vertex_shader, "shaders/basic.vert.spv", context, arena);
    Assert(shader_loaded);

    shader_loaded = load_shader(&context->fragment_shader, "shaders/basic.frag.spv", context, arena);
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

    create_buffer(&context->transfer_buffer, context, HE_MegaBytes(128),
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    bool loaded = load_static_mesh("models/DamagedHelmet.glb", &context->static_mesh, context, arena);
    Assert(loaded);

    create_graphics_pipeline(context,
                             context->vertex_shader.handle ,
                             context->fragment_shader.handle,
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

    Global_Uniform_Buffer gub_data;
    F32 aspect_ratio = (F32)renderer_state->back_buffer_width / (F32)renderer_state->back_buffer_height;

#if 0
    F32 rotation_speed = 45.0f;
    local_presist F32 rotation_angle = 0.0f;
    rotation_angle += rotation_speed * delta_time;
    if (rotation_angle >= 360.0f) rotation_angle -= 360.0f;

    glm::vec3 euler = glm::vec3(glm::radians(90.0f), glm::radians(rotation_angle), 0.0f);
    glm::quat rotation = glm::quat(euler);
    
    gub_data.model = glm::toMat4(rotation);
#else
    gub_data.model = glm::mat4(1.0f);
#endif

#if 0

    gub_data.view = glm::lookAt(glm::vec3(0.0f, 1.0f, -5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    gub_data.projection = glm::perspective(glm::radians(45.0f), aspect_ratio, 0.1f, 1000.0f);
    gub_data.projection[1][1] *= -1;
    
#else

    gub_data.view = renderer_state->camera.view;
    gub_data.projection = renderer_state->camera.projection;
    gub_data.projection[1][1] *= -1;

#endif

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
    vkDestroyCommandPool(context->logical_device, context->transfer_command_pool, nullptr);

    destroy_swapchain(context, &context->swapchain);
    destroy_graphics_pipeline(context->logical_device, &context->graphics_pipeline);

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

    // todo(amer): every renderer should call this and i don't like that
    update_camera(&renderer_state->camera);
}

void vulkan_renderer_draw(Renderer_State *renderer_state, F32 delta_time)
{
    vulkan_draw(renderer_state, &vulkan_context, delta_time);
}