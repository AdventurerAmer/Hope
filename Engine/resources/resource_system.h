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

    HE_FORCE_INLINE bool operator==(Resource_Ref other) const
    {
        return uuid == other.uuid;
    }

    HE_FORCE_INLINE bool operator!=(Resource_Ref other) const
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

bool init_resource_system(const String &resource_directory_name, struct Engine *engine);
void deinit_resource_system();

bool register_resource(Resource_Type type, const char *name, U32 version, Resource_Conditioner conditioner, Resource_Loader loader);

bool is_valid(Resource_Ref ref);

Resource_Ref find_resource(const String &relative_path);

Resource_Ref aquire_resource(const String &relative_path);
bool aquire_resource(Resource_Ref ref);
void release_resource(Resource_Ref ref);

Resource *get_resource(Resource_Ref ref);
Resource *get_resource(U32 index);

template<typename T>
Resource_Handle<T> get_resource_handle_as(Resource_Ref ref)
{
    Resource *resource = get_resource(ref);
    HE_ASSERT(resource->state == Resource_State::LOADED);
    return { resource->index, resource->generation };
}

bool create_material_resource(Resource *resource, const String &render_pass_name, const Pipeline_State_Settings &settings, struct Material_Property_Info *properties, U16 property_count);

void wait_for_resource_refs_to_condition(Array_View< Resource_Ref > resource_refs);
void wait_for_resource_refs_to_load(Array_View< Resource_Ref > resource_refs);

void imgui_draw_resource_system();