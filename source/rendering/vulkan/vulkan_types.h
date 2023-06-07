#pragma once

#include <vulkan/vulkan.h>

#include <glm/vec3.hpp> // glm::vec3
#include <glm/vec4.hpp> // glm::vec4
#include <glm/mat4x4.hpp> // glm::mat4
#include <glm/gtc/quaternion.hpp> // quaternion
#include <glm/gtx/quaternion.hpp> // quaternion
#include <glm/ext/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale
#include <glm/ext/matrix_clip_space.hpp> // glm::perspective
#include <glm/ext/scalar_constants.hpp> // glm::pi

#include "core/defines.h"
#include "core/memory.h"
#include "renderer_types.h"

#define MAX_FRAMES_IN_FLIGHT 3

#define HE_VULKAN_DEBUGGING 1

#ifdef HE_SHIPPING
#undef HE_VULKAN_DEBUGGING
#define HE_VULKAN_DEBUGGING 0
#endif

#if HE_VULKAN_DEBUGGING

#define CheckVkResult(VulkanFunctionCall)\
{\
    VkResult vk_result = VulkanFunctionCall;\
    Assert(vk_result == VK_SUCCESS);\
}

#else

#define CheckVkResult(VulkanFunctionCall) VulkanFunctionCall

#endif

struct Vulkan_Image
{
    VkImage handle;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat format;
    U32 mip_levels;
    void *data;
    U64 size;
};

struct Vulkan_Buffer
{
    VkBuffer handle;
    VkDeviceMemory memory;
    void *data;
    U64 size;
};

struct Vulkan_Shader
{
    VkShaderModule handle;
};

struct Vulkan_Swapchain_Support
{
    U32 surface_format_count;
    VkSurfaceFormatKHR *surface_formats;

    U32 present_mode_count;
    VkPresentModeKHR *present_modes;

    VkFormat image_format;
    VkFormat depth_stencil_format;
};

struct Vulkan_Swapchain
{
    VkSwapchainKHR handle;
    U32 width;
    U32 height;
    VkPresentModeKHR present_mode;
    VkFormat image_format;
    VkColorSpaceKHR image_color_space;

    U32 image_count;
    VkImage *images;
    VkImageView *image_views;
    VkFramebuffer *frame_buffers;

    VkFormat depth_stencil_format;
    Vulkan_Image color_attachment;
    Vulkan_Image depth_stencil_attachment;
};

struct Vulkan_Graphics_Pipeline
{
    VkPipelineLayout layout;
    VkPipeline handle;
};

struct Vulkan_Global_Uniform_Buffer
{
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
};

struct Vulkan_Object_Data
{
    glm::mat4 model;
};

struct Vulkan_Material
{
    VkDescriptorSet descriptor_sets[MAX_FRAMES_IN_FLIGHT];
    VkSampler albedo_sampler;
};

struct Vulkan_Static_Mesh
{
    Vulkan_Buffer vertex_buffer;
    Vulkan_Buffer index_buffer;
};

#define MAX_OBJECT_DATA_COUNT 8192

struct Vulkan_Context
{
    VkInstance instance;

    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkPhysicalDeviceMemoryProperties physical_device_memory_properties;

    U32 graphics_queue_family_index;
    U32 present_queue_family_index;
    U32 transfer_queue_family_index;
    VkDevice logical_device;

    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue transfer_queue;

    Vulkan_Swapchain_Support swapchain_support;
    Vulkan_Swapchain swapchain;

    VkSampleCountFlagBits msaa_samples;
    VkRenderPass render_pass;

    Vulkan_Shader vertex_shader;
    Vulkan_Shader fragment_shader;
    Vulkan_Graphics_Pipeline mesh_pipeline;

    VkCommandPool graphics_command_pool;
    VkCommandBuffer graphics_command_buffers[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore rendering_finished_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence frame_in_flight_fences[MAX_FRAMES_IN_FLIGHT];

    Vulkan_Buffer global_uniform_buffers[MAX_FRAMES_IN_FLIGHT];

    Vulkan_Buffer object_storage_buffers[MAX_FRAMES_IN_FLIGHT];
    Vulkan_Object_Data *object_data_base;
    U32 object_data_count;

    VkDescriptorSetLayout per_frame_descriptor_set_layout;
    VkDescriptorSetLayout per_material_descriptor_set_layout;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet per_frame_descriptor_sets[MAX_FRAMES_IN_FLIGHT];

    VkCommandPool transfer_command_pool;
    VkCommandBuffer transfer_command_buffer;
    Vulkan_Buffer transfer_buffer;

    U32 frames_in_flight;
    U32 current_frame_in_flight_index;
    U32 current_swapchain_image_index;

    Free_List_Allocator *allocator;

#if HE_VULKAN_DEBUGGING
    VkDebugUtilsMessengerEXT debug_messenger;
#endif
};

inline Vulkan_Image*
get_data(Texture *texture)
{
    Assert(texture->rendering_api_specific_data);
    return (Vulkan_Image *)texture->rendering_api_specific_data;
}

inline Vulkan_Static_Mesh*
get_data(Static_Mesh *static_mesh)
{
    Assert(static_mesh->rendering_api_specific_data);
    return (Vulkan_Static_Mesh *)static_mesh->rendering_api_specific_data;
}

inline Vulkan_Material*
get_data(Material *materail)
{
    Assert(materail->rendering_api_specific_data);
    return (Vulkan_Material *)materail->rendering_api_specific_data;
}

inline Vulkan_Image*
get_data(const Texture *texture)
{
    Assert(texture->rendering_api_specific_data);
    return (Vulkan_Image *)texture->rendering_api_specific_data;
}

inline Vulkan_Static_Mesh*
get_data(const Static_Mesh *static_mesh)
{
    Assert(static_mesh->rendering_api_specific_data);
    return (Vulkan_Static_Mesh *)static_mesh->rendering_api_specific_data;
}

inline Vulkan_Material*
get_data(const Material *materail)
{
    Assert(materail->rendering_api_specific_data);
    return (Vulkan_Material *)materail->rendering_api_specific_data;
}