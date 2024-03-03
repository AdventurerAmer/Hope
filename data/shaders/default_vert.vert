#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_uv;
layout (location = 3) in vec4 in_tangent;

out vec3 out_position;
out vec3 out_normal;
out vec2 out_uv;
out vec3 out_tangent;
out vec3 out_bitangent;

layout (std430, set = 0, binding = 0) uniform Globals
{
    mat4 view;
    mat4 projection;

    vec3 eye;

    vec3 directional_light_direction;
    vec3 directional_light_color;

    float gamma;
} globals;

layout (std430, set = 0, binding = 1) readonly buffer Instance_Buffer
{
    mat4 models[];
} instance_buffer;

void main()
{
    mat4 model = instance_buffer.models[gl_InstanceIndex];

    vec4 world_position = model * vec4(in_position, 1.0);
    out_position = world_position.xyz;
    gl_Position = globals.projection * globals.view * world_position;

    mat3 object_to_world_direction = transpose(inverse(mat3(model)));

    vec3 normal = object_to_world_direction * in_normal;
    vec3 tangent = object_to_world_direction * in_tangent.xyz;

    out_uv = in_uv;
    out_normal = normal;
    out_tangent = tangent;
    out_bitangent = cross(normal, tangent) * sign(in_tangent.w);
}