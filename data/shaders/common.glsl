#define PI 3.1415926535897932384626433832795

#define MATERIAL_SET_INDEX 2
#define MATERIAL_BINDING 0

#define MAX_LIGHT_COUNT 512

struct Light
{
    vec3 position;
    vec3 direction;
    float radius;
    float outer_angle;
    float inner_angle;
    vec3 color;
};

struct Instance_Data
{
    mat4 model;
};

layout (std430, set = 0, binding = 0) uniform Globals
{
    mat4 view;
    mat4 projection;

    vec3 eye;

    vec3 directional_light_direction;
    vec3 directional_light_color;
    
    uint light_count;
    Light lights[MAX_LIGHT_COUNT];

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