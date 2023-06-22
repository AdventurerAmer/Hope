#version 450

#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_uv;

layout (location = 0) out vec2 out_uv;
layout (location = 1) out flat uint out_material_index;

layout (set = 0, binding = 0) uniform Globals_Buffer
{
    mat4 view;
    mat4 projection;
} globals;

layout (set = 0, binding = 1) readonly buffer Object_Buffer
{
    Object_Data objects[];
};

void main()
{
    gl_Position = globals.projection * globals.view * objects[gl_InstanceIndex].model * vec4(in_position, 1.0);
    out_uv = in_uv;
    out_material_index = objects[gl_InstanceIndex].material_index;
}