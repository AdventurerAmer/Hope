#type vertex

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "common.glsl"

layout (location = 0) in vec3 in_position;

layout (std430, set = 0, binding = 1) readonly buffer Instance_Buffer
{
    Instance_Data instances[];
};

out vec3 out_cubemap_uv;

void main()
{
    mat4 model = instances[gl_InstanceIndex].model;
    mat4 view_rotation = mat4(mat3(globals.view));
    gl_Position = globals.projection * view_rotation * model * vec4(in_position, 1.0);
    out_cubemap_uv = in_position;
}

#type fragment

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

in vec3 in_cubemap_uv;

layout (location = 0) out vec4 out_color;

layout(set = 1, binding = 0) uniform samplerCube u_cubemaps[];

layout (std430, set = 2, binding = 0) uniform Material
{
    uint skybox_texture_index;
    vec3 sky_color;
} material;

vec4 sample_cubemap(uint texture_index, vec3 uv)
{
    return texture(u_cubemaps[nonuniformEXT(texture_index)], uv);
}

void main()
{
    float gamma = globals.gamma;
    vec3 color = srgb_to_linear(sample_cubemap(material.skybox_texture_index, in_cubemap_uv).rgb, gamma);
    color *= srgb_to_linear(material.sky_color, gamma);
    out_color = vec4(linear_to_srgb(color, gamma), 1.0f);
}