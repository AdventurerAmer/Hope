#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

in vec3 in_cubemap_uv;

layout (location = 0) out vec4 out_color;

layout (std430, set = 0, binding = 0) uniform Globals
{
    mat4 view;
    mat4 projection;

    vec3 eye;

    vec3 directional_light_direction;
    vec3 directional_light_color;

    float gamma;
} globals;

layout(set = 1, binding = 0) uniform samplerCube u_cubemaps[];

layout (std430, set = 2, binding = 0) uniform Material
{
    uint skybox_texture_index;
} material;

vec4 sample_cubemap(uint texture_index, vec3 uv)
{
    return texture(u_cubemaps[nonuniformEXT(texture_index)], uv);
}

void main()
{
    out_color = vec4(sample_cubemap(material.skybox_texture_index, in_cubemap_uv).rgb, 1.0f);
}