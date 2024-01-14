#pragma once

#include "core/defines.h"

#include "containers/string.h"
#include "containers/array.h"
#include "containers/dynamic_array.h"
#include "containers/resource_pool.h"

#include "rendering/renderer_types.h"

enum class Resource_Type : U32
{
    TEXTURE,
    SHADER,
    MATERIAL,
    STATIC_MESH,
    SCENE,
    COUNT
};

enum class Resource_State
{
    UNLOADED,
    PENDING,
    LOADED
};

struct Resource
{
    U32 type;

    String asset_absolute_path;
    String absolute_path;
    String relative_path;

    U64 uuid;
    Dynamic_Array< U64 > resource_refs;

    Mutex mutex;
    Allocation_Group allocation_group;

    bool conditioned;

    Resource_State state;
    U32 ref_count;
    
    S32 index;
    U32 generation;
};

struct Resource_Ref
{
    U64 uuid;

    HE_FORCE_INLINE bool operator==(Resource_Ref other)
    {
        return uuid == other.uuid;
    }

    HE_FORCE_INLINE bool operator!=(Resource_Ref other)
    {
        return uuid != other.uuid;
    }
};

typedef bool(*condition_resource_proc)(Resource *resource, Open_File_Result *asset_file, Open_File_Result *resource_file, Temprary_Memory_Arena *temp_arena);

struct Resource_Conditioner
{ 
    U32 extension_count;
    String *extensions;
    
    condition_resource_proc condition;
};

typedef bool(*load_resource_proc)(Open_File_Result *open_file_result, Resource *resource, Temprary_Memory_Arena *temp_arena);
typedef void(*unload_resource_proc)(Resource *resource);

struct Resource_Loader
{
    bool use_allocation_group;
    load_resource_proc load;
    unload_resource_proc unload;
};

#pragma pack(push, 1)

struct Resource_Header
{
    char magic_value[4];
    U32 type;
    U32 version;
    U64 uuid;
    U16 resource_ref_count;
};

struct Texture_Resource_Info
{
    uint32_t width;
    uint32_t height;
    Texture_Format format;
    bool mipmapping;
    U64 data_offset;
};

struct Shader_Resource_Info
{
    U64 data_offset;
    U64 data_size;
};

struct Material_Property_Info
{
    bool is_texture_resource;
    bool is_color;
    Material_Property_Data default_data;
    Material_Property_Data data;
};

struct Material_Resource_Info
{
    Pipeline_State_Settings settings;

    U64 render_pass_name_count;
    U64 render_pass_name_offset;

    U16 property_count;
    S64 property_data_offset;
};

struct Sub_Mesh_Info
{
    U16 vertex_count;
    U32 index_count;

    U64 material_uuid;
};

struct Static_Mesh_Resource_Info
{
    U16 sub_mesh_count;
    U64 sub_mesh_data_offset;

    U64 data_offset;
};

struct Scene_Node_Info
{
    U32 name_count;
    U64 name_offset;
    S32 parent_index;
    Transform transform;
    U64 static_mesh_uuid;
};

struct Scene_Resource_Info
{
    U32 node_count;
    U64 node_data_offset;
};

#pragma pack(pop)

bool init_resource_system(const String &resource_directory_name, struct Engine *engine);
void deinit_resource_system();

bool register_resource(Resource_Type type, const char *name, U32 version, Resource_Conditioner conditioner, Resource_Loader loader);

bool is_valid(Resource_Ref ref);

Resource_Ref find_resource(const String &relative_path);
Resource_Ref find_resource(const char *relative_path);

Resource_Ref aquire_resource(const String &relative_path);
Resource_Ref aquire_resource(const char *relative_path);

bool aquire_resource(Resource_Ref ref);
void release_resource(Resource_Ref ref);

Resource *get_resource(Resource_Ref ref);
Resource *get_resource(U32 index);

template<typename T>
Resource_Handle<T> get_resource_handle_as(Resource_Ref ref)
{
    Resource *resource = get_resource(ref);
    HE_ASSERT(resource->state != Resource_State::UNLOADED);
    return { resource->index, resource->generation };
}

Shader_Struct *find_material_properties(Array_View<U64> shaders);
Shader_Struct *find_material_properties(Array_View<Resource_Ref> shaders);

bool create_material_resource(Resource *resource, const String &render_pass_name, const Pipeline_State_Settings &settings, struct Material_Property_Info *properties, U16 property_count);

void wait_for_resource_refs_to_condition(Array_View< Resource_Ref > resource_refs);
void wait_for_resource_refs_to_load(Array_View< Resource_Ref > resource_refs);

const Dynamic_Array< Resource >& get_resources();

void imgui_draw_resource_system();
