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

#define USING_STD_LAYOUT_140 1
#ifdef USING_STD_LAYOUT_140

typedef glm::ivec2 _ivec2;
typedef glm::ivec4 _ivec3;
typedef glm::ivec4 _ivec4;

typedef glm::uvec2 _uvec2;
typedef glm::uvec4 _uvec3;
typedef glm::uvec4 _uvec4;

typedef glm::vec2 _vec2;
typedef glm::vec4 _vec3;
typedef glm::vec4 _vec4;
typedef glm::mat4 _mat3;
typedef glm::mat4 _mat4;

#define int S32
#define uint U32

#define ivec2 alignas(8) _ivec2
#define ivec3 alignas(16) _ivec3
#define ivec4 alignas(16) _ivec4

#define uvec2 alignas(8) _uvec2
#define uvec3 alignas(16) _uvec3
#define uvec4 alignas(16) _uvec4

#define vec2 alignas(8) _vec2
#define vec3 alignas(16) _vec3
#define vec4 alignas(16) _vec4

#define mat3 alignas(16) _mat3
#define mat4 alignas(16) _mat4
#define struct struct alignas(16)

#endif

// todo(amer): don't depend on common.glsl in this path
#include "../Data/shaders/common.glsl"

#ifdef USING_STD_LAYOUT_140

#undef int
#undef uint
#undef vec2
#undef vec3
#undef vec4
#undef ivec2
#undef ivec3
#undef ivec4
#undef mat3
#undef mat4
#undef struct

#endif

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec3 bi_tangent;
    glm::vec2 uv;
};

enum TextureFormat
{
    TextureFormat_RGBA
};

#define MAX_TEXTURE_NAME 256

struct Texture
{
    char name[MAX_TEXTURE_NAME];
    U32 name_length;

    U32 width;
    U32 height;
};

#define MAX_MATERIAL_NAME 256

struct Material
{
    char name[MAX_MATERIAL_NAME];
    U32 name_length;

    U64 hash; // todo(amer): temprary
    Texture *albedo;
    Texture *normal;
};

#define MAX_MESH_NAME 256

struct Static_Mesh
{
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