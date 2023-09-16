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

#define HE_GRAPHICS_DEBUGGING 1
#define HE_MAX_FRAMES_IN_FLIGHT 3
#define HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT UINT16_MAX
#define HE_MAX_DESCRIPTOR_SET_COUNT 4
#define HE_MAX_OBJECT_DATA_COUNT UINT16_MAX
#define HE_MAX_DESCRIPTOR_SET_COUNT 4
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

    U64 size;
    void *data;
};

using Buffer_Handle = Resource_Handle< Buffer >;

//
// Texture
//
enum class Texture_Format
{
    RGBA
};

struct Texture_Descriptor
{
    U32 width;
    U32 height;
    void *data;
    Texture_Format format;
    bool mipmapping;
};

struct Texture
{
    String name;

    U32 width;
    U32 height;

    void *data;
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
// Shader
//

enum ShaderDataType
{
    ShaderDataType_Bool,

    ShaderDataType_S8,
    ShaderDataType_S16,
    ShaderDataType_S32,
    ShaderDataType_S64,

    ShaderDataType_U8,
    ShaderDataType_U16,
    ShaderDataType_U32,
    ShaderDataType_U64,

    ShaderDataType_F16,
    ShaderDataType_F32,
    ShaderDataType_F64,

    ShaderDataType_Vector2f,
    ShaderDataType_Vector3f,
    ShaderDataType_Vector4f,

    ShaderDataType_Matrix3f,
    ShaderDataType_Matrix4f,

    ShaderDataType_CombinedImageSampler,

    ShaderDataType_Struct,
    ShaderDataType_Array,
};

struct Shader_Input_Variable
{
    String name;
    ShaderDataType data_type;
    U32 location;
};

struct Shader_Output_Variable
{
    String name;
    ShaderDataType data_type;
    U32 location;
};

struct Shader_Struct_Member
{
    String name;

    ShaderDataType data_type;
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

struct Shader
{
    String name;

    U32 input_count;
    Shader_Input_Variable *inputs;

    U32 output_count;
    Shader_Output_Variable *outputs;

    U32 struct_count;
    Shader_Struct *structs;
};

using Shader_Handle = Resource_Handle< Shader >;

struct Pipeline_State_Descriptor
{
    std::initializer_list< Shader_Handle > shaders;
};

struct Pipeline_State
{
    String name;

    U32 shader_count;
    Shader_Handle *shaders;
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

    void *data0;
    void *data1;

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

    U32 start_mesh_index;
    U32 static_mesh_count;

    glm::mat4 transform;
};

// todo(amer): hardcoding per object data and globals layouts here
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

static_assert(offsetof(Globals, view) == 0);
static_assert(offsetof(Globals, projection) == 64);
static_assert(offsetof(Globals, directional_light_direction) == 128);
static_assert(offsetof(Globals, directional_light_color) == 144);