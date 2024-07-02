#type vertex

#version 450

layout (location = 0) in vec3 in_position;

layout (location = 0) out vec3 out_direction;

layout( push_constant, std430 ) uniform Constants
{
    mat4 view;
    mat4 projection;
} constants;

void main()
{
    gl_Position = constants.projection * constants.view * vec4(in_position, 1.0);
    out_direction = in_position;
}

#type fragment

#version 450

layout (location = 0) in vec3 in_direction;

layout (location = 0) out vec4 out_color;

const vec2 inverse_atan = vec2(0.1591, 0.3183);

vec2 sample_spherical_map(vec3 direction)
{
    vec2 uv = vec2(atan(direction.z, direction.x), asin(direction.y));
    uv *= inverse_atan;
    uv += 0.5;
    return uv;
}

layout (set = 0, binding = 0) uniform sampler2D equirectangular_map;

void main()
{
    vec3 direction = normalize(in_direction);
    vec2 uv = sample_spherical_map(direction);
    vec3 color = texture(equirectangular_map, uv).rgb;
    out_color = vec4(color, 1.0);
}