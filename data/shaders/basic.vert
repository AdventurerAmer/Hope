#version 450

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec3 in_uv;

layout (location = 0) out vec3 out_normal;
layout (location = 1) out vec3 out_uv;

layout (set = 0, binding = 0) uniform Global_Uniform_Buffer
{
    mat4 model;
    mat4 view;
    mat4 projection;
} gub;

void main()
{
    gl_Position = gub.projection * gub.view * gub.model * vec4(in_position, 1.0);
    // out_normal = vec3(gub.model * vec4(in_normal, 0.0f)); // todo(amer): proper normal transformation
    out_normal = in_normal;
    out_uv = in_uv;
}