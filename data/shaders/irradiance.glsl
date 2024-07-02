#type vertex

#version 450

#extension GL_EXT_scalar_block_layout : enable

layout (location = 0) in vec3 in_position;

layout (location = 0) out vec3 out_normal;

layout (set = 0, binding = 0, std430) uniform Globals
{
    mat4 projection;
} globals;

layout( push_constant, std430 ) uniform Constants
{
    mat4 view;
} constants;

void main()
{
    gl_Position = globals.projection * constants.view * vec4(in_position, 1.0);
    out_normal = in_position;
}

#type fragment

#version 450

#define PI 3.1415926535897932384626433832795

layout (location = 0) in vec3 in_normal;

layout (location = 0) out vec4 out_color;

layout (set = 0, binding = 1) uniform samplerCube env_map;

void main()
{
    vec3 normal = normalize(in_normal);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, normal));
    up = normalize(cross(normal, right));

    float phi_sample_delta = 2.0 * PI / 180.0;
    float theta_sample_delta = 0.5 * PI / 64.0;

    // float phi_sample_delta = 0.0025;
    // float theta_sample_delta = 0.0025;

    uint sample_count = 0;

    vec3 irradiance = vec3(0.0);

    for (float phi = 0.0; phi < 2.0 * PI; phi += phi_sample_delta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += theta_sample_delta)
        {
            vec3 tangent_sample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            vec3 sample_direction = tangent_sample.x * right + tangent_sample.y * up + tangent_sample.z * normal;

            irradiance += texture(env_map, sample_direction).rgb * cos(theta) * sin(theta);
            sample_count++;
        }
    }

    irradiance = (PI * irradiance) / float(sample_count);
    out_color = vec4(irradiance, 1.0);
}