#pragma once

#include "core/defines.h"
#include "containers/string.h"
#include "containers/array.h"
#include "rendering/renderer_types.h"

enum class Resource_Type : U8
{
    TEXTURE,
    SHADER,
    STATIC_MESH,
    MATERIAL,
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
    Mutex mutex;
    Allocation_Group allocation_group;
    Resource_State state;

    U32 ref_count;
    
    S32 index;
    U32 generation;
    
    Open_File_Result open_file_result;

    U64 size;
    U8 *data;
};

struct Resource_Ref
{
    S32 index;

    HE_FORCE_INLINE bool operator==(Resource_Ref other)
    {
        return index == other.index;
    }

    HE_FORCE_INLINE bool operator!=(Resource_Ref other)
    {
        return index != other.index;
    }
};

typedef bool(*convert_resource_proc)(const String &path, const String &output_path, struct Temprary_Memory_Arena *arena);

struct Resource_Converter
{ 
    U32 extension_count;
    String *extensions;
    
    convert_resource_proc convert;
};

typedef bool(*load_resource_proc)(Resource *resource);
typedef void(*unload_resource_proc)(Resource *resource);

struct Resource_Loader
{
    load_resource_proc load;
    unload_resource_proc unload;
};

bool init_resource_system(const String &resource_directory_name, struct Engine *engine);
void deinit_resource_system();

bool register_resource(Resource_Type type, const char *name, U32 version, Resource_Converter converter, Resource_Loader loader);

bool is_valid(Resource_Ref ref);

Resource_Ref aquire_resource(const String &path);
void release_resource(Resource_Ref ref);