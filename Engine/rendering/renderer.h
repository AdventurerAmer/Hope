#pragma once

#include "core/defines.h"
#include "core/platform.h"
#include "core/job_system.h"

#include "renderer_types.h"
#include "camera.h"

#include <atomic>

enum RenderingAPI
{
    RenderingAPI_Vulkan
};

#define HE_MAX_BUFFER_COUNT 4096
#define HE_MAX_TEXTURE_COUNT 4096
#define HE_MAX_SAMPLER_COUNT 4096
#define HE_MAX_MATERIAL_COUNT 4096
#define HE_MAX_RENDER_PASS_COUNT 4096
#define HE_MAX_FRAME_BUFFER_COUNT 4096
#define HE_MAX_STATIC_MESH_COUNT 4096
#define HE_MAX_SHADER_COUNT 4096
#define HE_MAX_SHADER_GROUP_COUNT 4096
#define HE_MAX_PIPELINE_STATE_COUNT 4096
#define HE_MAX_BIND_GROUP_LAYOUT_COUNT 4096
#define HE_MAX_BIND_GROUP_COUNT 4096
#define HE_MAX_SCENE_NODE_COUNT 4096

#define HE_GAMMA 2.2f

struct Directional_Light
{
    glm::vec3 direction;
    glm::vec4 color;
    F32 intensity;
};

struct Scene_Data
{
    glm::mat4 view;
    glm::mat4 projection;

    Directional_Light directional_light;
};

struct Renderer_State
{
    struct Engine *engine;

    Memory_Arena arena;
    Temprary_Memory_Arena frame_arena;

    U32 back_buffer_width;
    U32 back_buffer_height;

    Resource_Pool< Buffer > buffers;
    Resource_Pool< Texture > textures;
    Resource_Pool< Sampler > samplers;
    Resource_Pool< Shader > shaders;
    Resource_Pool< Shader_Group > shader_groups;
    Resource_Pool< Pipeline_State > pipeline_states;
    Resource_Pool< Bind_Group_Layout > bind_group_layouts;
    Resource_Pool< Bind_Group > bind_groups;
    Resource_Pool< Render_Pass > render_passes;
    Resource_Pool< Frame_Buffer > frame_buffers;
    Resource_Pool< Material > materials;
    Resource_Pool< Static_Mesh > static_meshes;

    std::atomic< U32 > scene_node_count;
    Scene_Node *scene_nodes;

    Scene_Node *root_scene_node;

    Shader_Handle mesh_vertex_shader;
    Shader_Handle mesh_fragment_shader;
    Shader_Group_Handle mesh_shader_group;
    Pipeline_State_Handle mesh_pipeline;

    Bind_Group_Handle per_frame_bind_groups[HE_MAX_FRAMES_IN_FLIGHT];
    Bind_Group_Handle per_render_pass_bind_groups[HE_MAX_FRAMES_IN_FLIGHT];

    Texture_Handle white_pixel_texture;
    Texture_Handle normal_pixel_texture;

    Sampler_Handle default_sampler;

    Buffer_Handle globals_uniform_buffers[HE_MAX_FRAMES_IN_FLIGHT];

    Buffer_Handle object_data_storage_buffers[HE_MAX_FRAMES_IN_FLIGHT];
    Object_Data *object_data_base;
    U32 object_data_count;

    Buffer_Handle transfer_buffer;

    U64 max_vertex_count;
    U64 vertex_count;
    Buffer_Handle position_buffer;
    Buffer_Handle normal_buffer;
    Buffer_Handle uv_buffer;
    Buffer_Handle tangent_buffer;

    Buffer_Handle index_buffer;
    U64 index_offset;

    Scene_Data scene_data;

    Free_List_Allocator transfer_allocator;

    U32 frames_in_flight;
    U32 current_frame_in_flight_index;

    Mutex render_commands_mutex;
};

bool pre_init_renderer_state(Renderer_State *renderer_state, struct Engine *engine);

bool init_renderer_state(Renderer_State *renderer_state, struct Engine *engine);

void deinit_renderer_state(struct Renderer *renderer, Renderer_State *renderer_state);

struct Renderer
{
    bool (*init)(struct Engine *engine);
    void (*deinit)();

    void (*wait_for_gpu_to_finish_all_work)();

    void (*on_resize)(U32 width, U32 height);

    void (*begin_frame)(const Scene_Data *scene_data);
    void (*set_vertex_buffers)(Buffer_Handle *vertex_buffer_handles, U64 *offsets, U32 count);
    void (*set_index_buffer)(Buffer_Handle index_buffer_handle, U64 offset);
    void (*submit_static_mesh)(Static_Mesh_Handle static_mesh_handle, const glm::mat4 &transfom);
    void (*end_frame)();

    bool (*create_buffer)(Buffer_Handle buffer_handle, const Buffer_Descriptor &descriptor);
    void (*destroy_buffer)(Buffer_Handle buffer_handle);

    bool (*create_texture)(Texture_Handle texture_handle, const Texture_Descriptor &descriptor);
    void (*destroy_texture)(Texture_Handle texture_handle);

    bool (*create_sampler)(Sampler_Handle sampler_handle, const Sampler_Descriptor &descriptor);
    void (*destroy_sampler)(Sampler_Handle sampler_handle);

    bool (*create_shader)(Shader_Handle shader_handle, const Shader_Descriptor &descriptor);
    void (*destroy_shader)(Shader_Handle shader_handle);

    bool (*create_shader_group)(Shader_Group_Handle shader_group_handle, const Shader_Group_Descriptor &descriptor);
    void (*destroy_shader_group)(Shader_Group_Handle shader_group_handle);

    bool (*create_pipeline_state)(Pipeline_State_Handle pipeline_state_handle, const Pipeline_State_Descriptor &descriptor);
    void (*destroy_pipeline_state)(Pipeline_State_Handle pipeline_state_handle);

    bool (*create_bind_group_layout)(Bind_Group_Layout_Handle bind_group_layout_handle, const Bind_Group_Layout_Descriptor &descriptor);
    void (*destroy_bind_group_layout)(Bind_Group_Layout_Handle bind_group_layout_handle);

    bool (*create_bind_group)(Bind_Group_Handle bind_group_handle, const Bind_Group_Descriptor &descriptor);
    void (*update_bind_group)(Bind_Group_Handle bind_group_handle, const Update_Binding_Descriptor *update_binding_descriptors, U32 update_binding_descriptor_count);
    void (*set_bind_groups)(U32 first_bind_group, Bind_Group_Handle *bind_group_handles, U32 count, Shader_Group_Handle shader_group);
    void (*destroy_bind_group)(Bind_Group_Handle bind_group_handle);

    bool (*create_static_mesh)(Static_Mesh_Handle static_mesh_handle, const Static_Mesh_Descriptor &descriptor);
    void (*destroy_static_mesh)(Static_Mesh_Handle static_mesh_handle);

    void (*imgui_new_frame)();
};

bool request_renderer(RenderingAPI rendering_api, Renderer *renderer);

Scene_Node *add_child_scene_node(Renderer_State *renderer_state, Scene_Node *parent);

bool load_model(Scene_Node *root_scene_node, const String &path, Renderer *renderer, Renderer_State *renderer_state, Memory_Arena *arena);

Scene_Node* load_model_threaded(const String &path, Renderer *renderer, Renderer_State *renderer_state);

void render_scene_node(Renderer *renderer, Renderer_State *renderer_state, Scene_Node *scene_node, const glm::mat4 &transform);

Material_Handle create_material(Renderer_State *renderer_state, Renderer *renderer, const Material_Descriptor &descriptor);
void destroy_material(Material_Handle material_handle);

U8 *get_property(Material *material, const String &name, Shader_Data_Type data_type);

Texture_Handle find_texture(Renderer_State *renderer_state, const String &name);
Material_Handle find_material(Renderer_State *renderer_state, U64 hash);

HE_FORCE_INLINE glm::vec4 srgb_to_linear(const glm::vec4 &color)
{
    return glm::pow(color, glm::vec4(HE_GAMMA));
}

HE_FORCE_INLINE glm::vec4 linear_to_srgb(const glm::vec4 &color)
{
    return glm::pow(color, glm::vec4(1.0f / HE_GAMMA));
}

U32 get_size_of_shader_data_type(Shader_Data_Type data_type);