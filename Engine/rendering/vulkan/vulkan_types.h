#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "core/defines.h"
#include "core/memory.h"
#include "core/platform.h"

#include "containers/hash_map.h"

#include "rendering/renderer_types.h"

#define HE_VULKAN_PIPELINE_CACHE_FILE_PATH "vulkan/pipeline_cache.bin"

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

    VkDescriptorSet imgui_handle;
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
    VkShaderModule handles[(U32)Shader_Stage::COUNT];

    VkDescriptorSetLayout descriptor_set_layouts[HE_MAX_BIND_GROUP_INDEX_COUNT];
    VkPipelineLayout pipeline_layout;
    
    U32 vertex_shader_input_count = 0;
    VkVertexInputBindingDescription *vertex_input_binding_descriptions = nullptr;
    VkVertexInputAttributeDescription *vertex_input_attribute_descriptions = nullptr;
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
    VkColorSpaceKHR color_space;
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

struct Vulkan_Upload_Request
{
    VkCommandPool graphics_command_pool;
    VkCommandBuffer graphics_command_buffer;
    
    VkCommandPool transfer_command_pool;
    VkCommandBuffer transfer_command_buffer;
};

struct Vulkan_Thread_State
{
    VkCommandPool graphics_command_pool;
    VkCommandPool transfer_command_pool;
    VkCommandPool compute_command_pool;
};

#define HE_MAX_VULKAN_DESCRIPTOR_POOL_SIZE_RATIO_COUNT 8

struct Vulkan_Descriptor_Pool_Size_Ratio
{
    VkDescriptorType type;
    float ratio;
};

struct Vulkan_Descriptor_Pool_Allocator
{
    U32 set_count_per_pool;
    Dynamic_Array< VkDescriptorPool > full_pools;
    Dynamic_Array< VkDescriptorPool > ready_pools;
};

struct Vulkan_Command_Buffer
{
    VkCommandPool pool;
    VkCommandBuffer handle;
};

#define HE_MAX_DESCRIPTOR_POOL_SIZE_RATIO_COUNT 16

struct Vulkan_Context
{
    struct Renderer_State *renderer_state;

    VkAllocationCallbacks allocation_callbacks;
    VkInstance instance;

    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkPhysicalDeviceMemoryProperties physical_device_memory_properties;

    VkDevice logical_device;
    VmaAllocator allocator;

    U32 graphics_queue_family_index;
    U32 present_queue_family_index;
    U32 transfer_queue_family_index;
    U32 compute_queue_family_index;
    
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue compute_queue;
    VkQueue transfer_queue;

    Vulkan_Swapchain_Support swapchain_support;
    Vulkan_Swapchain swapchain;
    U32 current_swapchain_image_index;

    Counted_Array< Vulkan_Descriptor_Pool_Size_Ratio, HE_MAX_DESCRIPTOR_POOL_SIZE_RATIO_COUNT > descriptor_pool_ratios;
    Vulkan_Descriptor_Pool_Allocator descriptor_pool_allocators[HE_MAX_FRAMES_IN_FLIGHT];
    
    Hash_Map< U32, Vulkan_Thread_State > thread_states;

    VkCommandBuffer graphics_command_buffers[HE_MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer compute_command_buffers[HE_MAX_FRAMES_IN_FLIGHT];

    VkCommandBuffer graphics_command_buffer;
    VkCommandBuffer compute_command_buffer;
    VkCommandBuffer command_buffer;

    VkPipelineCache pipeline_cache;

    PFN_vkQueueSubmit2KHR vkQueueSubmit2KHR;
    PFN_vkCmdPipelineBarrier2KHR vkCmdPipelineBarrier2KHR;

    VkSemaphore image_available_semaphores[HE_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore rendering_finished_semaphores[HE_MAX_FRAMES_IN_FLIGHT];

    VkSemaphore frame_timeline_semaphore;
    VkSemaphore compute_timeline_semaphore;
    U64 timeline_value;

    Vulkan_Buffer *buffers;
    Vulkan_Image *textures;
    Vulkan_Sampler *samplers;
    Vulkan_Shader *shaders;
    Vulkan_Pipeline_State *pipeline_states;
    Vulkan_Bind_Group *bind_groups;
    
    Vulkan_Render_Pass *render_passes;
    Vulkan_Frame_Buffer *frame_buffers;

    Vulkan_Semaphore *semaphores;
    Vulkan_Upload_Request *upload_requests;
    
    Dynamic_Array<Vulkan_Buffer>         pending_delete_buffers[HE_MAX_FRAMES_IN_FLIGHT];
    Dynamic_Array<Vulkan_Image>          pending_delete_textures[HE_MAX_FRAMES_IN_FLIGHT];
    Dynamic_Array<Vulkan_Sampler>        pending_delete_samplers[HE_MAX_FRAMES_IN_FLIGHT];
    Dynamic_Array<Vulkan_Shader>         pending_delete_shaders[HE_MAX_FRAMES_IN_FLIGHT];
    Dynamic_Array<Vulkan_Pipeline_State> pending_delete_pipeline_states[HE_MAX_FRAMES_IN_FLIGHT];
    Dynamic_Array<Vulkan_Render_Pass>    pending_delete_render_passes[HE_MAX_FRAMES_IN_FLIGHT];
    Dynamic_Array<Vulkan_Frame_Buffer>   pending_delete_frame_buffers[HE_MAX_FRAMES_IN_FLIGHT];
    
    VkDescriptorPool imgui_descriptor_pool;

#if HE_GRAPHICS_DEBUGGING
    VkDebugUtilsMessengerEXT debug_messenger;
#endif
};