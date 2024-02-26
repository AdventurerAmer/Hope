#pragma once

#include "core/defines.h"
#include "containers/array.h"
#include "containers/dynamic_array.h"
#include "containers/string.h"
#include "containers/resource_pool.h"

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
#define HE_MAX_DESCRIPTOR_SET_COUNT 4
#define HE_MAX_ATTACHMENT_COUNT 16
#define HE_MAX_SHADER_COUNT_PER_PIPELINE 8
#define HE_MAX_OBJECT_DATA_COUNT UINT16_MAX
#define HE_PIPELINE_CACHE_FILENAME "shaders/bin/pipeline.cache"
#define HE_PER_FRAME_BIND_GROUP_INDEX 0
#define HE_PER_PASS_BIND_GROUP_INDEX 1
#define HE_PER_MATERIAL_BIND_GROUP_INDEX 2

#ifdef HE_SHIPPING
#undef HE_GRAPHICS_DEBUGGING
#define HE_GRAPHICS_DEBUGGING 0
#endif

struct Memory_Requirements
{
    U64 size;
    U64 alignment;
};

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
    R8G8B8A8_SRGB,
    B8G8R8A8_SRGB,
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

    bool is_uploaded_to_gpu;
    bool is_attachment;
    bool is_cubemap;
    Resource_Handle< Texture > alias;
};

using Texture_Handle = Resource_Handle< Texture >;

struct Texture_Descriptor
{
    U32 width = 1;
    U32 height = 1;
    Texture_Format format = Texture_Format::B8G8R8A8_SRGB;
    int layer_count = 1;
    Array_View< void * > data;
    bool mipmapping = false;
    U32 sample_count = 1;
    bool is_attachment = false;
    bool is_cubemap = false;
    Texture_Handle alias = Resource_Pool< Texture >::invalid_handle;
    struct Allocation_Group *allocation_group = nullptr;
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
// Bind Group Layout
//

enum class Binding_Type : U8
{
    UNIFORM_BUFFER,
    STORAGE_BUFFER,
    COMBINED_IMAGE_SAMPLER,
    COMBINED_CUBE_SAMPLER
};

struct Binding
{
    Binding_Type type;
    U32 number;
    U32 count;
    U32 stage_flags;
};

struct Bind_Group_Layout_Descriptor
{
    U32 binding_count;
    Binding *bindings;
};

struct Bind_Group_Layout
{
    Bind_Group_Layout_Descriptor descriptor;
};

using Bind_Group_Layout_Handle = Resource_Handle< Bind_Group_Layout >;

struct Update_Binding_Descriptor
{
    U32 binding_number;

    U32 element_index;
    U32 count;

    Buffer_Handle *buffers;
    Texture_Handle *textures;
    Sampler_Handle *samplers;
};

//
// Render Pass
//

struct Clear_Value
{
    glm::vec4 color;
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
    
    Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > color_attachments;
    Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > depth_stencil_attachments;
    Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > resolve_attachments;
    Attachment_Operation stencil_operation = Attachment_Operation::DONT_CARE;
};

struct Render_Pass
{
    String name;

    Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > color_attachments;
    Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > depth_stencil_attachments;
    Array< Attachment_Info, HE_MAX_ATTACHMENT_COUNT > resolve_attachments;
};

using Render_Pass_Handle = Resource_Handle< Render_Pass >;

// Frame Buffer

struct Frame_Buffer_Descriptor
{
    U32 width;
    U32 height;

    Array< Texture_Handle, HE_MAX_ATTACHMENT_COUNT > attachments;

    Render_Pass_Handle render_pass;
};

struct Frame_Buffer
{
    U32 width;
    U32 height;

    Array< Texture_Handle, HE_MAX_ATTACHMENT_COUNT > attachments;

    Render_Pass_Handle render_pass;
};

using Frame_Buffer_Handle = Resource_Handle< Frame_Buffer >;

//
// Shader
//

enum class Shader_Data_Type
{
    BOOL,

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

    MATRIX3F,
    MATRIX4F,

    COMBINED_IMAGE_SAMPLER,
    COMBINED_CUBE_SAMPLER,

    STRUCT,
    ARRAY,
};

struct Shader_Input_Variable
{
    String name;
    Shader_Data_Type data_type;
    U32 location;
};

struct Shader_Output_Variable
{
    String name;
    Shader_Data_Type data_type;
    U32 location;
};

struct Shader_Struct_Member
{
    String name;

    Shader_Data_Type data_type;
    U32 offset = 0;

    bool is_array = false;
    S32 array_element_count = -1;

    S32 struct_index = -1;
};

struct Shader_Struct
{
    String name;

    U32 member_count;
    Shader_Struct_Member *members;
};

struct Shader_Descriptor
{
    void *data;
    U64 size;
};

enum class Shader_Stage : U8
{
    VERTEX,
    FRAGMENT
};

struct Shader
{
    String name;

    Bind_Group_Layout_Descriptor sets[HE_MAX_DESCRIPTOR_SET_COUNT];
    Shader_Stage stage;

    U32 input_count;
    Shader_Input_Variable *inputs;

    U32 output_count;
    Shader_Output_Variable *outputs;

    U32 struct_count;
    Shader_Struct *structs;
};

using Shader_Handle = Resource_Handle< Shader >;

struct Shader_Group_Descriptor
{
    Array< Shader_Handle, HE_MAX_SHADER_COUNT_PER_PIPELINE > shaders;
};

struct Shader_Group
{
    String name;

    Array< Shader_Handle, HE_MAX_SHADER_COUNT_PER_PIPELINE > shaders;
    Array< Bind_Group_Layout_Handle, HE_MAX_DESCRIPTOR_SET_COUNT > bind_group_layouts;
};

using Shader_Group_Handle = Resource_Handle< Shader_Group >;

//
// Bind Group
//

struct Bind_Group_Descriptor
{
    Shader_Group_Handle shader_group;
    Bind_Group_Layout_Handle layout;
};

struct Bind_Group
{
    Bind_Group_Descriptor descriptor;
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

struct Pipeline_State_Settings
{
    Cull_Mode cull_mode = Cull_Mode::BACK;
    Front_Face front_face = Front_Face::COUNTER_CLOCKWISE;
    Fill_Mode fill_mode = Fill_Mode::SOLID;
    bool depth_testing = true;
    bool sample_shading = false;
};

struct Pipeline_State_Descriptor
{
    Pipeline_State_Settings settings;

    Shader_Group_Handle shader_group;
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

    glm::vec2 v2;
    glm::vec3 v3;
    glm::vec4 v4;
};

struct Material_Property
{
    String name;

    Shader_Data_Type data_type;
    Material_Property_Data data;

    U64 offset_in_buffer;
    bool is_texture_resource;
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
// Allocation Group
//

#define HE_MAX_ALLOCATION_COUNT 8

struct Allocation_Group
{
    String resource_name;
    S32 resource_index = -1;

    U64 target_value;
    Semaphore_Handle semaphore;

    Array< void*, HE_MAX_ALLOCATION_COUNT > allocations;

    S32 index = -1;
    U32 generation = 0;
    bool *uploaded = nullptr;
};

//
// Mesh
//

struct Sub_Mesh
{
    U16 vertex_count;
    U32 index_count;

    U32 vertex_offset;
    U32 index_offset;

    U64 material_uuid;
};

struct Static_Mesh
{
    String name;

    Buffer_Handle positions_buffer;
    Buffer_Handle normals_buffer;
    Buffer_Handle uvs_buffer;
    Buffer_Handle tangents_buffer;
    Buffer_Handle indices_buffer;

    U32 vertex_count;
    U32 index_count;

    Dynamic_Array< Sub_Mesh > sub_meshes;
};

struct Static_Mesh_Descriptor
{
    U32 vertex_count;
    U32 index_count;

    glm::vec3 *positions;
    glm::vec3 *normals;
    glm::vec2 *uvs;
    glm::vec4 *tangents;
    U16 *indices;

    Dynamic_Array< Sub_Mesh > sub_meshes;

    Allocation_Group *allocation_group;
};

using Static_Mesh_Handle = Resource_Handle< Static_Mesh >;

//
// Scene
//

struct Transform
{
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 euler_angles;
    glm::vec3 scale;
};

struct Scene_Node
{
    String name;

    Scene_Node *parent;
    Scene_Node *first_child;
    Scene_Node *last_child;
    Scene_Node *next_sibling;
    Scene_Node *prev_sibling;

    U64 static_mesh_uuid;

    Transform local_transform;
    Transform global_transform;
};

struct Scene
{
    Dynamic_Array< Scene_Node > nodes;
};

using Scene_Handle = Resource_Handle< Scene >;

struct Render_Packet
{
    Material_Handle material;
    Static_Mesh_Handle static_mesh;
    U16 sub_mesh_index;

    U32 transform_index;
};

//
// Shader Structs
//

// todo(amer): @HardCoding per object data and globals struct layouts here
struct Object_Data
{
    glm::mat4 model;
};

static_assert( offsetof(Object_Data, model) == 0 );
static_assert( sizeof(Object_Data) == 64 );

struct Globals
{
    glm::mat4 view;
    glm::mat4 projection;

    glm::vec3 directional_light_direction;
    alignas(16) glm::vec3 directional_light_color;

    float gamma;
};

static_assert( offsetof(Globals, view) == 0 );
static_assert( offsetof(Globals, projection) == 64 );
static_assert( offsetof(Globals, directional_light_direction) == 128 );
static_assert( offsetof(Globals, directional_light_color) == 144 );
static_assert( offsetof(Globals, gamma) == 156 );

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