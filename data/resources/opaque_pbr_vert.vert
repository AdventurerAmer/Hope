#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "common.glsl"

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_uv;
layout (location = 3) in vec4 in_tangent;

out Fragment_Input
{
    vec3 position;
    vec2 uv;
    vec3 normal;
    vec4 tangent;
} frag_input;

layout (std430, set = 0, binding = 1) readonly buffer Instance_Buffer
{
    mat4 models[];
} instances;

void main()
{
    mat4 model = instances.models[gl_InstanceIndex];

    vec4 world_position = model * vec4(in_position, 1.0);
    frag_input.position = world_position.xyz;
    gl_Position = globals.projection * globals.view * world_position;

    mat3 normal_matrix = transpose(inverse(mat3(model)));
    vec3 normal = normalize(normal_matrix * in_normal);
    vec4 tangent = vec4(normalize(normal_matrix * in_tangent.xyz), in_tangent.w);

    frag_input.uv = in_uv;
    frag_input.normal = normal;
    frag_input.tangent = tangent;
}