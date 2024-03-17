#pragma once

#include "core/defines.h"
#include "core/job_system.h"
#include "core/file_system.h"

#include "containers/string.h"
#include "containers/array.h"
#include "containers/dynamic_array.h"
#include "containers/resource_pool.h"

#include "rendering/renderer_types.h"

#include <atomic>

enum class Asset_Type : U32
{
    TEXTURE,
    SHADER,
    MATERIAL,
    STATIC_MESH,
    SCENE,
    COUNT
};

enum class Asset_State
{
    UNCONDITIONED,
    PENDING,
    CONDITIONED
};

struct Asset
{
    Asset_Type type;

    String absolute_path;
    String relative_path;

    U64 uuid;
    U64 last_write_time;

    Mutex mutex;

    Dynamic_Array< U64 > resource_refs;

    Asset_State state;
    Job_Handle job_handle;
};

typedef bool(*condition_asset_proc)(Read_Entire_File_Result *asset_file_result, Asset *asset, struct Resource *resource, Memory_Arena *arena);

struct Asset_Conditioner
{
    U32 extension_count;
    String *extensions;
    condition_asset_proc condition_asset;
};

typedef bool(*load_resource_proc)(Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena);
typedef void(*unload_resource_proc)(Resource *resource);

struct Resource_Loader
{
    load_resource_proc load;
    unload_resource_proc unload;
    S32 index;
    U32 generation;
};

struct Resource_Type_Info
{
    String name;
    U32 version;
    Asset_Conditioner conditioner;
    Resource_Loader loader;
};

enum class Resource_State : U8
{
    UNLOADED,
    PENDING,
    LOADED
};

struct Resource
{
    Asset_Type type;

    String absolute_path;
    String relative_path;

    U64 uuid;
    Dynamic_Array< U64 > resource_refs;
    Dynamic_Array< U64 > children;

    U64 asset_uuid;

    Mutex mutex;
    
    // todo(amer): is volatile and atomics nessessary here.
    volatile Resource_State state;
    volatile U32 ref_count;

    volatile S32 index;
    volatile U32 generation;

    Job_Handle job_handle;
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

#pragma pack(push, 1)

struct Asset_Database_Header
{
    U32 asset_count;
};

struct Asset_Info
{
    U64 uuid;
    U64 last_write_time;
    U64 relative_path_count;
    U32 resource_refs_count;
};

struct Resource_Header
{
    char magic_value[4];
    U32 type;
    U32 version;
    U64 uuid;
    U64 asset_uuid;
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

struct Shader_Stage_Info
{
    Shader_Stage stage;
    U64 size;
};

struct Shader_Resource_Info
{
    U32 shader_stage_info_count;
};

struct Material_Property_Info
{
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

bool register_asset(Asset_Type type, const char *name, U32 version, Asset_Conditioner conditioner, Resource_Loader loader);

bool is_valid(Resource_Ref ref);

Asset *find_asset(const String &relative_path);

Asset *get_asset(U64 asset_uuid);

Resource_Ref find_resource(const String &relative_path);
Resource_Ref find_resource(const char *relative_path);

Resource_Ref aquire_resource(const String &relative_path);
Resource_Ref aquire_resource(const char *relative_path);

bool aquire_resource(Resource_Ref ref);
void release_resource(Resource_Ref ref);

const Resource_Type_Info& get_info(const Asset &asset);
const Resource_Type_Info& get_info(const Resource &resource);

Resource *get_resource(Resource_Ref ref);
Resource *get_resource(U32 index);

template<typename T>
Resource_Handle<T> get_resource_handle_as(Resource_Ref ref)
{
    Resource *resource = get_resource(ref);
    HE_ASSERT(resource->state != Resource_State::UNLOADED);

    return
    {
        .index = std::atomic_load( (std::atomic<S32>*)&resource->index ),
        .generation = std::atomic_load( (std::atomic<U32>*)&resource->generation )
    };
}

String get_resource_path();

const Dynamic_Array< Asset >& get_assets();
const Dynamic_Array< Resource >& get_resources();


void reload_resources();