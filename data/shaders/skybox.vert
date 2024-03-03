#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

layout (location = 0) in vec3 in_position;

layout (std430, set = 0, binding = 0) uniform Globals
{
    mat4 view;
    mat4 projection;

    vec3 eye;

    vec3 directional_light_direction;
    vec3 directional_light_color;

    float gamma;
} globals;

layout (std430, set = 0, binding = 1) readonly buffer Instance_Buffer
{
    mat4 models[];
} instance_buffer;

out vec3 out_cubemap_uv;

void main()
{
    mat4 view_rotation = mat4(mat3(globals.view));
    gl_Position = globals.projection * view_rotation * vec4(in_position, 1.0);
    out_cubemap_uv = in_position;
}