#type vertex

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "common.glsl.inc"

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_uv;
layout (location = 3) in vec4 in_tangent;

out Fragment_Input
{
    vec3 position;
    vec2 uv;
    vec3 normal;
    vec4 tangent;
} frag_input;

layout (std430, set = 0, binding = 1) readonly buffer Instance_Buffer
{
    mat4 model;
} instances[];

void main()
{
    mat4 model = instances[gl_InstanceIndex].model;

    vec4 world_position = model * vec4(in_position, 1.0);
    frag_input.position = world_position.xyz;
    gl_Position = globals.projection * globals.view * world_position;

    mat3 normal_matrix = transpose(inverse(mat3(model)));
    vec3 normal = normalize(normal_matrix * in_normal);
    vec4 tangent = vec4(normalize(normal_matrix * in_tangent.xyz), in_tangent.w);

    frag_input.uv = in_uv;
    frag_input.normal = normal;
    frag_input.tangent = tangent;
}

#type fragment

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl.inc"

in Fragment_Input
{
    vec3 position;
    vec2 uv;
    vec3 normal;
    vec4 tangent;
} frag_input;

layout(set = 1, binding = 0) uniform sampler2D u_textures[];

vec4 sample_texture(uint texture_index, vec2 uv)
{
    return texture( u_textures[ nonuniformEXT( texture_index ) ], uv );
}

layout (std430, set = 2, binding = 0) uniform Material
{
    uint albedo_texture;
    vec3 albedo_color;

    uint normal_texture;

    uint roughness_metallic_texture;
    float roughness_factor;
    float metallic_factor;

    uint occlusion_texture;
} material;

layout(location = 0) out vec4 out_color;

vec3 fresnel_schlick(float NdotL, vec3 f0)
{
    return f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - NdotL, 0.0, 1.0), 5.0);
}

float distribution_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float b = NdotH2 * (a2 - 1.0) + 1.0;
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
    float gamma = globals.gamma;
    
    vec3 albedo = srgb_to_linear( sample_texture(material.albedo_texture, frag_input.uv).rgb, gamma );
    albedo *= srgb_to_linear( material.albedo_color, gamma );
    
    vec3 roughness_metallic = sample_texture(material.roughness_metallic_texture, frag_input.uv).rgb;

    float roughness = roughness_metallic.g;
    roughness *= material.roughness_factor;

    float metallic = roughness_metallic.b;
    metallic *= material.metallic_factor;

    float occlusion = sample_texture(material.occlusion_texture, frag_input.uv).r;

    vec3 normal = normalize(frag_input.normal);
    vec3 tangent = normalize(frag_input.tangent.xyz);
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    vec3 bitangent = cross(tangent, normal) * sign(frag_input.tangent.w);
    mat3 TBN = mat3(tangent, bitangent, normal);

    vec3 N = sample_texture( material.normal_texture, frag_input.uv ).xyz * 2.0 - vec3(1.0, 1.0, 1.0);
    N = normalize(TBN * N);

    vec3 light_color = globals.directional_light_color;
    vec3 L = normalize(-globals.directional_light_direction);

    vec3 eye = globals.eye;

    vec3 V = normalize(eye - frag_input.position);
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
    vec3 Ks = F;
    vec3 Kd = (1.0 - metallic) * (vec3(1.0) - Ks);
    vec3 diffuse = Kd * albedo / PI;
    vec3 Lo = (diffuse + specular) * light_color * NdotL;
    vec3 ambient = vec3(0.03) * albedo * occlusion;
    vec3 color = ambient + Lo;
    color = color / (color + vec3(1.0));
    color = linear_to_srgb(color, gamma);
    out_color = vec4(color, 1.0);
    // out_color = vec4(1.0, 0.0, 0.0, 1.0);
}