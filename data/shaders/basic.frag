#version 450

layout(location = 1) in vec2 out_uv;

layout(location = 0) out vec4 out_color;

layout(set = 1, binding = 0) uniform sampler2D albedo;

void main()
{
    out_color = texture(albedo, out_uv);
}