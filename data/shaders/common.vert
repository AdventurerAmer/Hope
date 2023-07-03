#version 450

#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

layout (std140, set = 0, binding = 1) uniform _Object_Data
{
    Object_Data object_data;
} _object_data;

layout (std140, set = 0, binding = 2) uniform _Material_Data
{
    Material_Data material_data;
} _material_data;

layout (std140, set = 0, binding = 3) uniform _Globals
{
    Globals globals;
} _globals;

void main()
{
    gl_Position = vec4(0.0f, 0.0f, 0.0f, 1.0);
}