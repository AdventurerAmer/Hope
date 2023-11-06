#include "resource_system.h"

#include "core/engine.h"
#include "core/file_system.h"
#include "core/debugging.h"

#include "containers/hash_map.h"

struct Resource_Type_Info
{
    String name;
    U32 version;
    Resource_Loader loader;
};

struct Resource_System_State
{
    String resource_path;
    Resource_Type_Info resource_type_infos[(U8)Resource_Type::COUNT];
};

static Resource_System_State *resource_system_state;

bool load_texture_resource(const String &path, Resource *out_resource)
{
    return false;
}

void unload_texture_resource(Resource *resource)
{
}

bool init_resource_system(const String &resource_directory_name, Engine *engine)
{
    if (resource_system_state)
    {
        HE_LOG(Resource, Fetal, "resource system already initialized\n");
        return false;
    }

    Memory_Arena *arena = &engine->memory.permanent_arena;
    resource_system_state = HE_ALLOCATE(arena, Resource_System_State);
    
    String working_directory = get_current_working_directory(arena);
    sanitize_path(working_directory);

    String resource_path = format_string(arena, "%.*s/%.*s", HE_EXPAND_STRING(working_directory), HE_EXPAND_STRING(resource_directory_name));
    
    if (!directory_exists(resource_path))
    {
        HE_LOG(Resource, Fetal, "invalid resource path: %.*s\n", HE_EXPAND_STRING(resource_path));
        return false;
    }

    // platform_walk_directory(resource_path.data, [](const char *data, U64 count)
    // {
    //     String path = { data, count };
    //     String ext = get_extension(path);
    //     HE_LOG(Resource, Trace, "resource: %.*s\n", HE_EXPAND_STRING(path));
    // });

    resource_system_state->resource_path = resource_path;

    Resource_Loader texture_loader = {};
    texture_loader.load = &load_texture_resource;
    texture_loader.unload = &unload_texture_resource;
    init(&texture_loader.extensions, &engine->memory.free_list_allocator);

    append(&texture_loader.extensions, HE_STRING_LITERAL("jpeg"));
    append(&texture_loader.extensions, HE_STRING_LITERAL("png"));
    append(&texture_loader.extensions, HE_STRING_LITERAL("tga"));
    append(&texture_loader.extensions, HE_STRING_LITERAL("psd"));

    register_resource(Resource_Type::TEXTURE, "texture", 1, texture_loader);
    
    return true;
}

void deinit_resource_system()
{
}

bool register_resource(Resource_Type type, const char *name, U32 version, Resource_Loader loader)
{
    if (!name)
    {
        HE_LOG(Resource, Trace, "name is null");
        return false;
    }

    Resource_Type_Info &resource_type_info = resource_system_state->resource_type_infos[(U32)type];
    resource_type_info.name = HE_STRING(name);
    resource_type_info.version = version;
    resource_type_info.loader = loader;
}