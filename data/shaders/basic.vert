#version 450

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec4 in_color;

layout (location = 0) out vec4 out_color;

layout (set = 0, binding = 0) uniform Global_Uniform_Buffer
{
    mat4 model;
    mat4 view;
    mat4 projection;
} gub;

void main()
{
    gl_Position = gub.projection * gub.view * gub.model * vec4(in_position, 1.0);
    out_color = in_color;
}