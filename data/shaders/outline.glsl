#type vertex

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

layout (location = 0) in vec3 in_position;

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_GLOBALS_UNIFORM_BINDING) uniform Globals
{
    Shader_Globals globals;
};

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_INSTANCE_STORAGE_BUFFER_BINDING) readonly buffer Instance_Buffer
{
    Shader_Instance_Data instances[];
};

layout (std430, set = SHADER_OBJECT_BIND_GROUP, binding = SHADER_MATERIAL_UNIFORM_BUFFER_BINDING) uniform Material
{
    vec3 outline_color;
    uint outline_texture_index;
    float scale_factor;
} material;

void main()
{
    mat4 local_to_world = instances[gl_InstanceIndex].local_to_world;

    vec4 world_position = local_to_world * vec4(in_position * material.scale_factor, 1.0);
    gl_Position = globals.projection * globals.view * world_position;
}

#type fragment

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

layout(location = 0) out vec4 out_color;

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_GLOBALS_UNIFORM_BINDING) uniform Globals
{
    Shader_Globals globals;
};

layout(set = SHADER_PASS_BIND_GROUP, binding = SHADER_BINDLESS_TEXTURES_BINDING) uniform sampler2D u_textures[];

vec4 sample_texture(uint texture_index, vec2 uv)
{
    return texture( u_textures[ nonuniformEXT( texture_index ) ], uv );
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
    vec3 outline_color;
    uint outline_texture_index;
    float scale_factor;
} material;

void main()
{
    vec3 outline_texture = srgb_to_linear( sample_texture( material.outline_texture_index, vec2(0.0, 0.0)).rgb, globals.gamma );

    float noop = NOOP(lights[0].color[0]) * NOOP(float(light_bins[0]));
    out_color = vec4(linear_to_srgb(material.outline_color, globals.gamma), 1.0) * noop;
}