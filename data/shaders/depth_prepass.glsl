#type vertex

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "../shaders/common.glsl"

layout (location = 0) in vec3 in_position;

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_GLOBALS_UNIFORM_BINDING) uniform Globals
{
    Shader_Globals globals;
};

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_INSTANCE_STORAGE_BUFFER_BINDING) readonly buffer Instance_Buffer
{
    Shader_Instance_Data instances[];
};

out Fragment_Input
{
    flat int entity_index;
} frag_input;

void main()
{
    Shader_Instance_Data instance = instances[gl_InstanceIndex];
    mat4 local_to_world = instance.local_to_world;
    gl_Position = globals.projection * globals.view * local_to_world * vec4(in_position, 1.0);
    frag_input.entity_index = instance.entity_index;
}

#type fragment

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "../shaders/common.glsl"

layout(location = 0) out int out_color;

in Fragment_Input
{
    flat int entity_index;
} frag_input;

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_GLOBALS_UNIFORM_BINDING) uniform Globals
{
    Shader_Globals globals;
};

void main()
{
    out_color = frag_input.entity_index * int(NOOP(globals.gamma));
}