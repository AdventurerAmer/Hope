#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "common.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_tangent;
layout(location = 3) in vec3 in_bi_tangent;
layout(location = 4) flat in uint in_material_index;

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
    mat3 TBN = mat3(normalize(in_tangent), normalize(in_bi_tangent), normalize(in_normal));
    const Material_Data material = materials[nonuniformEXT( in_material_index )];
    vec3 normal = texture( u_textures[material.normal_texture_index], in_uv ).rgb;
    normal = normal * 2.0 - 1.0;
    normal = normalize(TBN * normal);
    vec3 albedo = texture( u_textures[material.albedo_texture_index], in_uv ).rgb;
    vec3 final_color = globals.directional_light_color.rgb * dot(-globals.directional_light_direction, normal) * albedo;
    out_color = vec4(final_color, 1.0f);
    // out_color = vec4(normal, 1.0f);
}