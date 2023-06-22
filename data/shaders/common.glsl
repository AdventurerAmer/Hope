struct Vertex
{
    vec3 position;
    vec3 normal;
    vec2 uv;
};

struct Object_Data
{
    mat4 model;
    uint material_index;
};

struct Material_Data
{
    mat4 model; // temprary for alignment....
    uint albedo_texture_index;
};