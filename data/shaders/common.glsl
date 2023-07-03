struct Globals
{
    mat4 view;
    mat4 projection;
};

struct Object_Data
{
    mat4 model;
    uint material_index;
};

struct Material_Data
{
    uint albedo_texture_index;
    uint normal_texture_index;
};