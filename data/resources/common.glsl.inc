#define PI 3.14159265359
#define MATERIAL_SET_INDEX 2
#define MATERIAL_BINDING 0

layout (std430, set = 0, binding = 0) uniform Globals
{
    mat4 view;
    mat4 projection;

    vec3 eye;

    vec3 directional_light_direction;
    vec3 directional_light_color;

    float gamma;
} globals;

vec3 srgb_to_linear(vec3 color, float gamma)
{
    return pow(color, vec3(gamma));
}

vec3 linear_to_srgb(vec3 color, float gamma)
{
    return pow(color, vec3(1.0/gamma));
}