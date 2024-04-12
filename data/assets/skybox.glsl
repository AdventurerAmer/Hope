#type vertex

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "../shaders/common.glsl"

layout (location = 0) in vec3 in_position;

out vec3 out_cubemap_uv;

layout (std430, set = SHADER_PASS_BIND_GROUP, binding = SHADER_INSTANCE_STORAGE_BUFFER_BINDING) readonly buffer Instance_Buffer
{
    Instance_Data instances[];
};

void main()
{
    mat4 local_to_world = instances[gl_InstanceIndex].local_to_world;
    mat4 view_rotation = mat4(mat3(globals.view));
    gl_Position = globals.projection * view_rotation * local_to_world * vec4(in_position, 1.0);
    out_cubemap_uv = in_position;
}

#type fragment

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "../shaders/common.glsl"

in vec3 in_cubemap_uv;

layout (location = 0) out vec4 out_color;

layout(set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_BINDLESS_TEXTURES_BINDING) uniform samplerCube u_cubemaps[];

vec4 sample_cubemap(uint texture_index, vec3 uv)
{
    return texture(u_cubemaps[nonuniformEXT(texture_index)], uv);
}

layout (std430, set = SHADER_PASS_BIND_GROUP, binding = SHADER_LIGHT_STORAGE_BUFFER_BINDING) readonly buffer Light_Buffer
{
    Light lights[];
};

layout (std430, set = SHADER_OBJECT_BIND_GROUP, binding = SHADER_MATERIAL_UNIFORM_BUFFER_BINDING) uniform Material
{
    uint skybox_cubemap;
    vec3 sky_color;
} material;

void main()
{
    Light l = lights[0];
    vec3 c = l.color;
    vec3 color = srgb_to_linear( sample_cubemap(material.skybox_cubemap, in_cubemap_uv).rgb, globals.gamma );
    color *= srgb_to_linear( material.sky_color, globals.gamma ) * c;
    out_color = vec4( linear_to_srgb(color, globals.gamma), 1.0f );
}