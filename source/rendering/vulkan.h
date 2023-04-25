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

struct Vulkan_Swapchain
{
    VkSwapchainKHR handle;
    U32 width;
    U32 height;
    VkPresentModeKHR present_mode;
    VkSurfaceFormatKHR surface_format;

    // todo(amer): we only set the min image count
    // and vulkan could create more so we need a dynamic allocation
    // here using an array for now
    U32 image_count;
    VkImage images[3];
    VkImageView image_views[3];
};

struct Vulkan_Context
{
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkSurfaceKHR surface;
    Vulkan_Swapchain swapchain;

    VkShaderModule vertex_shader_module;
    VkShaderModule fragment_shader_module;
    VkPipelineLayout pipeline_layout;
    VkRenderPass render_pass;
    VkPipeline graphics_pipeline;

    VkFramebuffer frame_buffers[3];

    VkCommandPool graphics_command_pool;
    VkCommandBuffer graphics_command_buffers[3];

    VkSemaphore image_available_semaphores[3];
    VkSemaphore rendering_finished_semaphores[3];
    VkFence frame_in_flight_fences[3];

    U32 current_frame_index;

#if HE_VULKAN_DEBUGGING
    VkDebugUtilsMessengerEXT debug_messenger;
#endif
};

internal_function bool
init_vulkan(Vulkan_Context *context, HWND window, HINSTANCE instance, struct Memory_Arena *arena);

internal_function void
vulkan_draw(Vulkan_Context *context);

internal_function void
deinit_vulkan(Vulkan_Context *context);