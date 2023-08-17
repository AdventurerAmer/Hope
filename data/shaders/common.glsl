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
    uint material_index;
};

struct Material_Data
{
    uint albedo_texture_index;
    // vec3 albedo_color;
    uint normal_texture_index;
    uint roughness_texture_index;
    // float roughness_factor;

    // uint metallic_texture_index;
    // float metallic_factor;

    // float reflectance;
};