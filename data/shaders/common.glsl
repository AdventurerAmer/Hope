#ifndef COMMON_GLSL
#define COMMON_GLSL

#define PI 3.1415926535897932384626433832795

#define SHADER_GLOBALS_BIND_GROUP 0
#define SHADER_GLOBALS_UNIFORM_BINDING 0
#define SHADER_INSTANCE_STORAGE_BUFFER_BINDING 1

#define SHADER_PASS_BIND_GROUP 1
#define SHADER_LIGHT_STORAGE_BUFFER_BINDING 0
#define SHADER_LIGHT_BINS_STORAGE_BUFFER_BINDING 1
#define SHADER_BINDLESS_TEXTURES_BINDING 2

#define SHADER_OBJECT_BIND_GROUP 2
#define SHADER_MATERIAL_UNIFORM_BUFFER_BINDING 0

#define NOOP(x) step(0.0, clamp((x), 0.0, 1.0))

#ifndef __cplusplus

#define MATERIAL_TYPE_OPAQUE 0
#define MATERIAL_TYPE_ALPHA_CUTOFF 1
#define MATERIAL_TYPE_TRANSPARENT 2

struct Instance_Data
{
    int entity_index;
    mat4 local_to_world;
};

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT 1
#define LIGHT_TYPE_SPOT 2

struct Light
{
    vec3 direction;
    vec3 position;
    vec3 color;
    uvec2 screen_aabb;
    uint type;
    float radius;
    float outer_angle;
    float inner_angle;
};

struct Node
{
    vec4 color;
    float depth;
    uint next;
    int entity_index;
};

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_GLOBALS_UNIFORM_BINDING) uniform Globals
{
    mat4 view;
    mat4 projection;

    vec3 eye;
    vec3 ambient;

    uvec2 resolution;

    float gamma;

    float z_near;
    float z_far;

    uint light_count;
    uint directional_light_count;
    uint light_bin_count;

    int max_node_count;

} globals;

vec3 srgb_to_linear(vec3 color, float gamma)
{
    return pow(color, vec3(gamma));
}

vec4 srgb_to_linear(vec4 color, float gamma)
{
    return pow(color, vec4(gamma));
}

vec3 linear_to_srgb(vec3 color, float gamma)
{
    return pow(color, vec3(1.0/gamma));
}

#endif // __cplusplus
#endif // COMMON_GLSL