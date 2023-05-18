#version 450

layout(location = 0) in vec3 out_normal;
layout(location = 1) in vec2 out_uv;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 1) uniform sampler2D texture_sampler;

void main()
{
    out_color = texture(texture_sampler, out_uv);
}