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

    // String asset_absloute_path;
    String absolute_path;
    String relative_path;

    U64 uuid;
    Dynamic_Array< U64 > resource_refs;

    Mutex mutex;
    Allocation_Group allocation_group;

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

typedef bool(*condition_resource_proc)(Resource *resource, const String &asset_path, Temprary_Memory_Arena *temp_arena);
typedef bool(*save_resource_proc)(Resource *resource, Open_File_Result *open_file_result, struct Temprary_Memory_Arena *arena);

struct Resource_Conditioner
{ 
    U32 extension_count;
    String *extensions;
    
    condition_resource_proc condition;
    save_resource_proc save;
};

typedef bool(*load_resource_proc)(Open_File_Result *open_file_result, Resource *resource);
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
T *get_resource_as(Resource_Ref ref);

template<typename T>
Resource_Handle<T> get_resource_handle_as(Resource_Ref ref)
{
    Resource *resource = get_resource(ref);
    HE_ASSERT(resource->state == Resource_State::LOADED);
    return { resource->index, resource->generation };
}

Resource_Ref create_material_resource(const String &relative_path, const String &render_pass_name, Array_View< Resource_Ref > shader_refs, const Pipeline_State_Settings &settings);

void imgui_draw_resource_system();