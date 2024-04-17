#pragma once

#include "core/defines.h"
#include "containers/array.h"
#include "containers/counted_array.h"
#include "containers/dynamic_array.h"
#include "containers/string.h"
#include "containers/resource_pool.h"

#include "../data/shaders/common.glsl"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#define HE_GRAPHICS_DEBUGGING 1
#define HE_MAX_FRAMES_IN_FLIGHT 3
#define HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT UINT16_MAX
#define HE_MAX_BIND_GROUP_INDEX_COUNT 4
#define HE_MAX_ATTACHMENT_COUNT 16
#define HE_MAX_SHADER_COUNT_PER_PIPELINE 8
#define HE_MAX_LIGHT_COUNT_PER_SCENE 1024

#ifdef HE_SHIPPING
#undef HE_GRAPHICS_DEBUGGING
#define HE_GRAPHICS_DEBUGGING 0
#endif

struct Memory_Requirements
{
    U64 size;
    U64 alignment;
};

struct Transform
{
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 euler_angles;
    glm::vec3 scale;
};

//
// Semaphore
//

struct Renderer_Semaphore_Descriptor
{
    U64 initial_value = 0;
};

struct Renderer_Semaphore
{
    Renderer_Semaphore_Descriptor descriptor;
};

using Semaphore_Handle = Resource_Handle< Renderer_Semaphore >;

//
// Buffer
//

enum class Buffer_Usage : U8
{
    TRANSFER,
    VERTEX,
    INDEX,
    UNIFORM,
    STORAGE_CPU_SIDE,
    STORAGE_GPU_SIDE
};

struct Buffer_Descriptor
{
    U64 size;
    Buffer_Usage usage;
};

struct Buffer
{
    String name;

    U64 size;
    Buffer_Usage usage;
    void *data;
};

using Buffer_Handle = Resource_Handle< Buffer >;

//
// Texture
//
enum class Texture_Format
{
    R8G8B8A8_UNORM,
    R8G8B8_UNORM,
    R8G8B8A8_SRGB,
    B8G8R8A8_SRGB,
    B8G8R8A8_UNORM,
    R32_SINT,
    DEPTH_F32_STENCIL_U8,
    COUNT
};

struct Texture
{
    String name;

    U32 width;
    U32 height;

    Texture_Format format;
    U32 sample_count;

    U64 size;
    U64 alignment;
    
    bool is_attachment;
    bool is_cubemap;
    bool is_uploaded_to_gpu;
    Resource_Handle< Texture > alias;
};

using Texture_Handle = Resource_Handle< Texture >;

struct Texture_Descriptor
{
    String name;
    U32 width = 1;
    U32 height = 1;
    Texture_Format format = Texture_Format::R8G8B8A8_UNORM;
    U32 layer_count = 1;
    Array_View< void * > data_array;
    bool mipmapping = false;
    U32 sample_count = 1;
    bool is_attachment = false;
    bool is_cubemap = false;
    Texture_Handle alias = Resource_Pool< Texture >::invalid_handle; 
};

//
// Sampler
//

enum class Filter : U8
{
    NEAREST,
    LINEAR
};

enum class Address_Mode : U8
{
    REPEAT,
    CLAMP
};

struct Sampler_Descriptor
{
    Address_Mode address_mode_u = Address_Mode::REPEAT;
    Address_Mode address_mode_v = Address_Mode::REPEAT;
    Address_Mode address_mode_w = Address_Mode::REPEAT;

    Filter min_filter = Filter::NEAREST;
    Filter mag_filter = Filter::NEAREST;
    Filter mip_filter = Filter::NEAREST;

    U32 anisotropy = 16;
};

struct Sampler
{
    String name;
    Sampler_Descriptor descriptor;
};

using Sampler_Handle = Resource_Handle< Sampler >;

//
// Render Pass
//

struct Clear_Value
{
    glm::vec4 color;
    glm::ivec4 icolor;
    glm::uvec4 ucolor;
    F32 depth;
    U8 stencil;
};

enum class Attachment_Operation : U8
{
    DONT_CARE,
    LOAD,
    CLEAR
};

struct Attachment_Info
{
    Texture_Format format;
    U32 sample_count = 1;
    Attachment_Operation operation = Attachment_Operation::DONT_CARE;
};

struct Render_Pass_Descriptor
{
    String name;
    
    Counted_Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > color_attachments;
    Counted_Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > depth_stencil_attachments;
    Counted_Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > resolve_attachments;
};

struct Render_Pass
{
    String name;

    Counted_Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > color_attachments;
    Counted_Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > depth_stencil_attachments;
    Counted_Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > resolve_attachments;
};

using Render_Pass_Handle = Resource_Handle< Render_Pass >;

// Frame Buffer

struct Frame_Buffer_Descriptor
{
    U32 width;
    U32 height;

    Counted_Array< Texture_Handle, HE_MAX_ATTACHMENT_COUNT > attachments;

    Render_Pass_Handle render_pass;
};

struct Frame_Buffer
{
    U32 width;
    U32 height;

    Counted_Array< Texture_Handle, HE_MAX_ATTACHMENT_COUNT > attachments;

    Render_Pass_Handle render_pass;
};

using Frame_Buffer_Handle = Resource_Handle< Frame_Buffer >;

//
// Shader
//

enum class Shader_Data_Type
{
    NONE,

    S8,
    S16,
    S32,
    S64,
    U8,
    U16,
    U32,
    U64,
    F16,
    F32,
    F64,

    VECTOR2F,
    VECTOR3F,
    VECTOR4F,

    VECTOR2S,
    VECTOR3S,
    VECTOR4S,

    VECTOR2U,
    VECTOR3U,
    VECTOR4U,

    MATRIX3F,
    MATRIX4F,

    STRUCT
};

struct Shader_Struct_Member
{
    String name;

    Shader_Data_Type data_type;

    U32 offset;
    U32 size;
};

struct Shader_Struct
{
    String name;
    
    U64 size;

    U32 member_count;
    Shader_Struct_Member *members;
};

enum class Shader_Stage : U8
{
    VERTEX,
    FRAGMENT,
    COUNT
};

struct Shader_Compilation_Result
{
    bool success;
    String stages[(U32)Shader_Stage::COUNT];
};

struct Shader_Descriptor
{
    String name;
    const Shader_Compilation_Result *compilation_result;
};

struct Shader
{
    String name;

    U32 struct_count;
    Shader_Struct *structs;
};

using Shader_Handle = Resource_Handle< Shader >;

//
// Bind Group
//

struct Update_Binding_Descriptor
{
    U32 binding_number;

    U32 element_index;
    U32 count;

    Buffer_Handle *buffers;
    Texture_Handle *textures;
    Sampler_Handle *samplers;
};

struct Bind_Group_Descriptor
{
    Shader_Handle shader;
    U32 group_index;
};

struct Bind_Group
{
    Shader_Handle shader;
    U32 group_index;
};

using Bind_Group_Handle = Resource_Handle< Bind_Group >;

//
// Pipeline State
//

enum class Cull_Mode : U8
{
    NONE,
    FRONT,
    BACK,
};

enum class Front_Face : U8
{
    CLOCKWISE,
    COUNTER_CLOCKWISE,
};

enum class Fill_Mode : U8
{
    SOLID,
    WIREFRAME
};

enum class Compare_Operation
{
    NEVER,
    LESS,
    EQUAL,
    LESS_OR_EQUAL,
    GREATER,
    NOT_EQUAL,
    GREATER_OR_EQUAL,
    ALWAYS,
};

enum class Stencil_Operation
{
    KEEP,
    ZERO,
    REPLACE,
    INCREMENT_AND_CLAMP,
    DECREMENT_AND_CLAMP,
    INVERT,
    INCREMENT_AND_WRAP,
    DECREMENT_AND_WRAP,
};

struct Pipeline_State_Settings
{
    Cull_Mode cull_mode = Cull_Mode::BACK;
    Front_Face front_face = Front_Face::COUNTER_CLOCKWISE;
    Fill_Mode fill_mode = Fill_Mode::SOLID;

    Compare_Operation depth_operation = Compare_Operation::LESS_OR_EQUAL;
    bool depth_testing = true;
    bool depth_writing = false;

    Compare_Operation stencil_operation = Compare_Operation::ALWAYS;
    Stencil_Operation stencil_fail = Stencil_Operation::KEEP;
    Stencil_Operation stencil_pass = Stencil_Operation::KEEP;
    Stencil_Operation depth_fail = Stencil_Operation::KEEP;

    U32 stencil_compare_mask = 0xff;
    U32 stencil_write_mask = 0xff;
    U32 stencil_reference_value = 0;
    bool stencil_testing = false;

    bool sample_shading = false;
};

struct Pipeline_State_Descriptor
{
    Pipeline_State_Settings settings;
    
    Shader_Handle shader;
    Render_Pass_Handle render_pass;
};

struct Pipeline_State
{
    String name;
    Pipeline_State_Descriptor descriptor;
};

using Pipeline_State_Handle = Resource_Handle< Pipeline_State >;

//
// Material
//

struct Material_Descriptor
{
    String name;
    Pipeline_State_Handle pipeline_state_handle;
};

union Material_Property_Data
{
    U8  u8;
    U16 u16;
    U32 u32;
    U64 u64;

    S8  s8;
    S16 s16;
    S32 s32;
    S64 s64;

    F32 f32;
    F32 f64;

    glm::vec2 v2f;
    glm::vec3 v3f;
    glm::vec4 v4f;

    glm::ivec2 v2s;
    glm::ivec3 v3s;
    glm::ivec4 v4s;

    glm::uvec2 v2u;
    glm::uvec3 v3u;
    glm::uvec4 v4u;
};

struct Material_Property
{
    String name;

    Shader_Data_Type data_type;
    Material_Property_Data data;

    U64 offset_in_buffer;
    bool is_texture_asset;
    bool is_color;
};

struct Material
{
    String name;

    Pipeline_State_Handle pipeline_state_handle;

    Dynamic_Array< Material_Property > properties;

    U8 *data;
    U64 size;

    Array< Buffer_Handle, HE_MAX_FRAMES_IN_FLIGHT > buffers;
    Array< Bind_Group_Handle, HE_MAX_FRAMES_IN_FLIGHT > bind_groups;

    U32 dirty_count;
};

using Material_Handle = Resource_Handle< Material >;

//
// Mesh
//

struct Sub_Mesh
{
    U16 vertex_count;
    U32 index_count;

    U32 vertex_offset;
    U32 index_offset;

    U64 material_asset;
};

struct Static_Mesh_Descriptor
{
    String name;

    Array_View< void * > data_array;

    U16 *indices;
    U32 index_count;

    U32 vertex_count;
    glm::vec3 *positions;
    glm::vec3 *normals;
    glm::vec2 *uvs;
    glm::vec4 *tangents;

    Dynamic_Array< Sub_Mesh > sub_meshes;
};

struct Static_Mesh
{
    String name;

    bool is_uploaded_to_gpu;

    Buffer_Handle indices_buffer;
    Buffer_Handle positions_buffer;
    Buffer_Handle normals_buffer;
    Buffer_Handle uvs_buffer;
    Buffer_Handle tangents_buffer;

    U32 vertex_count;
    U32 index_count;

    Dynamic_Array< Sub_Mesh > sub_meshes;
};

using Static_Mesh_Handle = Resource_Handle< Static_Mesh >;

struct Model
{
    String name;

    U32 node_count;
    struct Scene_Node *nodes;
};

//
// Scene
//

enum class Skybox_Face : U8
{
    RIGHT,
    LEFT,
    TOP,
    BOTTOM,
    FRONT,
    BACK,
    COUNT
};

struct Skybox
{
    glm::vec3 ambient_color;
    U64 skybox_material_asset;
};

enum class Light_Type : U8
{
    DIRECTIONAL = 0,
    POINT = 1,
    SPOT = 2
};

struct Static_Mesh_Component
{
    U64 static_mesh_asset;
};

struct Light_Component
{
    Light_Type type;

    glm::vec3 color;
    F32 intensity;

    F32 radius;

    F32 inner_angle;
    F32 outer_angle;
};

struct Scene_Node
{
    String name;

    S32 parent_index;
    S32 first_child_index;
    S32 last_child_index;
    S32 next_sibling_index;
    S32 prev_sibling_index;

    Transform transform;

    bool has_mesh;
    Static_Mesh_Component mesh;

    bool has_light;
    Light_Component light;
};

struct Scene
{
    Skybox skybox;
    U32 node_count;
    Dynamic_Array< Scene_Node > nodes;
    S32 first_free_node_index;
};

using Scene_Handle = Resource_Handle< Scene >;

struct Draw_Command
{
    Static_Mesh_Handle static_mesh;
    U16 sub_mesh_index;
    U32 instance_index;
    Material_Handle material;
};

//
// Uploads
//

#define HE_MAX_UPLOAD_REQUEST_ALLOCATION_COUNT 8

struct Upload_Request_Descriptor
{
    String name;
    bool *is_uploaded;
};

struct Upload_Request
{
    String name;
    Semaphore_Handle semaphore;
    U64 target_value;
    Counted_Array< void*, HE_MAX_UPLOAD_REQUEST_ALLOCATION_COUNT > allocations_in_transfer_buffer;
    bool *uploaded;
    Texture_Handle texture;
};

using Upload_Request_Handle = Resource_Handle< Upload_Request >;

//
// Shader Structs
//

struct Shader_Instance_Data
{
    glm::mat4 model;
};

struct Shader_Light
{
    U32 type;
    alignas(16) glm::vec3 direction;
    alignas(16) glm::vec3 position;
    alignas(4) F32 radius;
    alignas(4) F32 outer_angle;
    alignas(4) F32 inner_angle;
    alignas(16) glm::vec3 color;
};

//
// Settings
//

enum class Anisotropic_Filtering_Setting : U8
{
    NONE,
    X2,
    X4,
    X8,
    X16
};

enum class MSAA_Setting : U8
{
    NONE,
    X2,
    X4,
    X8
};