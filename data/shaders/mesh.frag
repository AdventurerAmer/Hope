#version 450

#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 out_normal;
layout(location = 1) in vec2 out_uv;
layout(location = 2) flat in uint out_albedo_texture_index;

layout(location = 0) out vec4 out_color;

layout(set = 1, binding = 0) uniform sampler2D textures[];

void main()
{
    out_color = texture(textures[nonuniformEXT(out_albedo_texture_index)], out_uv);
}