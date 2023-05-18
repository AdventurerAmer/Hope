#pragma once

#include "core/defines.h"

#include <glm/vec3.hpp> // glm::vec3
#include <glm/vec4.hpp> // glm::vec4
#include <glm/mat4x4.hpp> // glm::mat4
#include <glm/gtc/quaternion.hpp> // quaternion
#include <glm/gtx/quaternion.hpp> // quaternion
#include <glm/ext/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale
#include <glm/ext/matrix_clip_space.hpp> // glm::perspective
#include <glm/ext/scalar_constants.hpp> // glm::pi

#include <vulkan/vulkan.h>

#include "core/memory.h"

#define HE_VULKAN_DEBUGGING 1

#if HE_VULKAN_DEBUGGING

#define CheckVkResult(VulkanFunctionCall)\
{\
    VkResult vk_result = VulkanFunctionCall;\
    Assert(vk_result == VK_SUCCESS);\
}

#else

#define CheckVkResult(VulkanFunctionCall) VulkanFunctionCall

#endif

#define MAX_FRAMES_IN_FLIGHT 3

struct Vulkan_Swapchain_Support
{
    U32 surface_format_count;
    VkSurfaceFormatKHR *surface_formats;

    U32 present_mode_count;
    VkPresentModeKHR *present_modes;

    VkFormat format;
};

struct Vulkan_Image
{
    VkImage handle;
    VkDeviceMemory memory;
    VkImageView view;
    U32 width;
    U32 height;
    void* data;
    U64 size;
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
    Vulkan_Image depth_sentcil_attachment;
};

struct Vulkan_Graphics_Pipeline
{
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout layout;
    VkPipeline handle;
};

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct Global_Uniform_Buffer
{
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
};

struct Vulkan_Buffer
{
    VkBuffer handle;
    VkDeviceMemory memory;
    void *data;
    U64 size;
};

struct Static_Mesh
{
    Vulkan_Buffer vertex_buffer;
    U32 vertex_count;

    Vulkan_Buffer index_buffer;
    U16 index_count;

    VkSampler sampler;
    Vulkan_Image image;
};

struct Vulkan_Context
{
    VkInstance instance;

    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkPhysicalDeviceMemoryProperties physical_device_memory_properties;

    U32 graphics_queue_family_index;
    U32 present_queue_family_index;
    VkDevice logical_device;

    VkQueue graphics_queue;
    VkQueue present_queue;

    Vulkan_Swapchain_Support swapchain_support;
    Vulkan_Swapchain swapchain;

    VkRenderPass render_pass;

    VkShaderModule vertex_shader_module;
    VkShaderModule fragment_shader_module;
    Vulkan_Graphics_Pipeline graphics_pipeline;

    VkCommandPool graphics_command_pool;
    VkCommandBuffer graphics_command_buffers[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore rendering_finished_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence frame_in_flight_fences[MAX_FRAMES_IN_FLIGHT];

    Vulkan_Buffer global_uniform_buffers[MAX_FRAMES_IN_FLIGHT];

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_sets[MAX_FRAMES_IN_FLIGHT];

    Free_List_Allocator *allocator;

    Vulkan_Buffer transfer_buffer;
    Static_Mesh static_mesh;

    U32 frames_in_flight;
    U32 current_frame_in_flight_index;

#if HE_VULKAN_DEBUGGING
    VkDebugUtilsMessengerEXT debug_messenger;
#endif
};

bool vulkan_renderer_init(struct Renderer_State *renderer_State,
                          struct Engine *engine,
                          struct Memory_Arena *arena);

void vulkan_renderer_deinit(struct Renderer_State *renderer_State);

void vulkan_renderer_on_resize(struct Renderer_State *renderer_State, U32 width, U32 height);

void vulkan_renderer_draw(struct Renderer_State *renderer_State, F32 delta_time);