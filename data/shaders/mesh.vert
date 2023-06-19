#version 450

#extension GL_EXT_nonuniform_qualifier : enable

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_uv;

layout (location = 0) out vec2 out_uv;
layout (location = 1) out flat uint out_material_index;

layout (set = 0, binding = 0) uniform Global_Uniform_Buffer
{
    mat4 view;
    mat4 projection;
} global_uniform_buffer;

struct Object_Data
{
    mat4 model;
    uint material_index;
};

layout (std140, set = 0, binding = 1) readonly buffer Object_Buffer
{
    Object_Data objects[];
} object_buffer;

void main()
{
    gl_Position = global_uniform_buffer.projection * global_uniform_buffer.view * object_buffer.objects[gl_InstanceIndex].model * vec4(in_position, 1.0);
    out_uv = in_uv;
    out_material_index = object_buffer.objects[gl_InstanceIndex].material_index;
}