#type vertex

#version 450

#extension GL_EXT_scalar_block_layout : enable

layout (location = 0) out vec2 out_uv;

void main()
{
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0f + -1.0f, 0.0f, 1.0f);
    out_uv = uv;
}

#type fragment

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable

#define PI 3.1415926535897932384626433832795

layout (location = 0) in vec2 in_uv;

layout (location = 0) out vec4 out_color;

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

float geometry_schlick_ggx(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / denom;
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometry_schlick_ggx(NdotV, roughness) * geometry_schlick_ggx(NdotL, roughness);
}

vec2 integrate_BRDF(float NdotV, float roughness)
{
    vec3 V = vec3(sqrt(1.0 - NdotV*NdotV), 0.0, NdotV);

    float a = 0;
    float b = 0;
    vec3 N = vec3(0.0, 0.0, 1.0);

    uint sample_count = 1024;

    for (uint i = 0; i < sample_count; i++)
    {
        vec2 xi = hammersley(i, sample_count);
        vec3 H = importance_sample_GGX(xi, N, roughness);
        vec3 L = normalize(2.0 * dot(H, V) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(H, V), 0.0);

        if (NdotL > 0.0)
        {
            float G = geometry_smith(N, V, L, roughness);
            float g_vis = (G * VdotH) / (NdotH * NdotV);
            float fc = pow(1.0 - VdotH, 5.0);

            a += (1.0 - fc) * g_vis;
            b += fc * g_vis;
        }
    }

    a /= float(sample_count);
    b /= float(sample_count);

    return vec2(a, b);
}

void main()
{
    vec2 uv = in_uv;
    out_color = vec4(integrate_BRDF(uv.x, uv.y), 0.0, 1.0);
}