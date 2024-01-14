#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

in vec3 in_cubemap_uv;

layout (location = 0) out vec4 out_color;

layout (std430, set = 0, binding = 0) uniform u_Globals
{
    Globals globals;
};

// layout(set = 1, binding = 0) uniform sampler2D u_textures[];
layout(set = 1, binding = 0) uniform samplerCube u_cubemaps[];

struct Material_Properties
{
    uint skybox;
};

layout (std430, set = 2, binding = 0) uniform u_Material
{
    Material_Properties mat;
};

// vec4 sample_texture(uint texture_index, vec2 uv)
// {
//     return texture(u_textures[nonuniformEXT(texture_index)], uv);
// }

vec4 sample_cubemap(uint texture_index, vec3 uv)
{
    return texture(u_cubemaps[nonuniformEXT(texture_index)], uv);
}

void main()
{
    out_color = vec4(sample_cubemap(mat.skybox, in_cubemap_uv).rgb, 1.0f);
}