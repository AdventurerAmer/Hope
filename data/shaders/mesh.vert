#version 450

#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec3 in_tangent;
layout (location = 3) in vec3 in_bi_tangent;
layout (location = 4) in vec2 in_uv;

out vec2 out_uv;
out vec3 out_normal;
out mat3 out_TBN;
flat out uint out_material_index;

layout (std140, set = 0, binding = 0) uniform u_Globals
{
    Globals globals;
};

layout (std140, set = 0, binding = 1) readonly buffer u_Object_Buffer
{
    Object_Data objects[];
};

void main()
{
    mat4 model = objects[gl_InstanceIndex].model;
    gl_Position = globals.projection * globals.view * model * vec4(in_position, 1.0);

    mat3 non_uniform_scaling_removed_model = transpose(inverse(mat3(model)));

    vec3 normal = normalize(non_uniform_scaling_removed_model * in_normal);
    vec3 tangent = normalize(non_uniform_scaling_removed_model * in_tangent);
    vec3 bi_tangent = normalize(non_uniform_scaling_removed_model * in_bi_tangent);

    out_uv = in_uv;
    out_normal = in_normal;
    out_TBN = mat3(tangent, bi_tangent, normal);
    out_material_index = objects[gl_InstanceIndex].material_index;
}