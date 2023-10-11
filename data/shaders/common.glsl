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