#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

in vec3 in_position;
in vec3 in_normal;
in vec2 in_uv;
in vec3 in_tangent;
in vec3 in_bitangent;

layout (std430, set = 0, binding = 0) uniform u_Globals
{
    Globals globals;
};

layout(set = 1, binding = 0) uniform sampler2D u_textures[];

struct Material_Properties
{
    uint debug_texture_index;
    vec3 debug_color;
};

layout (std430, set = 2, binding = 0) uniform u_Material
{
    Material_Properties material;
};

layout(location = 0) out vec4 out_color;

void main()
{
    vec3 debug_texture = srgb_to_linear(texture( u_textures[ nonuniformEXT( material.debug_texture_index ) ], in_uv ).rgb, globals.gamma);
    vec3 color = debug_texture * srgb_to_linear(material.debug_color, globals.gamma);
    out_color = vec4(linear_to_srgb(color, globals.gamma), 1.0);
}