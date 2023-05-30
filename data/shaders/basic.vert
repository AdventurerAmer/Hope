#version 450

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec3 in_uv;

layout (location = 1) out vec3 out_uv;

layout (set = 0, binding = 0) uniform Global_Uniform_Buffer
{
    mat4 view;
    mat4 projection;
} global_uniform_buffer;

layout (push_constant) uniform Mesh_Push_Constant
{
    mat4 model;
} mesh_push_constant;

void main()
{
    gl_Position = global_uniform_buffer.projection * global_uniform_buffer.view * mesh_push_constant.model * vec4(in_position, 1.0);
    out_uv = in_uv;
}