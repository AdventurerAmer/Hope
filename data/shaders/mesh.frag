#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "common.glsl"

#define GAMMA 2.2
#define PI 3.14159265359

vec3 srgb_to_linear(vec3 color)
{
    return pow(color, vec3(GAMMA));
}

vec3 linear_to_srgb(vec3 color)
{
    return pow(color, vec3(1.0/GAMMA));
}

in vec3 in_position;
in vec2 in_uv;
in vec3 in_normal;
in vec3 in_tangent;
in vec3 in_bitangent;
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

vec3 fresnel_schlick(float NdotL, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1 - NdotL, 0.0, 1.0), 5.0);
}

float distribution_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float b = NdotH2 * (a2 - 1) + 1;
    return a2 / (PI * b * b);
}

float geometry_schlickGGX(float d, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return d / (d * (1.0 - k) + k);
}

float geometry_smith(float NdotV, float NdotL, float roughness)
{
    return geometry_schlickGGX(NdotV, roughness) * geometry_schlickGGX(NdotL, roughness);
}

void main()
{
    const Material_Data material = materials[nonuniformEXT( in_material_index )];

    vec3 albedo = srgb_to_linear( texture( u_textures[ nonuniformEXT( material.albedo_texture_index) ], in_uv ).rgb );
    vec3 occlusion_roughness_metal = texture( u_textures[ nonuniformEXT(material.roughness_texture_index) ], in_uv ).rgb;
    float occlusion = occlusion_roughness_metal.r;
    float roughness = occlusion_roughness_metal.g;
    float metallic = occlusion_roughness_metal.b;
    vec3 light_color = globals.directional_light_color;

    vec3 normal = normalize(in_normal);
    vec3 tangent = normalize(in_tangent);
    vec3 bitangent = normalize(in_bitangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    vec3 N = texture( u_textures[ nonuniformEXT( material.normal_texture_index ) ], in_uv ).rgb * 2.0 - 1.0;
    N = normalize(TBN * N);

    vec3 L = -globals.directional_light_direction;

    vec3 eye = globals.view[3].xyz;
    vec3 V = eye - in_position;

    vec3 H = normalize(L + V);

    float NdotL = max(0.0, dot(N, L));
    float NdotH = max(0.0, dot(N, H));
    float NdotV = max(0.0, dot(N, V));
    float HdotV = max(0.0, dot(H, V));

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnel_schlick(HdotV, f0);

    float D = distribution_GGX(NdotH, roughness);
    float G = geometry_smith(NdotV, NdotL, roughness);

    vec3 specular = (F * D * G) / ((4.0 * NdotV * NdotL) + 0.0001);
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kd * albedo / PI;
    vec3 Lo = (diffuse + specular) * light_color * NdotL;
    vec3 ambient = 0.03 * albedo * occlusion;
    vec3 color = ambient + Lo;
    color = color / (color + vec3(1.0));
    color = linear_to_srgb(color);
    out_color = vec4(color, 1.0);
}