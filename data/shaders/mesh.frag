#version 450

#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 in_uv;
layout(location = 1) in flat uint in_material_index;

layout(location = 0) out vec4 out_color;

struct Material_Data
{
    mat4 model;
    uint albedo_texture_index;
};

layout (std140, set = 0, binding = 2) readonly buffer Material_Buffer
{
    Material_Data materials[];
} material_buffer;

layout(set = 1, binding = 0) uniform sampler2D textures[];

void main()
{
    const Material_Data material = material_buffer.materials[nonuniformEXT( in_material_index )];
    out_color = texture( textures[nonuniformEXT( material.albedo_texture_index )], in_uv );
}