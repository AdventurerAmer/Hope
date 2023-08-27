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
#include "rendering/renderer_types.h"

#define HOPE_VULKAN_DEBUGGING 1
#define HOPE_MAX_FRAMES_IN_FLIGHT 3
#define HOPE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT UINT16_MAX
#define HOPE_MAX_DESCRIPTOR_SET_COUNT 4

#ifdef HOPE_SHIPPING
#undef HOPE_VULKAN_DEBUGGING
#define HOPE_VULKAN_DEBUGGING 0
#endif

#if HOPE_VULKAN_DEBUGGING

#define HOPE_CheckVkResult(VulkanFunctionCall)\
{\
    VkResult vk_result = VulkanFunctionCall;\
    HOPE_Assert(vk_result == VK_SUCCESS);\
}

#else

#define HOPE_CheckVkResult(VulkanFunctionCall) VulkanFunctionCall

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

    VkSampler sampler;
};

struct Vulkan_Buffer
{
    VkBuffer handle;
    VkDeviceMemory memory;
    void *data;
    U64 size;
};

struct Vulkan_Descriptor_Set
{
    U32 binding_count;
    VkDescriptorSetLayoutBinding *bindings;
};

struct Vulkan_Shader
{
    VkShaderModule handle;
    VkShaderStageFlagBits stage;
    Vulkan_Descriptor_Set sets[HOPE_MAX_DESCRIPTOR_SET_COUNT];
};

struct Vulkan_Pipeline_State
{
    U32 descriptor_set_layout_count;
    VkDescriptorSetLayout descriptor_set_layouts[HOPE_MAX_DESCRIPTOR_SET_COUNT];

    VkPipelineLayout layout;
    VkPipeline handle;
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

struct Vulkan_Material
{
    Vulkan_Buffer buffers[HOPE_MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSet descriptor_sets[HOPE_MAX_FRAMES_IN_FLIGHT];
};

struct Vulkan_Static_Mesh
{
    S32 first_vertex;
    U32 first_index;
};

#define MAX_OBJECT_DATA_COUNT UINT16_MAX
#define MAX_DESCRIPTOR_SET_COUNT 4
#define PIPELINE_CACHE_FILENAME "shaders/bin/pipeline.cache"

struct Engine;

struct Vulkan_Context
{
    Engine *engine;

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

    VkPipelineCache pipeline_cache;

    VkCommandPool graphics_command_pool;
    VkCommandBuffer graphics_command_buffers[HOPE_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore image_available_semaphores[HOPE_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore rendering_finished_semaphores[HOPE_MAX_FRAMES_IN_FLIGHT];
    VkFence frame_in_flight_fences[HOPE_MAX_FRAMES_IN_FLIGHT];

    Vulkan_Buffer globals_uniform_buffers[HOPE_MAX_FRAMES_IN_FLIGHT];

    Vulkan_Buffer object_storage_buffers[HOPE_MAX_FRAMES_IN_FLIGHT];
    Object_Data *object_data_base;
    U32 object_data_count;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_sets[MAX_DESCRIPTOR_SET_COUNT][HOPE_MAX_FRAMES_IN_FLIGHT];

    VkCommandPool transfer_command_pool;
    Vulkan_Buffer transfer_buffer;

    Vulkan_Buffer vertex_buffer;
    U64 vertex_offset;

    Vulkan_Buffer index_buffer;
    U64 index_offset;

    U32 frames_in_flight;
    U32 current_frame_in_flight_index;
    U32 current_swapchain_image_index;

    Vulkan_Image *textures;
    Vulkan_Material *materials;
    Vulkan_Static_Mesh *static_meshes;
    Vulkan_Shader *shaders;
    Vulkan_Pipeline_State *pipeline_states;

    Free_List_Allocator *allocator;
    Free_List_Allocator transfer_allocator;

    VkDescriptorPool imgui_descriptor_pool;

#if HOPE_VULKAN_DEBUGGING
    VkDebugUtilsMessengerEXT debug_messenger;
#endif
};