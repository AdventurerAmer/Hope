#type vertex

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "../shaders/common.glsl"

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

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_INSTANCE_STORAGE_BUFFER_BINDING) readonly buffer Instance_Buffer
{
    Instance_Data instances[];
};

void main()
{
    mat4 local_to_world = instances[gl_InstanceIndex].local_to_world;

    vec4 world_position = local_to_world * vec4(in_position, 1.0);
    frag_input.position = world_position.xyz;
    gl_Position = globals.projection * globals.view * world_position;

    mat3 normal_matrix = transpose(inverse(mat3(local_to_world)));
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

#include "../shaders/common.glsl"

in Fragment_Input
{
    vec3 position;
    vec2 uv;
    vec3 normal;
    vec4 tangent;
} frag_input;

layout(set = SHADER_PASS_BIND_GROUP, binding = SHADER_BINDLESS_TEXTURES_BINDING) uniform sampler2D u_textures[];

vec4 sample_texture(uint texture_index, vec2 uv)
{
    return texture( u_textures[ nonuniformEXT( texture_index ) ], uv );
}

layout (std430, set = SHADER_PASS_BIND_GROUP, binding = SHADER_LIGHT_STORAGE_BUFFER_BINDING) readonly buffer Light_Buffer
{
    Light lights[];
};

layout (std430, set = SHADER_PASS_BIND_GROUP, binding = SHADER_LIGHT_BINS_STORAGE_BUFFER_BINDING) readonly buffer Light_Bins_Buffer
{
    uint light_bins[];
};

layout (std430, set = SHADER_OBJECT_BIND_GROUP, binding = SHADER_MATERIAL_UNIFORM_BUFFER_BINDING) uniform Material
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

float distribution_ggx(float NdotH, float roughness)
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

vec3 brdf(vec3 L, vec3 radiance, vec3 N, vec3 V, float NdotV, vec3 albedo, float roughness, float metallic)
{
    vec3 H = normalize(L + V);

    float NdotL = max(0.0, dot(N, L));
    float NdotH = max(0.0, dot(N, H));
    float HdotV = max(0.0, dot(H, V));

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnel_schlick(clamp(dot(H, V), 0.0, 1.0), f0);
    float D = distribution_ggx(NdotH, roughness);
    float G = geometry_smith(NdotV, NdotL, roughness);

    vec3 specular = (F * D * G) / ((4.0 * NdotV * NdotL) + 0.0001);
    vec3 Ks = F;
    vec3 Kd = (1.0 - metallic) * (vec3(1.0) - Ks);
    vec3 diffuse = Kd * albedo / PI;
    return (diffuse + specular) * radiance * NdotL;
}

void main()
{
    float gamma = globals.gamma;

    vec3 albedo = srgb_to_linear( sample_texture( material.albedo_texture, frag_input.uv ).rgb, gamma );
    albedo *= srgb_to_linear( material.albedo_color, gamma );

    vec3 roughness_metallic = sample_texture( material.roughness_metallic_texture, frag_input.uv ).rgb;

    float roughness = roughness_metallic.g;
    roughness *= material.roughness_factor;

    float metallic = roughness_metallic.b;
    metallic *= material.metallic_factor;

    float occlusion = sample_texture( material.occlusion_texture, frag_input.uv ).r;

    vec3 normal = normalize(frag_input.normal);
    vec3 tangent = normalize(frag_input.tangent.xyz);
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    vec3 bitangent = cross(tangent, normal) * sign(frag_input.tangent.w);
    mat3 TBN = mat3(tangent, bitangent, normal);

    vec3 N = sample_texture( material.normal_texture, frag_input.uv ).xyz * 2.0 - vec3(1.0, 1.0, 1.0);
    N = normalize(TBN * N);
    
    vec3 V = normalize(globals.eye - frag_input.position);
    float NdotV = max(0.0, dot(N, V));

    vec4 view_space_p = globals.view * vec4(frag_input.position, 1.0);
    float depth = (-view_space_p.z - globals.z_near) / (globals.z_far - globals.z_near);
    float bin_width = 1.0 / float(globals.light_bin_count);
    uint bin_index = uint(depth / bin_width);

    uint bin_value = light_bins[bin_index];
    uint min_light_index = bin_value & 0xffff;
    uint max_light_index = (bin_value >> 16) & 0xffff;

    uvec2 coords = uvec2(gl_FragCoord.xy);

    float rmin = 0.0001;
    vec3 Lo = vec3(0.0f);

    for (uint light_index = min_light_index; light_index <= max_light_index; ++light_index)
    {
        Light light = lights[light_index];

        uvec2 aabb = light.screen_aabb;

        uint min_x = aabb.x & 0xffff;
        uint min_y = (aabb.x >> 16) & 0xffff;

        uint max_x = aabb.y & 0xfffff;
        uint max_y = (aabb.y >> 16) & 0xffff;

        if (coords.x < min_x || coords.x > max_x || coords.y < min_y || coords.y > max_y)
        {
            continue;
        }

        float attenuation = 1.0f;
        vec3 L = vec3(0.0f);

        switch (light.type)
        {
            case LIGHT_TYPE_DIRECTIONAL:
            {
                L = -light.direction;
            } break;

            case LIGHT_TYPE_POINT:
            {
                vec3 frag_pos_to_light = light.position - frag_input.position;
                float r = length(frag_pos_to_light);
                L = frag_pos_to_light / r;
                attenuation = pow(light.radius / max(r, rmin), 2);
                attenuation *= pow(max(1 - pow(r / light.radius, 4), 0.0), 2);
            } break;

            case LIGHT_TYPE_SPOT:
            {
                vec3 frag_pos_to_light = light.position - frag_input.position;
                float r = length(frag_pos_to_light);
                L = frag_pos_to_light / r;
                attenuation = pow(light.radius / max(r, rmin), 2);
                attenuation *= pow(max(1 - pow(r / light.radius, 4), 0.0), 2);

                float cos_theta_u = cos(light.outer_angle);
                float cos_theta_p = cos(light.inner_angle);
                float cos_theta_s = dot(light.direction, -L);
                float t = pow(clamp((cos_theta_s - cos_theta_u) / (cos_theta_p - cos_theta_u), 0.0, 1.0), 2.0);
                attenuation *= t;
            } break;
        }

        vec3 radiance = light.color * attenuation; 
        Lo += brdf(L, radiance, N, V, NdotV, albedo, roughness, metallic);
    }

    vec3 ambient = vec3(0.03) * albedo * occlusion;
    vec3 color = ambient + Lo;
    color = color / (color + vec3(1.0));
    color = linear_to_srgb(color, gamma);
    out_color = vec4(color, 1.0);
}