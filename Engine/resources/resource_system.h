#pragma once

#include "core/defines.h"
#include "containers/string.h"
#include "containers/array.h"
#include "containers/dynamic_array.h"

enum class Resource_Type : U8
{
    TEXTURE,
    SHADER,
    STATIC_MESH,
    MATERIAL,
    COUNT
};

struct Resource
{
    U32 index;
};

typedef bool(*load_resource_proc)(const String &path, Resource *out_resource);
typedef void(*unload_resource_proc)(Resource *resource);

struct Resource_Loader
{
    load_resource_proc load;
    unload_resource_proc unload;
    
    Dynamic_Array< String > extensions;
};

bool init_resource_system(const String &resource_directory_name, struct Engine *engine);
void deinit_resource_system();

bool register_resource(Resource_Type type, const char *name, U32 version, Resource_Loader loader);