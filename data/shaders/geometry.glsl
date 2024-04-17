#type vertex

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "../shaders/common.glsl"

layout (location = 0) in vec3 in_position;

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_INSTANCE_STORAGE_BUFFER_BINDING) readonly buffer Instance_Buffer
{
    Instance_Data instances[];
};

void main()
{
    mat4 local_to_world = instances[gl_InstanceIndex].local_to_world;
    gl_Position = globals.projection * globals.view * local_to_world * vec4(in_position, 1.0);
}

#type fragment

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "../shaders/common.glsl"

layout(location = 0) out int out_color;

void main()
{
    out_color = -1 * int(sign(globals.gamma));
}