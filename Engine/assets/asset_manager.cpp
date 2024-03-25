#include "assets/asset_manager.h"

#include "core/logging.h"
#include "core/file_system.h"
#include "core/job_system.h"
#include "core/binary_stream.h"

#include "containers/dynamic_array.h"
#include "containers/string.h"

#include "assets/texture_importer.h"
#include "assets/shader_importer.h"
#include "assets/material_importer.h"
#include "assets/model_importer.h"
#include "assets/skybox_importer.h"

#include <ExcaliburHash/ExcaliburHash.h>

#include <random> // todo(amer): to be removed
static U64 generate_uuid()
{
    static std::random_device device;
    static std::mt19937 engine(device());
    static std::uniform_int_distribution<U64> dist(1, HE_MAX_U64);
    U64 uuid = dist(engine);
    return uuid;
}

struct Asset
{
    Load_Asset_Result load_result;
};

using Asset_Registry = Excalibur::HashMap< U64, Asset_Registry_Entry >;
using Asset_Cache = Excalibur::HashMap< U64, Asset >;

#define HE_ASSET_REGISTRY_FILE_NAME "asset_registry.haregistry"

//
// helpers
//

Asset_Info* get_asset_info_from_extension(String extension);

struct Load_Asset_Job_Data
{
    Asset_Handle asset_handle;
    String path;
    load_asset_proc load;
};

Job_Result load_asset_job(const Job_Parameters &params);

bool serialize_asset_registry();
bool deserialize_asset_registry();

struct Asset_Manager
{
    String asset_path;

    Dynamic_Array< Asset_Info > asset_infos;

    String asset_registry_path;
    Asset_Registry asset_registry;
    Asset_Cache asset_cache;
    Mutex asset_mutex;
};

static Asset_Manager *asset_manager_state;

bool init_asset_manager(String asset_path)
{
    if (asset_manager_state)
    {
        HE_LOG(Assets, Error, "init_asset_manager -- asset manager already initialized");
        return false;
    }

    if (!directory_exists(asset_path))
    {
        HE_LOG(Assets, Error, "init_asset_manager -- asset path: %.*s doesn't exists", HE_EXPAND_STRING(asset_path));
        return false;
    }

    Memory_Context cxt = use_memory_context();
    asset_manager_state = HE_ALLOCATE(cxt.permenent, Asset_Manager);
    asset_manager_state->asset_path = copy_string(asset_path, to_allocator(cxt.permenent));

    asset_manager_state->asset_registry_path = format_string(cxt.permenent, "%.*s/%s", HE_EXPAND_STRING(asset_manager_state->asset_path), HE_ASSET_REGISTRY_FILE_NAME);

    init(&asset_manager_state->asset_infos);

    asset_manager_state->asset_registry = Asset_Registry();
    asset_manager_state->asset_cache = Asset_Cache();

    platform_create_mutex(&asset_manager_state->asset_mutex);

    {
        String extensions[] =
        {
            HE_STRING_LITERAL("png"),
            HE_STRING_LITERAL("jpeg"),
            HE_STRING_LITERAL("jpg"),
            HE_STRING_LITERAL("tga"),
            HE_STRING_LITERAL("psd"),
        };

        register_asset(HE_STRING_LITERAL("texture"), to_array_view(extensions), &load_texture, &unload_texture);
    }

    {
        String extensions[] =
        {
            HE_STRING_LITERAL("glsl"),
        };
        register_asset(HE_STRING_LITERAL("shader"), to_array_view(extensions), &load_shader, &unload_shader);
    }

    {
        String extensions[] =
        {
            HE_STRING_LITERAL("gltf"),
            HE_STRING_LITERAL("glb")
        };

        register_asset(HE_STRING_LITERAL("model"), to_array_view(extensions), &load_model, &unload_model);
    }

    {
        String extensions[] =
        {
            HE_STRING_LITERAL("hamaterial"),
        };
        register_asset(HE_STRING_LITERAL("material"), to_array_view(extensions), &load_material, &unload_material);
    }

    {
        String extensions[] =
        {
            HE_STRING_LITERAL("haskybox")
        };

        register_asset(HE_STRING_LITERAL("skybox"), to_array_view(extensions), &load_skybox, &unload_skybox);
    }

    {
        String extensions[] =
        {
            HE_STRING_LITERAL("hascene")
        };

        register_asset(HE_STRING_LITERAL("scene"), to_array_view(extensions), &load_texture, &unload_texture);
    }

    if (file_exists(asset_manager_state->asset_registry_path))
    {
        bool success = deserialize_asset_registry();
        if (!success)
        {
            HE_LOG(Assets, Error, "init_asset_manager -- failed to deserialize asset registry\n");
            return false;
        }
    }

    return true;
}

void deinit_asset_manager()
{
    bool success = serialize_asset_registry();
    if (!success)
    {
        HE_LOG(Assets, Error, "deinit_asset_manager -- failed to serialize asset registry\n");
        return;
    }
}

String get_asset_path()
{
    return asset_manager_state->asset_path;
}

bool register_asset(String name, Array_View< String > extensions, load_asset_proc load, unload_asset_proc unload)
{
    for (U32 i = 0; i < asset_manager_state->asset_infos.count; i++)
    {
        Asset_Info *current = &asset_manager_state->asset_infos[i];
        if (current->name == name)
        {
            HE_LOG(Assets, Trace, "regiser_asset --- asset type %.*s already registered\n", HE_EXPAND_STRING(name));
            return false;
        }
    }

    Memory_Arena *arena = get_permenent_arena();

    Asset_Info &asset_info = append(&asset_manager_state->asset_infos);
    asset_info.name = copy_string(name, to_allocator(arena));

    asset_info.extensions = HE_ALLOCATE_ARRAY(arena, String, extensions.count);
    asset_info.extension_count = extensions.count;

    for (U32 i = 0; i < extensions.count; i++)
    {
        asset_info.extensions[i] = copy_string(extensions[i], to_allocator(arena));
    }

    asset_info.load = load;
    asset_info.unload = unload;

    return true;
}

bool is_asset_handle_valid(Asset_Handle asset_handle)
{
    if (asset_handle.uuid == 0)
    {
        return false;
    }

    platform_lock_mutex(&asset_manager_state->asset_mutex);
    auto it = asset_manager_state->asset_registry.find(asset_handle.uuid);
    bool result = it != asset_manager_state->asset_registry.iend();
    platform_unlock_mutex(&asset_manager_state->asset_mutex);
    return result;
}

bool is_asset_loaded(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    auto it = asset_manager_state->asset_cache.find(asset_handle.uuid);
    bool result = it != asset_manager_state->asset_cache.iend();
    platform_unlock_mutex(&asset_manager_state->asset_mutex);
    return result;
}

static Job_Handle aquire_asset_internal(Asset_Handle asset_handle)
{
    Asset_Registry &asset_registry = asset_manager_state->asset_registry;

    auto entry_it = asset_registry.find(asset_handle.uuid);
    HE_ASSERT(entry_it != asset_registry.iend());
    Asset_Registry_Entry &entry = entry_it.value();
    
    if (entry.state == Asset_State::UNLOADED)
    {
        entry.state = Asset_State::PENDING;

        Job_Handle parent_job = Resource_Pool< Job >::invalid_handle;
        if (is_asset_handle_valid(entry.parent))
        {
            parent_job = aquire_asset_internal(entry.parent);
        }
        
        const Asset_Info &asset_info = asset_manager_state->asset_infos[entry.type_info_index];

        Load_Asset_Job_Data load_asset_job_data = 
        {
            .asset_handle = asset_handle,
            .path = entry.path,
            .load = asset_info.load,
        };

        Job_Data data = 
        {
            .parameters =
            { 
                .data = &load_asset_job_data,
                .size = sizeof(Load_Asset_Job_Data),
                .alignment = alignof(Load_Asset_Job_Data)
            },
            .proc = &load_asset_job
        };

        entry.job = execute_job(data, { .count = 1, .data = &parent_job });
        return entry.job;
    }
    
    entry.ref_count++;
    return entry.job;
}

Job_Handle aquire_asset(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    Job_Handle result = aquire_asset_internal(asset_handle);
    platform_unlock_mutex(&asset_manager_state->asset_mutex);
    return result;
}

Load_Asset_Result get_asset(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    Asset_Cache &asset_cache = asset_manager_state->asset_cache;
    auto it = asset_cache.find(asset_handle.uuid);
    if (it == asset_cache.iend())
    {
        return {};
    }
    Asset *asset = &it.value();
    return asset->load_result;
}

void release_asset(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    Asset_Registry &asset_registry = asset_manager_state->asset_registry;

    auto entry_it = asset_registry.find(asset_handle.uuid);
    HE_ASSERT(entry_it != asset_registry.iend());
    Asset_Registry_Entry &entry = entry_it.value();

    Asset_Cache &asset_cache = asset_manager_state->asset_cache;
    auto it = asset_cache.find(asset_handle.uuid);
    HE_ASSERT(it != asset_cache.iend());

    HE_ASSERT(entry.ref_count);
    entry.ref_count--;

    if (entry.ref_count == 0)
    {
        Asset_Info &info = asset_manager_state->asset_infos[entry.type_info_index];
        
        Asset *asset = &it.value();
        info.unload(asset->load_result);

        HE_LOG(Assets, Trace, "unloaded asset: %.*s\n", HE_EXPAND_STRING(entry.path));
        asset_cache.erase(it);

        entry.state = Asset_State::UNLOADED;
    }
}

Asset_Handle get_asset_handle_internal(String path)
{
    for (auto it = asset_manager_state->asset_registry.ibegin(); it != asset_manager_state->asset_registry.iend(); it++)
    {
        const Asset_Registry_Entry &entry = it.value();
        if (entry.path == path)
        {
            return { .uuid = it.key() };
        }
    }

    return { .uuid = 0 };
}

Asset_Handle get_asset_handle(String path)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };
    return get_asset_handle_internal(path);
}

Asset_Handle import_asset(String path)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    auto &registry = asset_manager_state->asset_registry;

    Asset_Handle asset_handle = get_asset_handle_internal(path);
    if (asset_handle.uuid != 0)
    {
        return asset_handle;
    }

    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
    String absolute_path = format_string(scratch_memory.arena, "%.*s/%.*s", HE_EXPAND_STRING(asset_manager_state->asset_path), HE_EXPAND_STRING(path));

    if (!file_exists(absolute_path))
    {
        HE_LOG(Assets, Error, "import_asset -- failed to import asset file: %.*s --> filepath doesn't exist\n", HE_EXPAND_STRING(path));
        return {};
    }

    String extension = get_extension(path);
    Asset_Info *asset_info = get_asset_info_from_extension(extension);
    if (!asset_info)
    {
        HE_LOG(Assets, Error, "import_asset -- failed to import asset file: %.*s --> file extension: %.*s isn't registered", HE_EXPAND_STRING(path), HE_EXPAND_STRING(extension));
        return {};
    }

    U32 type_info_index = index_of(&asset_manager_state->asset_infos, asset_info);

    Asset_Registry_Entry entry =
    {
        .path = copy_string(path, to_allocator(get_general_purpose_allocator())),
        .type_info_index = u32_to_u16(type_info_index),
        .parent = { .uuid = 0 },
        .ref_count = 0,
        .state = Asset_State::UNLOADED,
        .job = Resource_Pool< Job >::invalid_handle,
    };

    asset_handle = { .uuid = generate_uuid() };
    registry.emplace(asset_handle.uuid, entry);
    return asset_handle;
}

void set_parent(Asset_Handle asset, Asset_Handle parent)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    auto &registry = asset_manager_state->asset_registry;
    auto it = registry.find(asset.uuid);
    auto parent_it = registry.find(parent.uuid);
    HE_ASSERT(it != registry.iend());
    Asset_Registry_Entry &entry = it.value();
    if (parent.uuid == 0 || parent_it != registry.iend())
    {
        entry.parent = parent;
    }
    else
    {
        HE_LOG(Assets, Error, "set_parent -- failed to set parent of asset %.*s-%llu parent asset %llu is invalid", HE_EXPAND_STRING(entry.path), asset.uuid, parent.uuid);
    }
}

const Asset_Registry_Entry &get_asset_registry_entry(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    auto it = asset_manager_state->asset_registry.find(asset_handle.uuid);
    HE_ASSERT(it != asset_manager_state->asset_registry.iend());
    return it.value();
}

const Asset_Info *get_asset_info(String name)
{
    for (U32 i = 0; i < asset_manager_state->asset_infos.count; i++)
    {
        Asset_Info *result = &asset_manager_state->asset_infos[i];
        if (result->name == name)
        {
            return result;
        }
    }

    return nullptr;
}

//
// helpers
//

Asset_Info* get_asset_info_from_extension(String extension)
{
    for (U32 i = 0; i < asset_manager_state->asset_infos.count; i++)
    {
        Asset_Info *current = &asset_manager_state->asset_infos[i];
        for (U32 j = 0; j < current->extension_count; j++)
        {
            if (current->extensions[j] == extension)
            {
                return current;
            }
        }
    }

    return nullptr;
}

static Job_Result load_asset_job(const Job_Parameters &params)
{
    Load_Asset_Job_Data *job_data = (Load_Asset_Job_Data *)params.data;
    String path = format_string(params.arena, "%.*s/%.*s", HE_EXPAND_STRING(asset_manager_state->asset_path), HE_EXPAND_STRING(job_data->path));
    
    Load_Asset_Result load_result = job_data->load(path);
    if (!load_result.success)
    {
        HE_LOG(Assets, Error, "aquire_asset -- failed to load asset: %.*s\n", HE_EXPAND_STRING(job_data->path));

        platform_lock_mutex(&asset_manager_state->asset_mutex);
        auto entryIt = asset_manager_state->asset_registry.find(job_data->asset_handle.uuid);
        HE_ASSERT(entryIt != asset_manager_state->asset_registry.iend());
        Asset_Registry_Entry &entry = entryIt.value();
        entry.state = Asset_State::UNLOADED;
        platform_unlock_mutex(&asset_manager_state->asset_mutex);

        return Job_Result::FAILED;
    }

    platform_lock_mutex(&asset_manager_state->asset_mutex);
    auto entryIt = asset_manager_state->asset_registry.find(job_data->asset_handle.uuid);
    HE_ASSERT(entryIt != asset_manager_state->asset_registry.iend());
    Asset_Registry_Entry &entry = entryIt.value();
    entry.state = Asset_State::LOADED;
    asset_manager_state->asset_cache.emplace(job_data->asset_handle.uuid, Asset { .load_result = load_result });
    platform_unlock_mutex(&asset_manager_state->asset_mutex);

    HE_LOG(Assets, Trace, "loaded asset: %.*s\n", HE_EXPAND_STRING(job_data->path));
    return Job_Result::SUCCEEDED;
}

static bool serialize_asset_registry()
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
    
    Asset_Registry &registry = asset_manager_state->asset_registry;

    String_Builder builder = {};
    begin_string_builder(&builder, scratch_memory.arena);
    
    append(&builder, "version 1\n");
    append(&builder, "entry_count %u\n", registry.size());

    for (auto it = registry.ibegin(); it != registry.iend(); ++it)
    {
        const Asset_Registry_Entry &entry = it.value();
        append(&builder, "asset %llu\n", it.key());
        append(&builder, "parent %llu\n", entry.parent.uuid);
        append(&builder, "path %llu %.*s\n", entry.path.count, HE_EXPAND_STRING(entry.path));
    }

    String contents = end_string_builder(&builder);
    bool success = write_entire_file(asset_manager_state->asset_registry_path, (void *)contents.data, contents.count);
    if (!success)
    {
        HE_LOG(Assets, Error, "serialize_asset_registry -- failed to write file: %.*s\n", HE_EXPAND_STRING(asset_manager_state->asset_registry_path));
    }
    
    HE_LOG(Assets, Trace, "serialized asset registry\n");
    return success;
}

static bool deserialize_asset_registry()
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    Read_Entire_File_Result file_result = read_entire_file(asset_manager_state->asset_registry_path, to_allocator(scratch_memory.arena));
    if (!file_result.success)
    {
        HE_LOG(Assets, Error, "deserialize_asset_registry -- failed to open file: %.*s\n", HE_EXPAND_STRING(asset_manager_state->asset_registry_path));
        return false;
    }

    Asset_Registry &registry = asset_manager_state->asset_registry;

    String str = { .count = file_result.size, .data = (const char *)file_result.data };
    Parse_Name_Value_Result result = parse_name_value(&str, HE_STRING_LITERAL("version"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "deserialize_asset_registry -- failed to parse version\n");
        return false;
    }

    U64 version = str_to_u64(result.value);

    result = parse_name_value(&str, HE_STRING_LITERAL("entry_count"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "deserialize_asset_registry -- failed to parse entry count\n");
        return false;
    }

    U32 entry_count = u64_to_u32(str_to_u64(result.value));
    String white_space = HE_STRING_LITERAL(" \n\t\r\v\f");

    for (U32 i = 0; i < entry_count; i++)
    {
        result = parse_name_value(&str, HE_STRING_LITERAL("asset"));
        if (!result.success)
        {
            HE_LOG(Assets, Error, "deserialize_asset_registry -- failed to parse asset in entry %u\n", i);
            return false;
        }
        U64 asset_uuid = str_to_u64(result.value);

        result = parse_name_value(&str, HE_STRING_LITERAL("parent"));
        if (!result.success)
        {
            HE_LOG(Assets, Error, "deserialize_asset_registry -- failed to parse parent in entry %u\n", i);
            return false;
        }

        U64 parent_uuid = str_to_u64(result.value);

        String path_lit = HE_STRING_LITERAL("path");
        if (!starts_with(str, path_lit))
        {
            HE_LOG(Assets, Error, "deserialize_asset_registry -- failed to parse path in entry %u\n", i);
            return false;
        }

        str = advance(str, path_lit.count);
        str = eat_chars(str, white_space);

        S64 index = find_first_char_from_left(str, white_space);
        if (index == -1)
        {
            HE_LOG(Assets, Error, "deserialize_asset_registry -- failed to parse path count in entry %u\n", i);
            return false;
        }

        String path_count_str = sub_string(str, 0, index);
        str = advance(str, path_count_str.count);
        U64 path_count = str_to_u64(path_count_str);
        str = eat_chars(str, white_space);
        String path = sub_string(str, 0, path_count);
        str = advance(str, path_count);

        String extension = get_extension(path);
        Asset_Info *asset_info = get_asset_info_from_extension(extension);
        HE_ASSERT(asset_info);

        Asset_Registry_Entry entry = {};
        entry.path = copy_string(path, to_allocator(get_general_purpose_allocator()));
        entry.type_info_index = index_of(&asset_manager_state->asset_infos, asset_info);
        entry.parent = { .uuid = parent_uuid };
        entry.ref_count = 0;
        entry.state = Asset_State::UNLOADED;
        entry.job = Resource_Pool< Job >::invalid_handle;
        registry.emplace(asset_uuid, entry);
    }

    return true;
}