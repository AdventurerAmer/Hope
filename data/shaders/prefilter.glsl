#type vertex

#version 450

#extension GL_EXT_scalar_block_layout : enable

layout (location = 0) in vec3 in_position;

layout (location = 0) out vec3 out_normal;

layout (set = 0, binding = 0, std430) uniform Globals
{
    mat4 projection;
} globals;

layout( push_constant, std430 ) uniform Constants
{
    float roughness;
    mat4 view;
} constants;

void main()
{
    gl_Position = globals.projection * constants.view * vec4(in_position, 1.0);
    out_normal = in_position;
}

#type fragment

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable

#define PI 3.1415926535897932384626433832795

#include "pbr.glsl"

layout (location = 0) in vec3 in_normal;

layout (location = 0) out vec4 out_color;

layout (set = 0, binding = 1) uniform samplerCube env_map;

layout( push_constant, std430 ) uniform Constants
{
    float roughness;
    mat4 view;
} constants;

float radical_inverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 hammersley(uint i, uint N)
{
    return vec2(float(i)/float(N), radical_inverse_VdC(i));
}

vec3 importance_sample_GGX(vec2 xi, vec3 N, float roughness)
{
    float a = roughness*roughness;

    float phi = 2.0 * PI * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a*a - 1.0) * xi.y));
    float sin_theta = sqrt(1.0 - cos_theta*cos_theta);

    // from spherical coordinates to cartesian coordinates
    vec3 H;
    H.x = cos(phi) * sin_theta;
    H.y = sin(phi) * sin_theta;
    H.z = cos_theta;

    // from tangent-space vector to world-space sample vector
    vec3 up        = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sample_vector = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sample_vector);
}

void main()
{
    vec3 N = normalize(in_normal);
    vec3 R = N;
    vec3 V = R;

    const float roughness = constants.roughness;
    const uint sample_count = 1024;
    float total_weight = 0.0;
    vec3 prefiltered_color = vec3(0.0);

    for (uint i = 0; i < sample_count; i++)
    {
        vec2 xi = hammersley(i, sample_count);
        vec3 H = importance_sample_GGX(xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0)
        {
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float D   = distribution_ggx(NdotH, roughness);
            float pdf = (D * NdotH / (4.0 * HdotV)) + 0.0001;

            // float resolution = 512.0; // todo(amer): @hardcoding
            float resolution = float(textureSize(env_map, 0).x);

            float sa_texel   = 4.0 * PI / (6.0 * resolution * resolution);
            float sa_sample  = 1.0 / (float(sample_count) * pdf + 0.0001);

            float mip_level = roughness == 0.0 ? 0.0 : 0.5 * log2(sa_sample / sa_texel);

            prefiltered_color += textureLod(env_map, L, mip_level).rgb * NdotL;
            total_weight += NdotL;
        }
    }

    prefiltered_color = prefiltered_color / total_weight;
    out_color = vec4(prefiltered_color, 1.0);
}