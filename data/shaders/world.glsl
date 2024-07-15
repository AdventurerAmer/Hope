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
    flat int entity_index;
} frag_input;

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_GLOBALS_UNIFORM_BINDING) uniform Globals
{
    Shader_Globals globals;
};

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_INSTANCE_STORAGE_BUFFER_BINDING) readonly buffer Instance_Buffer
{
    Shader_Instance_Data instances[];
};

void main()
{
    mat4 local_to_world = instances[gl_InstanceIndex].local_to_world;

    vec4 world_position = local_to_world * vec4(in_position, 1.0);
    gl_Position = globals.projection * globals.view * world_position;

    mat3 normal_matrix = transpose(inverse(mat3(local_to_world)));
    vec3 normal = normalize(normal_matrix * in_normal);
    vec4 tangent = vec4(normalize(normal_matrix * in_tangent.xyz), in_tangent.w);

    frag_input.position = world_position.xyz;
    frag_input.uv = in_uv;
    frag_input.normal = normal;
    frag_input.tangent = tangent;
    frag_input.entity_index = instances[gl_InstanceIndex].entity_index;
}

#type fragment

#version 450

layout (early_fragment_tests) in;

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "../shaders/common.glsl"
#include "../shaders/pbr.glsl"
#include "../shaders/lighting.glsl"

in Fragment_Input
{
    vec3 position;
    vec2 uv;
    vec3 normal;
    vec4 tangent;
    flat int entity_index;
} frag_input;

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_GLOBALS_UNIFORM_BINDING) uniform Globals
{
    Shader_Globals globals;
};

layout(set = SHADER_PASS_BIND_GROUP, binding = SHADER_BINDLESS_TEXTURES_BINDING) uniform sampler2D u_textures[];
layout(set = SHADER_PASS_BIND_GROUP, binding = SHADER_BINDLESS_TEXTURES_BINDING) uniform samplerCube u_cubemaps[];

vec4 sample_texture(uint texture_index, vec2 uv)
{
    return texture( u_textures[ nonuniformEXT( texture_index ) ], uv );
}

vec4 sample_cubemap(uint cubemap_index, vec3 uv)
{
    return texture( u_cubemaps[ nonuniformEXT( cubemap_index ) ], uv );
}

vec4 sample_cubemap_lod(uint cubemap_index, vec3 uv, float lod)
{
    return textureLod( u_cubemaps[ nonuniformEXT( cubemap_index ) ], uv, lod );
}

layout (std430, set = SHADER_PASS_BIND_GROUP, binding = SHADER_LIGHT_STORAGE_BUFFER_BINDING) readonly buffer Light_Buffer
{
    Shader_Light lights[];
};

layout (std430, set = SHADER_PASS_BIND_GROUP, binding = SHADER_LIGHT_BINS_STORAGE_BUFFER_BINDING) readonly buffer Light_Bins_Buffer
{
    uint light_bins[];
};

layout (std430, set = SHADER_OBJECT_BIND_GROUP, binding = SHADER_MATERIAL_UNIFORM_BUFFER_BINDING) uniform Material
{
    uint albedo_texture;
    vec4 albedo_color;

    uint normal_texture;

    uint roughness_metallic_texture;
    float roughness_factor;
    float metallic_factor;
    float reflectance;
    float alpha_cutoff;

    uint occlusion_texture;
    uint type;
} material;

layout (set = 3, binding = 0, r32ui) uniform coherent uimage2D head_index_image;

layout (std430, set = 3, binding = 1) writeonly buffer Node_Buffer
{
    Shader_Node nodes[];
};

layout (std430, set = 3, binding = 2) writeonly buffer Node_Count_Buffer
{
    int node_count;
};

layout(location = 0) out vec4 out_color;

void main()
{
    float gamma = globals.gamma;

    vec4 base_color = srgb_to_linear( sample_texture( material.albedo_texture, frag_input.uv ), gamma );
    float alpha = base_color.a * material.albedo_color.a;

    if (material.type == SHADER_MATERIAL_TYPE_ALPHA_CUTOFF && alpha < material.alpha_cutoff)
    {
        discard;
    }

    vec3 albedo = base_color.rgb * material.albedo_color.rgb;
    vec3 roughness_metallic = sample_texture( material.roughness_metallic_texture, frag_input.uv ).rgb;

    float roughness = roughness_metallic.g;
    roughness *= material.roughness_factor;

    float metallic = roughness_metallic.b;
    metallic *= material.metallic_factor;

    float occlusion = sample_texture( material.occlusion_texture, frag_input.uv ).r;

    vec3 normal = normalize(frag_input.normal);
    vec3 N = vec3(0.0);

    if (material.normal_texture == 0)
    {
        N = normal;
    }
    else
    {
        vec3 tangent = normalize(frag_input.tangent.xyz);
        tangent = normalize(tangent - dot(tangent, normal) * normal);
        vec3 bitangent = cross(tangent, normal) * sign(frag_input.tangent.w);
        mat3 TBN = mat3(tangent, bitangent, normal);
        N = sample_texture( material.normal_texture, frag_input.uv ).rgb * vec3(2.0) - vec3(1.0);
        N = normalize(TBN * N);
    }

    vec3 eye = vec3(globals.eye[0], globals.eye[1], globals.eye[2]);
    vec3 V = normalize(eye - frag_input.position);
    float NdotV = max(dot(N, V), 0.0);

    vec4 view_space_p = globals.view * vec4(frag_input.position, 1.0);
    float depth = (-view_space_p.z - globals.z_near) / (globals.z_far - globals.z_near);
    float bin_width = 1.0 / float(globals.light_bin_count);
    uint bin_index = uint(depth / bin_width);

    uint bin_value = light_bins[bin_index];
    uint min_light_index = bin_value & 0xffff;
    uint max_light_index = (bin_value >> 16) & 0xffff;

    uvec2 coords = uvec2(gl_FragCoord.xy);

    vec3 Lo = vec3(0.0f);

    for (uint light_index = 0; light_index < globals.directional_light_count; ++light_index)
    {
        Shader_Light light = lights[light_index];

        vec3 light_direction = vec3(light.direction[0], light.direction[1], light.direction[2]);
        vec3 light_color = vec3(light.color[0], light.color[1], light.color[2]);

        vec3 L = -light_direction;
        vec3 radiance = light_color;
        Lo += brdf(L, radiance, N, V, NdotV, albedo, roughness, metallic, material.reflectance);
    }

    for (uint light_index = min_light_index; light_index <= max_light_index; ++light_index)
    {
        Shader_Light light = lights[light_index];
        vec3 light_position = vec3(light.position[0], light.position[1], light.position[2]);
        vec3 light_direction = vec3(light.direction[0], light.direction[1], light.direction[2]);
        vec3 light_color = vec3(light.color[0], light.color[1], light.color[2]);
        uvec2 aabb = uvec2(light.screen_aabb[0], light.screen_aabb[1]);

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
            case SHADER_LIGHT_TYPE_POINT:
            {
                // NOTE: L is an out param.
                attenuation = calc_point_light_attenuation(light_position, light.radius, frag_input.position, L);
            } break;

            case SHADER_LIGHT_TYPE_SPOT:
            {
                // NOTE: L is an out param.
                attenuation = calc_spot_light_attenuation(light_position, light.radius, light_direction, light.outer_angle, light.inner_angle, frag_input.position, L);
            } break;
        }

        vec3 radiance = light_color * attenuation;
        Lo += brdf(L, radiance, N, V, NdotV, albedo, roughness, metallic, material.reflectance);
    }

    vec3 color = vec3(0.0);

    if (globals.use_environment_map == 0)
    {
        vec3 ambient = vec3(globals.ambient[0], globals.ambient[1], globals.ambient[2]);
        color = ambient * albedo * occlusion + Lo;
    }
    else
    {
        vec3 f0 = mix(vec3(material.reflectance), albedo, metallic);
        vec3 ks = fresnel_schlick_roughness(NdotV, f0, roughness);
        vec3 kd = (vec3(1.0) - ks) * (1.0 - metallic);

        vec3 irradiance = sample_cubemap(globals.irradiance_map, N).rgb;
        vec3 diffuse = irradiance * albedo;

        vec3 R = reflect(-V, N);
        vec3 prefiltered_color = sample_cubemap_lod( globals.prefilter_map, R, roughness * float(globals.prefilter_map_lod) ).rgb;
        vec2 env_brdf = sample_texture( globals.brdf_lut, vec2(NdotV, roughness) ).rg;
        vec3 specular = prefiltered_color * (ks * env_brdf.x + env_brdf.y);

        vec3 ambient = (kd * diffuse + specular) * occlusion;
        color = ambient + Lo;
    }

    int node_index = atomicAdd(node_count, 1);
    if (node_index < globals.max_node_count)
    {
        uint prev_head_index = imageAtomicExchange( head_index_image, ivec2( coords ), node_index );
        Shader_Node node;
        node.color = float[](color.r, color.g, color.b, alpha);
        node.depth = gl_FragCoord.z;
        node.next = prev_head_index;
        node.entity_index = frag_input.entity_index;
        nodes[node_index] = node;
    }

    out_color = vec4(color, 1.0f);
}