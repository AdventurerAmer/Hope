#type vertex

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_uv;
layout (location = 3) in vec4 in_tangent;

out vec3 out_position;
out vec3 out_normal;
out vec2 out_uv;
out vec3 out_tangent;
out vec3 out_bitangent;

layout (std430, set = SHADER_PASS_BIND_GROUP, binding = SHADER_INSTANCE_STORAGE_BUFFER_BINDING) readonly buffer Instance_Buffer
{
    Instance_Data instances[];
};

void main()
{
    mat4 local_to_world = instances[gl_InstanceIndex].local_to_world;

    vec4 world_position = local_to_world * vec4(in_position, 1.0);
    out_position = world_position.xyz;
    gl_Position = globals.projection * globals.view * world_position;

    mat3 local_to_world_direction = transpose(inverse(mat3(local_to_world)));

    vec3 normal = local_to_world_direction * in_normal;
    vec3 tangent = local_to_world_direction * in_tangent.xyz;

    out_uv = in_uv;
    out_normal = normal;
    out_tangent = tangent;
    out_bitangent = cross(normal, tangent) * sign(in_tangent.w);
}

#type fragment

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

in vec3 in_position;
in vec3 in_normal;
in vec2 in_uv;
in vec3 in_tangent;
in vec3 in_bitangent;

layout(location = 0) out vec4 out_color;

layout(set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_BINDLESS_TEXTURES_BINDING) uniform sampler2D u_textures[];

layout (std430, set = SHADER_PASS_BIND_GROUP, binding = SHADER_LIGHT_STORAGE_BUFFER_BINDING) readonly buffer Light_Buffer
{
    Light lights[];
};

layout (std430, set = SHADER_OBJECT_BIND_GROUP, binding = SHADER_MATERIAL_UNIFORM_BUFFER_BINDING) uniform Material
{
    uint debug_texture_index;
    vec3 debug_color;
} material;

void main()
{
    vec3 debug_texture = srgb_to_linear(texture( u_textures[ nonuniformEXT( material.debug_texture_index ) ], in_uv ).rgb, globals.gamma);
    vec3 color = debug_texture * srgb_to_linear(material.debug_color, globals.gamma) * lights[0].color;
    out_color = vec4(linear_to_srgb(color, globals.gamma), 1.0);
}