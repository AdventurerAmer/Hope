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

    ShaderDataType_Struct,
    ShaderDataType_Array,
};

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
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