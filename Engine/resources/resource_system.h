#pragma once

#include "core/defines.h"
#include "containers/string.h"
#include "containers/array.h"
#include "containers/dynamic_array.h"
#include "rendering/renderer_types.h"

enum class Resource_Type : U8
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

    String asset_absloute_path;
    String absloute_path;
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

typedef bool(*convert_resource_proc)(const String &path, const String &output_path, Resource *resource, struct Temprary_Memory_Arena *arena);

struct Resource_Converter
{ 
    U32 extension_count;
    String *extensions;
    
    convert_resource_proc convert;
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

bool register_resource(Resource_Type type, const char *name, U32 version, Resource_Converter converter, Resource_Loader loader);

bool is_valid(Resource_Ref ref);

Resource_Ref aquire_resource(const String &path);
bool aquire_resource(Resource_Ref ref);

void release_resource(Resource_Ref ref);

Resource *get_resource(Resource_Ref ref);