#pragma once

#include "core/defines.h"
#include "renderer_types.h"
#include "camera.h"

#if HE_OS_WINDOWS
#define HE_RHI_VULKAN
#endif

enum RenderingAPI
{
    RenderingAPI_Vulkan
};

#define MAX_TEXTURE_COUNT 4096
#define MAX_MATERIAL_COUNT 4096
#define MAX_STATIC_MESH_COUNT 4096
#define MAX_SCENE_NODE_COUNT 4096

struct Renderer_State
{
    U32 back_buffer_width;
    U32 back_buffer_height;

    Camera camera;
    FPS_Camera_Controller camera_controller;

    U64 texture_bundle_size;
    U32 texture_count;
    U8 *textures;

    U64 material_bundle_size;
    U32 material_count;
    U8 *materials;

    U64 static_mesh_bundle_size;
    U32 static_mesh_count;
    U8 *static_meshes;

    U32 scene_node_count;
    Scene_Node *scene_nodes;

    Scene_Node *sponza;
    Scene_Node *flight_helmet;
};

bool init_renderer_state(Renderer_State *renderer_state, struct Memory_Arena *arena);

struct Scene_Data
{
    glm::mat4 view;
    glm::mat4 projection;

    glm::vec3 directional_light_direction;
    glm::vec4 directional_light_color;
};

struct Renderer
{
    bool (*init)(struct Renderer_State *renderer_state,
                 struct Engine *engine,
                 struct Memory_Arena *arena);

    void (*wait_for_gpu_to_finish_all_work)(struct Renderer_State *renderer_state);
    void (*deinit)(struct Renderer_State *renderer_state);

    void (*on_resize)(struct Renderer_State *renderer_state, U32 width, U32 height);

    void (*begin_frame)(struct Renderer_State *renderer_state, const Scene_Data *scene_data);
    void (*submit_static_mesh)(struct Renderer_State *renderer_state, const struct Static_Mesh *mesh, const glm::mat4 transfom);
    void (*end_frame)(struct Renderer_State *renderer_state);

    bool (*create_texture)(Texture *texture, U32 width, U32 height,
                           void *data, TextureFormat format, bool mipmaping);

    void (*destroy_texture)(Texture *texture);

    bool (*create_static_mesh)(Static_Mesh *static_mesh, void *vertices, U16 vertex_count, U16 *indices, U32 index_count);
    void (*destroy_static_mesh)(Static_Mesh *static_mesh);

    bool (*create_material)(Material *material, U32 albedo_texture_index, U32 normal_texture_index);
    void (*destroy_material)(Material *material);
};

bool request_renderer(RenderingAPI rendering_api, Renderer *renderer);

Scene_Node *add_child_scene_node(Renderer_State *renderer_state,
                                 Scene_Node *parent);

Scene_Node*
load_model(const char *path, Renderer *renderer,
           Renderer_State *renderer_state,
           Memory_Arena *arena);

void render_scene_node(Renderer *renderer, Renderer_State *renderer_state, Scene_Node *scene_node, glm::mat4 transform);

Texture *allocate_texture(Renderer_State *renderer_state);
Material *allocate_material(Renderer_State *renderer_state);
Static_Mesh *allocate_static_mesh(Renderer_State *renderer_state);

Texture *get_texture(Renderer_State *renderer_state, U32 index);
Material *get_material(Renderer_State *renderer_state, U32 index);
Static_Mesh *get_static_mesh(Renderer_State *renderer_state, U32 index);

U32 index_of(Renderer_State *renderer_state, Texture *texture);
U32 index_of(Renderer_State *renderer_state, Material *material);
U32 index_of(Renderer_State *renderer_state, Static_Mesh *static_mesh);

S32 find_texture(Renderer_State *renderer_state, char *name, U32 length);
S32 find_material(Renderer_State *renderer_state, U64 hash);