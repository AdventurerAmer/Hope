#pragma once

#include "core/defines.h"

#if HE_OS_WINDOWS
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#define HE_VULKAN_DEBUGGING 1

#if HE_VULKAN_DEBUGGING

#define CheckVkResult(VulkanFunctionCall)\
{\
    VkResult vk_result = VulkanFunctionCall;\
    HE_Assert(vk_result == VK_SUCCESS);\
}

#else

#define CheckVkResult(VulkanFunctionCall) VulkanFunctionCall

#endif

#define HE_Max_Frames_In_Flight 2

struct Vulkan_Swapchain_Support
{
    U32 surface_format_count;
    VkSurfaceFormatKHR *surface_formats;

    U32 present_mode_count;
    VkPresentModeKHR *present_modes;

    VkFormat format;
};

struct Vulkan_Swapchain
{
    VkSwapchainKHR handle;
    U32 width;
    U32 height;
    VkPresentModeKHR present_mode;
    VkFormat image_format;
    VkColorSpaceKHR image_color_space;

    // todo(amer): we only set the min image count
    // and vulkan could create more so we need a dynamic allocation
    // here using an array and asserting for now....
    U32 image_count;
    VkImage images[HE_Max_Frames_In_Flight];
    VkImageView image_views[HE_Max_Frames_In_Flight];
    VkFramebuffer frame_buffers[HE_Max_Frames_In_Flight];
};

struct Vulkan_Graphics_Pipeline
{
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout layout;
    VkPipeline handle;
};

// todo(amer): temprary
struct Vector3
{
    F32 X;
    F32 Y;
    F32 Z;
};

// todo(amer): temprary
struct Vector4
{
    F32 X;
    F32 Y;
    F32 Z;
    F32 W;
};

// todo(amer): temprary
struct Vertex
{
    Vector3 position;
    Vector4 color;
};

struct Global_Uniform_Buffer
{
    Vector3 offset;
};

struct Vulkan_Buffer
{
    VkBuffer handle;
    VkDeviceMemory memory;
    void *data;
    U64 size;
};

struct Vulkan_Context
{
    VkInstance instance;

    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;

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

    Vulkan_Buffer vertex_buffer;
    Vulkan_Buffer index_buffer;

    VkCommandPool graphics_command_pool;
    VkCommandBuffer graphics_command_buffers[HE_Max_Frames_In_Flight];
    VkSemaphore image_available_semaphores[HE_Max_Frames_In_Flight];
    VkSemaphore rendering_finished_semaphores[HE_Max_Frames_In_Flight];
    VkFence frame_in_flight_fences[HE_Max_Frames_In_Flight];

    Vulkan_Buffer global_uniform_buffers[HE_Max_Frames_In_Flight];

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_sets[HE_Max_Frames_In_Flight];

    U32 frames_in_flight;
    U32 current_frame_in_flight_index;

#if HE_VULKAN_DEBUGGING
    VkDebugUtilsMessengerEXT debug_messenger;
#endif
};

internal_function bool
init_vulkan(Vulkan_Context *context, struct Engine *engine, struct Memory_Arena *arena);

internal_function void
vulkan_draw(Vulkan_Context *context, U32 width, U32 height);

internal_function void
deinit_vulkan(Vulkan_Context *context);