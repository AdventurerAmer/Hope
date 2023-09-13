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

#define MAX_TEXTURE_COUNT 4096
#define MAX_MATERIAL_COUNT 4096
#define MAX_STATIC_MESH_COUNT 4096
#define MAX_SHADER_COUNT 4096
#define MAX_PIPELINE_STATE_COUNT 4096
#define MAX_SCENE_NODE_COUNT 4096
#define GAMMA 2.2f

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

    U32 back_buffer_width;
    U32 back_buffer_height;

    Resource_Pool< Texture > textures;
    Resource_Pool< Shader > shaders;
    Resource_Pool< Pipeline_State > pipeline_states;
    Resource_Pool< Material > materials;
    Resource_Pool< Static_Mesh > static_meshes;

    std::atomic< U32 > scene_node_count;
    Scene_Node *scene_nodes;

    Shader_Handle mesh_vertex_shader;
    Shader_Handle mesh_fragment_shader;
    Pipeline_State_Handle mesh_pipeline;

    Texture_Handle white_pixel_texture;
    Texture_Handle normal_pixel_texture;

    Scene_Data scene_data;

    struct Free_List_Allocator *transfer_allocator;

    Mutex render_commands_mutex;
};

bool pre_init_renderer_state(Renderer_State *renderer_state, struct Engine *engine);

bool init_renderer_state(Renderer_State *renderer_state, struct Engine *engine);

void deinit_renderer_state(struct Renderer *renderer, Renderer_State *renderer_state);

struct Renderer
{
    bool (*init)(struct Renderer_State *renderer_state,
                 struct Engine *engine,
                 struct Memory_Arena *arena);

    void (*wait_for_gpu_to_finish_all_work)(struct Renderer_State *renderer_state);
    void (*deinit)(struct Renderer_State *renderer_state);

    void (*on_resize)(struct Renderer_State *renderer_state, U32 width, U32 height);

    void (*begin_frame)(struct Renderer_State *renderer_state, const Scene_Data *scene_data);
    void (*submit_static_mesh)(struct Renderer_State *renderer_state, Static_Mesh_Handle static_mesh_handle, const glm::mat4 &transfom);
    void (*end_frame)(struct Renderer_State *renderer_state);

    bool (*create_texture)(Texture_Handle texture, const Texture_Descriptor &descriptor);
    void (*destroy_texture)(Texture_Handle texture);

    bool (*create_shader)(Shader_Handle shader_handle, const Shader_Descriptor &descriptor);
    void (*destroy_shader)(Shader_Handle shader_handle);

    bool (*create_pipeline_state)(Pipeline_State_Handle pipeline_state_handle, const Pipeline_State_Descriptor &descriptor);
    void (*destroy_pipeline_state)(Pipeline_State_Handle pipeline_state_handle);

    bool (*create_static_mesh)(Static_Mesh_Handle static_mesh_handle, const Static_Mesh_Descriptor &descriptor);
    void (*destroy_static_mesh)(Static_Mesh_Handle static_mesh_handle);

    bool (*create_material)(Material_Handle material_handle, const Material_Descriptor &descriptor);
    void (*destroy_material)(Material_Handle material_handle);

    void (*imgui_new_frame)();
};

bool request_renderer(RenderingAPI rendering_api, Renderer *renderer);

Scene_Node *add_child_scene_node(Renderer_State *renderer_state,
                                 Scene_Node *parent);

bool load_model(Scene_Node *root_scene_node, const String &path, Renderer *renderer, Renderer_State *renderer_state, Memory_Arena *arena);

Scene_Node* load_model(const String &path, Renderer *renderer, Renderer_State *renderer_state, Memory_Arena *arena);

Scene_Node* load_model_threaded(const String &path, Renderer *renderer, Renderer_State *renderer_state);

void render_scene_node(Renderer *renderer, Renderer_State *renderer_state, Scene_Node *scene_node, const glm::mat4 &transform);

U8 *get_property(Material *material, const String &name, ShaderDataType shader_datatype);

Texture_Handle find_texture(Renderer_State *renderer_state, const String &name);
Material_Handle find_material(Renderer_State *renderer_state, U64 hash);

inline glm::vec4 sRGB_to_linear(const glm::vec4 &color)
{
    return glm::pow(color, glm::vec4(GAMMA));
}

inline glm::vec4 linear_to_sRGB(const glm::vec4 &color)
{
    return glm::pow(color, glm::vec4(1.0f / GAMMA));
}

U32 get_size_of_shader_data_type(ShaderDataType shader_data_type);