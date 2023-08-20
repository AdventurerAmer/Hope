struct Globals
{
    mat4 view;
    mat4 projection;

    vec3 directional_light_direction;
    vec3 directional_light_color;
};

struct Object_Data
{
    mat4 model;
};