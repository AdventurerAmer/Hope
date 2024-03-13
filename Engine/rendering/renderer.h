#pragma once

#include "core/defines.h"
#include "core/platform.h"
#include "core/job_system.h"

#include "rendering/renderer_types.h"
#include "rendering/camera.h"
#include "rendering/render_graph.h"

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
#define HE_MAX_SEMAPHORE_COUNT 4096
#define HE_MAX_SCENE_COUNT 4096
#define HE_MAX_UPLOAD_REQUEST_COUNT 4096

struct Directional_Light
{
    glm::vec3 direction;
    glm::vec3 color;
    F32 intensity;
};

struct Point_Light
{
    glm::vec3 position;
    float radius;

    glm::vec3 color;
    float intensity;
};

struct Spot_Light
{
    glm::vec3 position;
    glm::vec3 direction;
    F32 outer_angle;
    F32 inner_angle;
    float radius;

    glm::vec3 color;
    float intensity;
};

struct Scene_Data
{
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 eye;
    Directional_Light directional_light;
    Point_Light point_light;
    Spot_Light spot_light;

    Texture_Handle skybox;
    Material_Handle skybox_material_handle;
};

struct Renderer
{
    bool (*init)(struct Engine *engine, struct Renderer_State *renderer_state);
    void (*deinit)();

    void (*wait_for_gpu_to_finish_all_work)();

    void (*on_resize)(U32 width, U32 height);

    void (*begin_frame)();
    void (*set_viewport)(U32 width, U32 height);
    void (*set_vertex_buffers)(const Array_View< Buffer_Handle > &vertex_buffer_handles, const Array_View< U64 > &offsets);
    void (*set_index_buffer)(Buffer_Handle index_buffer_handle, U64 offset);
    void (*set_pipeline_state)(Pipeline_State_Handle pipeline_state_handle);
    void (*set_bind_groups)(U32 first_bind_group, const Array_View< Bind_Group_Handle > &bind_group_handles);
    void (*draw_static_mesh)(Static_Mesh_Handle static_mesh_handle, U32 first_instance);
    void (*draw_sub_mesh)(Static_Mesh_Handle static_mesh_handle, U32 first_instance, U32 sub_mesh_index);
    void (*end_frame)();

    bool (*create_buffer)(Buffer_Handle buffer_handle, const Buffer_Descriptor &descriptor);
    void (*destroy_buffer)(Buffer_Handle buffer_handle);

    bool (*create_texture)(Texture_Handle texture_handle, const Texture_Descriptor &descriptor, Upload_Request_Handle upload_request_handle);
    void (*destroy_texture)(Texture_Handle texture_handle);

    bool (*create_sampler)(Sampler_Handle sampler_handle, const Sampler_Descriptor &descriptor);
    void (*destroy_sampler)(Sampler_Handle sampler_handle);

    bool (*create_shader)(Shader_Handle shader_handle, const Shader_Descriptor &descriptor);
    void (*destroy_shader)(Shader_Handle shader_handle);

    bool (*create_pipeline_state)(Pipeline_State_Handle pipeline_state_handle, const Pipeline_State_Descriptor &descriptor);
    void (*destroy_pipeline_state)(Pipeline_State_Handle pipeline_state_handle);
    
    void (*update_bind_group)(Bind_Group_Handle bind_group_handle, const Array_View< Update_Binding_Descriptor > &update_binding_descriptors);

    bool (*create_render_pass)(Render_Pass_Handle render_pass_handle, const Render_Pass_Descriptor &descriptor);
    void (*begin_render_pass)(Render_Pass_Handle render_pass_handle, Frame_Buffer_Handle frame_buffer_handle, const Array_View< Clear_Value > &clear_views);
    void (*end_render_pass)(Render_Pass_Handle render_pass_handle);
    void (*destroy_render_pass)(Render_Pass_Handle render_pass_handle);

    bool (*create_frame_buffer)(Frame_Buffer_Handle frame_buffer_handle, const Frame_Buffer_Descriptor &descriptor);
    void (*destroy_frame_buffer)(Frame_Buffer_Handle frame_buffer_handle);

    bool (*create_static_mesh)(Static_Mesh_Handle static_mesh_handle, const Static_Mesh_Descriptor &descriptor, Upload_Request_Handle upload_request_handle);
    void (*destroy_static_mesh)(Static_Mesh_Handle static_mesh_handle);

    bool (*create_semaphore)(Semaphore_Handle semaphore_handle, const Renderer_Semaphore_Descriptor &descriptor);
    U64 (*get_semaphore_value)(Semaphore_Handle semaphore_handle);
    void (*destroy_semaphore)(Semaphore_Handle semaphore_handle);

    void (*destroy_upload_request)(Upload_Request_Handle upload_request_handle);

    void (*set_vsync)(bool enabled);

    Memory_Requirements (*get_texture_memory_requirements)(const Texture_Descriptor &descriptor);

    bool (*init_imgui)();
    void (*imgui_new_frame)();
    void (*imgui_render)();
};

struct Light
{
    glm::vec3 position;
    alignas(16) glm::vec3 direction;
    alignas(4) F32 radius;
    alignas(4) F32 outer_angle;
    alignas(4) F32 inner_angle;
    alignas(16) glm::vec3 color;
};

struct Renderer_State
{
    struct Engine *engine;
    
    Renderer renderer;
    Mutex render_commands_mutex;

    U32 back_buffer_width;
    U32 back_buffer_height;

    Resource_Pool< Buffer > buffers;
    Resource_Pool< Texture > textures;
    Resource_Pool< Sampler > samplers;
    Resource_Pool< Shader > shaders;
    Resource_Pool< Pipeline_State > pipeline_states;
    Resource_Pool< Bind_Group > bind_groups;
    Resource_Pool< Render_Pass > render_passes;
    Resource_Pool< Frame_Buffer > frame_buffers;
    Resource_Pool< Renderer_Semaphore > semaphores;
    Resource_Pool< Material > materials;
    Resource_Pool< Static_Mesh > static_meshes;
    Resource_Pool< Scene > scenes;
    Resource_Pool< Upload_Request > upload_requests;

    Mutex pending_upload_requests_mutex;
    Array< Upload_Request_Handle, HE_MAX_UPLOAD_REQUEST_COUNT > pending_upload_requests;

    Mutex root_scene_node_mutex;
    Scene_Node root_scene_node;

    Pipeline_State_Handle current_pipeline_state_handle;

    U32 opaque_packet_count;
    Render_Packet *opaque_packets;

    F32 gamma;
    bool triple_buffering;
    bool vsync;
    MSAA_Setting msaa_setting;
    Anisotropic_Filtering_Setting anisotropic_filtering_setting;
    
    Bind_Group_Handle per_frame_bind_groups[HE_MAX_FRAMES_IN_FLIGHT];
    Bind_Group_Handle per_render_pass_bind_groups[HE_MAX_FRAMES_IN_FLIGHT];

    Buffer_Handle globals_uniform_buffers[HE_MAX_FRAMES_IN_FLIGHT];

    Buffer_Handle object_data_storage_buffers[HE_MAX_FRAMES_IN_FLIGHT];
    Object_Data *object_data_base;
    U32 object_data_count;

    Buffer_Handle transfer_buffer;
    Free_List_Allocator transfer_allocator;

    U32 frames_in_flight;
    U32 current_frame_in_flight_index;

    Shader_Handle default_shader;
    Pipeline_State_Handle default_pipeline;
    Material_Handle default_material;

    Texture_Handle white_pixel_texture;
    Texture_Handle normal_pixel_texture;

    Sampler_Handle default_texture_sampler;
    Sampler_Handle default_cubemap_sampler;
    
    Static_Mesh_Handle default_static_mesh;

    Scene_Handle default_scene;

    Scene_Data scene_data;
    Render_Graph render_graph;

    bool imgui_docking;
};

struct Render_Context
{
    Renderer *renderer;
    Renderer_State *renderer_state;
};

bool request_renderer(RenderingAPI rendering_api, Renderer *renderer);
bool init_renderer_state(struct Engine *engine);
void deinit_renderer_state();

void renderer_on_resize(U32 width, U32 height);
void renderer_wait_for_gpu_to_finish_all_work();

//
// Buffers
//

Buffer_Handle renderer_create_buffer(const Buffer_Descriptor &descriptor);
Buffer *renderer_get_buffer(Buffer_Handle buffer_handle);
void renderer_destroy_buffer(Buffer_Handle &buffer_handle);

//
// Textures
//

Texture_Handle renderer_create_texture(const Texture_Descriptor &descriptor);
Texture* renderer_get_texture(Texture_Handle texture_handle);
void renderer_destroy_texture(Texture_Handle &texture_handle);

//
// Samplers
//

Sampler_Handle renderer_create_sampler(const Sampler_Descriptor &descriptor);
Sampler* renderer_get_sampler(Sampler_Handle sampler_handle);
void renderer_destroy_sampler(Sampler_Handle &sampler_handle);

//
// Shaders
//

Shader_Compilation_Result renderer_compile_shader(String source, String path = HE_STRING_LITERAL(""));
void renderer_destroy_shader_compilation_result(Shader_Compilation_Result *result);

Shader_Handle renderer_create_shader(const Shader_Descriptor &descriptor);
Shader* renderer_get_shader(Shader_Handle shader_handle);
Shader_Struct *renderer_find_shader_struct(Shader_Handle shader_handle, String name);
void renderer_destroy_shader(Shader_Handle &shader_handle);

//
// Bind Groups
//
Bind_Group_Handle renderer_create_bind_group(const Bind_Group_Descriptor &descriptor);
Bind_Group* renderer_get_bind_group(Bind_Group_Handle bind_group_handle);
void renderer_update_bind_group(Bind_Group_Handle bind_group_handle, const Array_View< Update_Binding_Descriptor > &update_binding_descriptors);
void renderer_destroy_bind_group(Bind_Group_Handle &bind_group_handle);

//
// Pipeline States
//

Pipeline_State_Handle renderer_create_pipeline_state(const Pipeline_State_Descriptor &descriptor);
Pipeline_State* renderer_get_pipeline_state(Pipeline_State_Handle pipeline_state_handle);
void renderer_destroy_pipeline_state(Pipeline_State_Handle &pipeline_state_handle);

//
// Render Passes
//

Render_Pass_Handle renderer_create_render_pass(const Render_Pass_Descriptor &descriptor);
Render_Pass* renderer_get_render_pass(Render_Pass_Handle render_pass_handle);
void renderer_destroy_render_pass(Render_Pass_Handle &render_pass_handle);

//
// Frame Buffers
//

Frame_Buffer_Handle renderer_create_frame_buffer(const Frame_Buffer_Descriptor &descriptor);
Frame_Buffer* renderer_get_frame_buffer(Frame_Buffer_Handle frame_buffer_handle);
void renderer_destroy_frame_buffer(Frame_Buffer_Handle &frame_buffer_handle);

//
// Semaphores
//

Semaphore_Handle renderer_create_semaphore(const Renderer_Semaphore_Descriptor &descriptor);
Renderer_Semaphore *renderer_get_semaphore(Semaphore_Handle semaphore_handle);
U64 renderer_get_semaphore_value(Semaphore_Handle semaphore_handle);
void renderer_destroy_semaphore(Semaphore_Handle &semaphore_handle);

//
// Static Meshes
//
Static_Mesh_Handle renderer_create_static_mesh(const Static_Mesh_Descriptor &descriptor);
Static_Mesh* renderer_get_static_mesh(Static_Mesh_Handle static_mesh_handle);
void renderer_destroy_static_mesh(Static_Mesh_Handle &static_mesh_handle);

//
// Materials
//

Shader_Struct *find_material_properties(Array_View<Shader_Handle> shaders);

Material_Handle renderer_create_material(const Material_Descriptor &descriptor);
void renderer_use_material(Material_Handle material_handle);
Material* renderer_get_material(Material_Handle material_handle);
void renderer_destroy_material(Material_Handle &material_handle);

S32 find_property(Material_Handle material_handle, const char *name);
bool set_property(Material_Handle material_handle, const char *name, Material_Property_Data data);
bool set_property(Material_Handle material_handle, S32 property_id, Material_Property_Data data);

//
// Scenes
//

Scene_Handle renderer_create_scene(U32 node_capacity, U32 node_count = 0);
Scene *renderer_get_scene(Scene_Handle scene_handle);
void renderer_destroy_scene(Scene_Handle &scene_handle);

Transform get_identity_transform();
Transform combine(const Transform &a, const Transform &b);
glm::mat4 get_world_matrix(const Transform &transform);

void add_child(Scene_Node *parent, Scene_Node *node);
void remove_child(Scene_Node *parent, Scene_Node *node);
void renderer_parse_scene_tree(Scene_Node *scene_node, const Transform &parent_transform = get_identity_transform());


//
// Upload Request
//

Upload_Request_Handle renderer_create_upload_request(const Upload_Request_Descriptor &descriptor);
Upload_Request *renderer_get_upload_request(Upload_Request_Handle upload_request_handle);
void renderer_destroy_upload_request(Upload_Request_Handle upload_request_handle);
void renderer_add_pending_upload_request(Upload_Request_Handle upload_request_handle);
void renderer_handle_upload_requests();

//
// Render Context
//
Render_Context get_render_context();

//
// Settings
//

void renderer_set_anisotropic_filtering(Anisotropic_Filtering_Setting anisotropic_filtering_setting);
void renderer_set_msaa(MSAA_Setting msaa_setting);
void renderer_set_vsync(bool enabled);
void renderer_set_triple_buffering(bool enabled);

//
// ImGui
//

bool init_imgui(Engine *engine);
void imgui_new_frame();