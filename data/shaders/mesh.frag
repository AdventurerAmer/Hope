#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "common.glsl"

in vec2 in_uv;
in vec3 in_normal;
in mat3 in_TBN;
flat in uint in_material_index;

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

bool normal_mapping = true;

void main()
{
    const Material_Data material = materials[nonuniformEXT( in_material_index )];
    vec3 albedo = texture( u_textures[material.albedo_texture_index], in_uv ).rgb;

    vec3 normal = normalize(in_normal);

    if (normal_mapping)
    {
        normal = texture( u_textures[material.normal_texture_index], in_uv ).rgb;
        normal = normal * 2.0 - 1.0;
        normal = normalize(in_TBN * normal);
    }

    vec3 light_direction = -globals.directional_light_direction;
    vec3 ambient = 0.1 * albedo;
    vec3 diffuse = max(dot(light_direction, normal), 0.0) * albedo;
    out_color = vec4(ambient + diffuse, 1.0f);
}