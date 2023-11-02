#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

in vec3 in_cubmap_uv;

layout (location = 0) out vec4 out_color;

layout (std430, set = 0, binding = 0) uniform u_Globals
{
    Globals globals;
};

layout(set = 1, binding = 0) uniform samplerCube u_skybox_texture;

void main()
{
    out_color = vec4(texture(u_skybox_texture, in_cubmap_uv).rgb, 1.0f);
}