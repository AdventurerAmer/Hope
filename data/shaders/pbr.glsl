#ifndef PBR_GLSL
#define PBR_GLSL

vec3 fresnel_schlick(float NdotL, vec3 f0)
{
    return f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - NdotL, 0.0, 1.0), 5.0);
}

vec3 fresnel_schlick_roughness(float NdotL, vec3 f0, float roughness)
{
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - NdotL, 0.0, 1.0), 5.0);
}

float distribution_ggx(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float b = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * b * b);
}

float geometry_schlick_ggx(float d, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return d / (d * (1.0 - k) + k);
}

float geometry_smith(float NdotV, float NdotL, float roughness)
{
    return geometry_schlick_ggx(NdotV, roughness) * geometry_schlick_ggx(NdotL, roughness);
}

vec3 brdf(vec3 L, vec3 radiance, vec3 N, vec3 V, float NdotV, vec3 albedo, float roughness, float metallic, float reflectance)
{
    vec3 H = normalize(L + V);

    float NdotL = max(0.0, dot(N, L));
    float NdotH = max(0.0, dot(N, H));
    float HdotV = max(0.0, dot(H, V));

    vec3 f0 = mix(vec3(reflectance), albedo, metallic);
    vec3 F = fresnel_schlick(clamp(dot(H, V), 0.0, 1.0), f0);
    float D = distribution_ggx(NdotH, roughness);
    float G = geometry_smith(NdotV, NdotL, roughness);

    vec3 specular = (F * D * G) / ((4.0 * NdotV * NdotL) + 0.0001);
    vec3 Ks = F;
    vec3 Kd = (1.0 - metallic) * (vec3(1.0) - Ks);
    vec3 diffuse = Kd * albedo / PI;
    return (diffuse + specular) * radiance * NdotL;
}

#endif