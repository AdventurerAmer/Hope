#pragma once

#include "core/defines.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtc/type_ptr.hpp>

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

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::vec2 uv;
};

enum TextureFormat
{
    TextureFormat_RGBA
};

#define MAX_TEXTURE_NAME 256

struct Texture
{
    // todo(amer): String
    char name[MAX_TEXTURE_NAME];
    U32 name_length;

    U32 width;
    U32 height;
};

struct Shader_Input_Variable
{
    const char *name;
    U32 name_length;
    ShaderDataType type;
    U32 location;
};

struct Shader_Output_Variable
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

#define MAX_SHADER_NAME 256

struct Shader
{
    // todo(amer): String
    char name[MAX_SHADER_NAME];
    U32 name_length;

    U32 input_count;
    Shader_Input_Variable *inputs;

    U32 output_count;
    Shader_Output_Variable *outputs;

    U32 struct_count;
    Shader_Struct *structs;
};

#define MAX_PIPELINE_STATE_NAME 256

struct Pipeline_State
{
    // todo(amer): String
    char name[MAX_PIPELINE_STATE_NAME];
    U32 name_length;

    U32 shader_count;
    Shader **shaders;
};

#define MAX_MATERIAL_NAME 256

struct Material_Descriptor
{
    Pipeline_State *pipeline_state;
};

struct Material
{
    // todo(amer): String
    char name[MAX_MATERIAL_NAME];
    U32 name_length;

    U64 hash; // todo(amer): temprary

    Pipeline_State *pipeline_state;

    U8 *data;
    U64 size;

    Shader_Struct *properties;
};

#define MAX_MESH_NAME 256

struct Static_Mesh
{
    // todo(amer): String
    char name[MAX_MATERIAL_NAME];
    U32 name_length;

    U16 vertex_count;
    U32 index_count;
    Material *material;
};

struct Scene_Node
{
    Scene_Node *parent;
    Scene_Node *first_child;
    Scene_Node *last_child;
    Scene_Node *next_sibling;

    U32 start_mesh_index;
    U32 static_mesh_count;

    glm::mat4 transform;
};

struct Object_Data
{
    glm::mat4 model;
};

StaticAssert(offsetof(Object_Data, model) == 0);
StaticAssert(sizeof(Object_Data) == 64);

struct Globals
{
    glm::mat4 view;
    glm::mat4 projection;

    glm::vec3 directional_light_direction;
    alignas(16) glm::vec3 directional_light_color;
};

StaticAssert(offsetof(Globals, view) == 0);
StaticAssert(offsetof(Globals, projection) == 64);
StaticAssert(offsetof(Globals, directional_light_direction) == 128);
StaticAssert(offsetof(Globals, directional_light_color) == 144);