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

#define HE_VULKAN_DEBUGGING 1
#define MAX_FRAMES_IN_FLIGHT 3
#define MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT UINT16_MAX
#define MAX_DESCRIPTOR_SET_COUNT 4

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

    VkSampler sampler;
};

struct Vulkan_Texture_Bundle
{
    Texture texture;
    Vulkan_Image vulkan_image;
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

struct Vulkan_Shader_Input_Variable
{
    const char *name;
    U32 name_length;
    ShaderDataType type;
    U32 location;
};

struct Vulkan_Shader_Output_Variable
{
    const char *name;
    U32 name_length;
    ShaderDataType type;
    U32 location;
};

struct Shader_Struct_Member
{
    const char *name;
    U32 name_length;

    ShaderDataType data_type;
    U32 offset;

    bool is_array;
    S32 array_element_count = -1;

    S32 struct_index = -1;
};

struct Shader_Struct
{
    const char *name;
    U32 name_length;

    U32 member_count;
    Shader_Struct_Member *members;
};

struct Vulkan_Shader
{
    VkShaderModule handle;

    VkShaderStageFlagBits stage;

    Vulkan_Descriptor_Set sets[MAX_DESCRIPTOR_SET_COUNT];

    U32 input_count;
    Vulkan_Shader_Input_Variable *inputs;

    U32 output_count;
    Vulkan_Shader_Output_Variable *outputs;

    U32 struct_count;
    Shader_Struct *structs;
};

struct Vulkan_Graphics_Pipeline
{
    U32 descriptor_set_layout_count;
    VkDescriptorSetLayout descriptor_set_layouts[MAX_DESCRIPTOR_SET_COUNT];

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

struct Vulkan_Globals_Uniform_Buffer
{
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
};

struct Vulkan_Object_Data
{
    alignas(16) glm::mat4 model;
    alignas(4)  U32 material_index;
};

struct Vulkan_Material_Data
{
    alignas(16) glm::mat4 model;
    U32 albedo_texture_index;
};

struct Vulkan_Material
{
    Vulkan_Material_Data data;
};

struct Vulkan_Material_Bundle
{
    Material material;
    Vulkan_Material vulkan_material;
};

struct Vulkan_Static_Mesh
{
    S32 first_vertex;
    U32 first_index;
};

struct Vulkan_Static_Mesh_Bundle
{
    Static_Mesh static_mesh;
    Vulkan_Static_Mesh vulkan_static_mesh;
};

#define MAX_OBJECT_DATA_COUNT 8192
#define MAX_DESCRIPTOR_SET_COUNT 4
#define PIPELINE_CACHE_FILENAME "shaders/pipeline.cache"

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

    VkPipelineCache pipeline_cache;
    Vulkan_Shader mesh_vertex_shader;
    Vulkan_Shader mesh_fragment_shader;
    Vulkan_Graphics_Pipeline mesh_pipeline;

    VkCommandPool graphics_command_pool;
    VkCommandBuffer graphics_command_buffers[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore rendering_finished_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence frame_in_flight_fences[MAX_FRAMES_IN_FLIGHT];

    Vulkan_Buffer globals_uniform_buffers[MAX_FRAMES_IN_FLIGHT];

    Vulkan_Buffer object_storage_buffers[MAX_FRAMES_IN_FLIGHT];
    Vulkan_Object_Data *object_data_base;
    U32 object_data_count;

    Vulkan_Buffer material_storage_buffers[MAX_FRAMES_IN_FLIGHT];

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_sets[MAX_DESCRIPTOR_SET_COUNT][MAX_FRAMES_IN_FLIGHT];

    VkCommandPool transfer_command_pool;
    VkCommandBuffer transfer_command_buffer;
    Vulkan_Buffer transfer_buffer;

    Vulkan_Buffer vertex_buffer;
    U64 vertex_offset;

    Vulkan_Buffer index_buffer;
    U64 index_offset;

    U32 frames_in_flight;
    U32 current_frame_in_flight_index;
    U32 current_swapchain_image_index;

    Free_List_Allocator *allocator;

#if HE_VULKAN_DEBUGGING
    VkDebugUtilsMessengerEXT debug_messenger;
#endif
};

// todo(amer): force inline.
inline Vulkan_Image*
get_data(const Texture *texture)
{
    Vulkan_Texture_Bundle *bundle = (Vulkan_Texture_Bundle *)texture;
    return &bundle->vulkan_image;
}

inline Vulkan_Material*
get_data(const Material *material)
{
    Vulkan_Material_Bundle *bundle = (Vulkan_Material_Bundle *)material;
    return &bundle->vulkan_material;
}

inline Vulkan_Static_Mesh*
get_data(const Static_Mesh *static_mesh)
{
    Vulkan_Static_Mesh_Bundle *bundle = (Vulkan_Static_Mesh_Bundle *)static_mesh;
    return &bundle->vulkan_static_mesh;
}