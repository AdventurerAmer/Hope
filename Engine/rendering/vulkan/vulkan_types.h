#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "core/defines.h"
#include "core/memory.h"
#include "core/platform.h"
#include "rendering/renderer_types.h"

#if HE_GRAPHICS_DEBUGGING

#define HE_CHECK_VKRESULT(vulkan_function_call)\
{\
    VkResult result = vulkan_function_call;\
    HE_ASSERT(result == VK_SUCCESS);\
}

#else

#define HE_CHECK_VKRESULT(vulkan_function_call) vulkan_function_call

#endif

struct Vulkan_Image
{
    VkImage handle;
    VkImageView view;
    VmaAllocation allocation;
    VmaAllocationInfo allocation_info;
};

struct Vulkan_Sampler
{
    VkSampler handle;
};

struct Vulkan_Buffer
{
    VkBuffer handle;
    VmaAllocation allocation;
    VmaAllocationInfo allocation_info;
};

struct Vulkan_Shader
{
    VkShaderModule handle;
};

struct Vulkan_Bind_Group_Layout
{
    VkDescriptorSetLayout handle;
};

struct Vulkan_Shader_Group
{
    VkPipelineLayout pipeline_layout;
};

struct Vulkan_Bind_Group
{
    VkDescriptorSet handle;
};

struct Vulkan_Pipeline_State
{
    VkPipeline handle;
};

struct Vulkan_Frame_Buffer
{
    VkFramebuffer handle;
};

struct Vulkan_Semaphore
{
    VkSemaphore handle;
};

struct Vulkan_Swapchain_Support
{
    U32 surface_format_count;
    VkSurfaceFormatKHR *surface_formats;

    U32 present_mode_count;
    VkPresentModeKHR *present_modes;

    VkFormat image_format;
};

struct Vulkan_Swapchain
{
    VkSwapchainKHR handle;
    U32 width;
    U32 height;
    VkPresentModeKHR present_mode;
    VkFormat image_format;
    VkFormat depth_stencil_format;
    VkColorSpaceKHR image_color_space;

    U32 image_count;
    VkImage *images;
    VkImageView *image_views;
};

struct Vulkan_Render_Pass
{
    VkRenderPass handle;
};

struct Vulkan_Context
{
    struct Renderer_State *renderer_state;

    VkInstance instance;

    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkPhysicalDeviceMemoryProperties physical_device_memory_properties;

    U32 graphics_queue_family_index;
    U32 present_queue_family_index;
    U32 transfer_queue_family_index;
    VkDevice logical_device;
    VmaAllocator allocator;
    
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue transfer_queue;

    Vulkan_Swapchain_Support swapchain_support;
    Vulkan_Swapchain swapchain;
    U32 current_swapchain_image_index;
    
    VkSemaphore image_available_semaphores[HE_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore rendering_finished_semaphores[HE_MAX_FRAMES_IN_FLIGHT];
    
    VkSemaphore timeline_semaphore;
    U64 timeline_value;
    
    VkDescriptorPool descriptor_pool;
    VkDescriptorPool imgui_descriptor_pool;

    VkCommandPool graphics_command_pool;
    // todo(amer): this is temprary to test texture hot reloading in the future we are going to have to use a per thread command pool.
    VkCommandPool upload_textures_command_pool;
    VkCommandPool transfer_command_pool;

    VkCommandBuffer graphics_command_buffers[HE_MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer command_buffer;

    VkPipelineCache pipeline_cache;

    PFN_vkQueueSubmit2KHR vkQueueSubmit2KHR;
    PFN_vkCmdPipelineBarrier2KHR vkCmdPipelineBarrier2KHR;

    Vulkan_Buffer *buffers;
    Vulkan_Image *textures;
    Vulkan_Sampler *samplers;
    Vulkan_Shader *shaders;
    Vulkan_Shader_Group *shader_groups;
    Vulkan_Pipeline_State *pipeline_states;
    Vulkan_Bind_Group_Layout *bind_group_layouts;
    Vulkan_Bind_Group *bind_groups;
    Vulkan_Render_Pass *render_passes;
    Vulkan_Frame_Buffer *frame_buffers;
    Vulkan_Semaphore *semaphores;

#if HE_GRAPHICS_DEBUGGING
    VkDebugUtilsMessengerEXT debug_messenger;
#endif
};