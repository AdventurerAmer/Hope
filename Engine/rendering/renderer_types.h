#pragma once

#include "core/defines.h"
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
#include <glm/gtc/type_ptr.hpp>

#include <atomic>

#define HE_GRAPHICS_DEBUGGING 1
#define HE_MAX_FRAMES_IN_FLIGHT 3
#define HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT UINT16_MAX
#define HE_MAX_DESCRIPTOR_SET_COUNT 4
#define HE_MAX_OBJECT_DATA_COUNT UINT16_MAX
#define HE_PIPELINE_CACHE_FILENAME "shaders/bin/pipeline.cache"

#ifdef HE_SHIPPING
#undef HE_GRAPHICS_DEBUGGING
#define HE_GRAPHICS_DEBUGGING 0
#endif

//
// Buffer
//

enum class Buffer_Usage : U8
{
    TRANSFER,
    VERTEX,
    INDEX,
    UNIFORM,
    STORAGE,
};

struct Buffer_Descriptor
{
    U64 size;
    Buffer_Usage usage;
    bool is_device_local;
};

struct Buffer
{
    String name;

    Buffer_Usage usage;
    U64 size;
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

    DEPTH_F32_STENCIL_U8
};

struct Texture_Descriptor
{
    U32 width;
    U32 height;
    void *data = nullptr;
    Texture_Format format;
    bool mipmapping = false;
    U32 sample_count = 1;
    bool is_attachment = false;
};

struct Texture
{
    String name;

    U32 width;
    U32 height;

    Texture_Format format;

    bool is_attachment;
};

using Texture_Handle = Resource_Handle< Texture >;

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

    bool anisotropic_filtering = true;
};

struct Sampler
{
    String name;
    Sampler_Descriptor descriptor;
};

using Sampler_Handle = Resource_Handle< Sampler >;

//
// Bind Group
//

enum class Binding_Type : U8
{
    UNIFORM_BUFFER,
    STORAGE_BUFFER,
    COMBINED_IMAGE_SAMPLER
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

struct Bind_Group_Descriptor
{
    Bind_Group_Layout_Handle layout;
};

struct Bind_Group
{
    Bind_Group_Descriptor descriptor;
};

using Bind_Group_Handle = Resource_Handle< Bind_Group >;

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
    std::initializer_list< Attachment_Info > color_attachments;
    std::initializer_list< Attachment_Info > depth_stencil_attachments;
    std::initializer_list< Attachment_Info > resolve_attachments;
    Attachment_Operation stencil_operation = Attachment_Operation::DONT_CARE;
};

struct Render_Pass
{
    String name;
    Render_Pass_Descriptor descriptor;
};

using Render_Pass_Handle = Resource_Handle< Render_Pass >;

// Frame Buffer

struct Frame_Buffer_Descriptor
{
    U32 width;
    U32 height;

    std::initializer_list< Texture_Handle > attachments;

    Render_Pass_Handle render_pass;
};

struct Frame_Buffer
{
    U32 width;
    U32 height;

    U32 attachment_count;
    Texture_Handle *attachments;

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
    const char *path;
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
    std::initializer_list< Shader_Handle > shaders;
};

struct Shader_Group
{
    String name;

    U32 shader_count;
    Shader_Handle *shaders;

    Bind_Group_Layout_Handle bind_group_layouts[HE_MAX_DESCRIPTOR_SET_COUNT];
};

using Shader_Group_Handle = Resource_Handle< Shader_Group >;

struct Pipeline_State_Descriptor
{
    Shader_Group_Handle shader_group;
    Render_Pass_Handle render_pass;
};

struct Pipeline_State
{
    String name;

    Shader_Group_Handle shader_group;
};

using Pipeline_State_Handle = Resource_Handle< Pipeline_State >;

//
// Material
//

struct Material_Descriptor
{
    Pipeline_State_Handle pipeline_state_handle;
};

struct Material
{
    String name;

    U64 hash; // todo(amer): temprary

    Pipeline_State_Handle pipeline_state_handle;

    U8 *data;
    U64 size;
    Shader_Struct *properties;

    Buffer_Handle buffers[HE_MAX_FRAMES_IN_FLIGHT];
    Bind_Group_Handle bind_groups[HE_MAX_FRAMES_IN_FLIGHT];
};

using Material_Handle = Resource_Handle< Material >;

//
// Mesh
//

struct Static_Mesh_Descriptor
{
    U16 vertex_count;

    glm::vec3 *positions;
    glm::vec3 *normals;
    glm::vec2 *uvs;
    glm::vec4 *tangents;

    U16 *indices;
    U32 index_count;
};

struct Static_Mesh
{
    String name;

    U16 vertex_count;
    U32 index_count;

    Material_Handle material_handle;
};

using Static_Mesh_Handle = Resource_Handle< Static_Mesh >;

struct Scene_Node
{
    String name;

    Scene_Node *parent;
    Scene_Node *first_child;
    Scene_Node *last_child;
    Scene_Node *next_sibling;

    S32 start_mesh_index = -1;
    U32 static_mesh_count = 0;

    glm::mat4 transform;
};

// todo(amer): @HardCoding per object data and globals layouts here
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
};

static_assert( offsetof(Globals, view) == 0 );
static_assert( offsetof(Globals, projection) == 64 );
static_assert( offsetof(Globals, directional_light_direction) == 128 );
static_assert( offsetof(Globals, directional_light_color) == 144 );