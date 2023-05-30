#pragma once

#include "core/defines.h"

#include <glm/vec3.hpp> // glm::vec3
#include <glm/vec4.hpp> // glm::vec4
#include <glm/mat4x4.hpp> // glm::mat4
#include <glm/gtc/quaternion.hpp> // quaternion
#include <glm/gtx/quaternion.hpp> // quaternion
#include <glm/ext/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale
#include <glm/ext/matrix_clip_space.hpp> // glm::perspective
#include <glm/ext/scalar_constants.hpp> // glm::pi

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

struct Texture
{
    void *rendering_api_specific_data;
    U32 width;
    U32 height;
};

struct Static_Mesh
{
    void *rendering_api_specific_data;
    U32 vertex_count;
    U32 index_count;
    Texture albedo;
};