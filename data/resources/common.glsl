#define PI 3.14159265359

struct Globals
{
    mat4 view;
    mat4 projection;

    vec3 directional_light_direction;
    vec3 directional_light_color;   
    float gamma;
};

struct Object_Data
{
    mat4 model;
};

vec3 srgb_to_linear(vec3 color, float gamma)
{
    return pow(color, vec3(gamma));
}

vec3 linear_to_srgb(vec3 color, float gamma)
{
    return pow(color, vec3(1.0/gamma));
}