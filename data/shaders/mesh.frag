#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "common.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 1) flat in uint in_material_index;

layout(location = 0) out vec4 out_color;

layout (std140, set = 0, binding = 0) uniform u_Globals
{
    Globals globals;
};

layout (std140, set = 0, binding = 2) readonly buffer u_Materials_Buffer
{
    Material_Data materials[];
};

layout(set = 1, binding = 0) uniform sampler2D u_textures[];

void main()
{
    const Material_Data material = materials[nonuniformEXT( in_material_index )];
    out_color = texture( u_textures[material.albedo_texture_index], in_uv );
}