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
    VkMemoryRequirements memory_requirements;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat format;
    U32 mip_levels;
};

struct Vulkan_Sampler
{
    VkSampler handle;
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

struct Vulkan_Static_Mesh
{
    S32 first_vertex;
    U32 first_index;
};

struct Vulkan_Context
{
    struct Renderer_State *renderer_state;

    Memory_Arena arena;
    Free_List_Allocator *allocator;

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
    U32 current_swapchain_image_index;
    
    VkSemaphore image_available_semaphores[HE_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore rendering_finished_semaphores[HE_MAX_FRAMES_IN_FLIGHT];
    
    VkSemaphore timeline_semaphore;
    U64 timeline_value;
    
    VkDescriptorPool descriptor_pool;
    VkDescriptorPool imgui_descriptor_pool;

    VkCommandPool graphics_command_pool;
    VkCommandBuffer graphics_command_buffers[HE_MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer command_buffer;

    VkPipelineCache pipeline_cache;

    VkCommandPool transfer_command_pool;

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
    Vulkan_Static_Mesh *static_meshes;
    Vulkan_Semaphore *semaphores;

#if HE_GRAPHICS_DEBUGGING
    VkDebugUtilsMessengerEXT debug_messenger;
#endif
};