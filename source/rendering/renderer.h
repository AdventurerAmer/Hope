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

struct Renderer_State
{
    U32 back_buffer_width;
    U32 back_buffer_height;

    Camera camera;
    FPS_Camera_Controller camera_controller;

    Static_Mesh static_mesh;
};

struct Scene_Data
{
    glm::mat4 view;
    glm::mat4 projection;
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
    void (*submit_static_mesh)(struct Renderer_State *renderer_state, struct Static_Mesh *mesh);
    void (*end_frame)(struct Renderer_State *renderer_state);

    bool (*create_texture)(Texture *texture, U32 width, U32 height,
                           void *data, TextureFormat format);

    void (*destroy_texture)(Texture *texture);

    bool (*create_static_mesh)(Static_Mesh *static_mesh, void *vertices, U32 vertex_count, U16 *indices, U32 index_count);
    void (*destroy_static_mesh)(Static_Mesh *static_mesh);
};

bool request_renderer(RenderingAPI rendering_api, Renderer *renderer);
bool load_static_mesh(Static_Mesh *static_mesh, const char *path, Renderer *renderer, Memory_Arena *arena);