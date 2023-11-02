#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

layout (location = 0) in vec3 in_position;

layout (std430, set = 0, binding = 0) uniform u_Globals
{
    Globals globals;
};

out vec3 out_cubmap_uv;

void main()
{
    mat4 viewRotation = mat4(mat3(globals.view));
    gl_Position = globals.projection * viewRotation * vec4(in_position, 1.0);
    
    out_cubmap_uv = in_position;
}