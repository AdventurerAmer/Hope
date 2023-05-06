#version 450

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec4 in_color;

layout (location = 0) out vec4 out_color;

layout (set = 0, binding = 0) uniform Global_Uniform_Buffer
{
    vec3 offset;
} global_uniform_buffer;

void main()
{
    gl_Position = vec4(in_position + global_uniform_buffer.offset, 1.0);
    out_color = in_color;
}