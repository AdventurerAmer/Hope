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
#include "assets/scene_importer.h"

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
using Embeded_Asset_Cache = Excalibur::HashMap< U64, Dynamic_Array<U64> >;
using Asset_Dependency = Excalibur::HashMap< U64, Dynamic_Array<U64> >;

#define HE_ASSET_REGISTRY_FILE_NAME "asset_registry.haregistry"

struct Load_Asset_Job_Data
{
    Asset_Handle asset_handle;
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
    Embeded_Asset_Cache embeded_cache;
    Asset_Dependency asset_dependency;
    Dynamic_Array<Asset_Handle> pending_reload_assets;
    Mutex asset_mutex;
};

static Asset_Manager *asset_manager_state;

struct Reload_Asset_Job_Data
{
    Asset_Handle asset_handle;
};

Asset_Registry_Entry& internal_get_asset_registry_entry(Asset_Handle asset_handle);
bool internal_is_asset_handle_valid(Asset_Handle asset_handle);

static Job_Result reload_asset_job(const Job_Parameters &params)
{
    const Reload_Asset_Job_Data *job_data = (Reload_Asset_Job_Data *)params.data;
    Asset_Handle asset_handle = job_data->asset_handle;

    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };
    
    Memory_Context memory_context = grab_memory_context();

    Asset_Registry_Entry &entry = internal_get_asset_registry_entry(asset_handle);
    
    const Asset_Info *info = get_asset_info(entry.type_info_index);
    auto cache_it = asset_manager_state->asset_cache.find(asset_handle.uuid);
    HE_ASSERT(cache_it != asset_manager_state->asset_cache.iend());
    Asset &asset = cache_it.value();

    String relative_path = entry.path;
    load_asset_proc load = info->load;

    Asset_Handle embedder_asset = {};
    U64 data_id = 0;
    bool is_embeded = is_asset_embeded(entry.path, &embedder_asset, &data_id);
    
    if (is_embeded)
    {
        const Asset_Registry_Entry &embedder_entry = get_asset_registry_entry(embedder_asset);
        relative_path = embedder_entry.path;
        load = asset_manager_state->asset_infos[embedder_entry.type_info_index].load;
    }
    HE_ASSERT(load);

    String path = format_string(memory_context.temp_allocator, "%.*s/%.*s", HE_EXPAND_STRING(asset_manager_state->asset_path), HE_EXPAND_STRING(relative_path));

    Embeded_Asset_Params embeded_params =
    {
        .name = get_name(entry.path),
        .type_info_index = entry.type_info_index,
        .data_id = data_id,
    };

    Load_Asset_Result load_result = load(path, is_embeded ? &embeded_params : nullptr);
    if (!load_result.success)
    {
        entry.state = Asset_State::FAILED_TO_LOAD;
        asset.load_result = {};
        HE_LOG(Assets, Error, "load_asset_job -- failed to reload asset: %.*s\n", HE_EXPAND_STRING(entry.path));
        return Job_Result::FAILED;
    }
    
    entry.state = Asset_State::LOADED;
    asset.load_result = load_result;
    HE_LOG(Assets, Trace, "reloaded asset: %.*s\n", HE_EXPAND_STRING(entry.path));
    return Job_Result::SUCCEEDED;
}

static String internal_get_asset_absolute_path(const Asset_Registry_Entry &entry, Allocator allocator)
{
    String result = {};

    Asset_Handle embeder_handle = {};
    if (is_asset_embeded(entry.path, &embeder_handle))
    {
        Asset_Registry_Entry &embeder_entry = internal_get_asset_registry_entry(embeder_handle);   
        result = format_string(allocator, "%.*s/%.*s", HE_EXPAND_STRING(get_asset_path()), HE_EXPAND_STRING(embeder_entry.path));
    }
    else
    {
        result = format_string(allocator, "%.*s/%.*s", HE_EXPAND_STRING(get_asset_path()), HE_EXPAND_STRING(entry.path));
    }

    return result;
}

static void internal_reload_asset(Asset_Handle asset_handle, Job_Handle parent_job = Resource_Pool< Job >::invalid_handle, bool force_reload = false)
{
    if (!internal_is_asset_handle_valid(asset_handle))
    {
        return;
    }

    Asset_Registry_Entry &entry = internal_get_asset_registry_entry(asset_handle);
    
    if (entry.state == Asset_State::UNLOADED)
    {
        return;
    }

    Memory_Context memory_context = grab_memory_context();

    String absolute_path = internal_get_asset_absolute_path(entry, memory_context.temp_allocator);

    U64 last_write_time = platform_get_file_last_write_time(absolute_path.data);
    if (entry.last_write_time == last_write_time && !force_reload)
    {
        return;
    }

    const Asset_Info *info = get_asset_info(entry.type_info_index);
    auto cache_it = asset_manager_state->asset_cache.find(asset_handle.uuid);

    Asset *asset = nullptr;

    if (cache_it == asset_manager_state->asset_cache.iend())
    {
        cache_it = asset_manager_state->asset_cache.emplace(asset_handle.uuid, Asset {}).first;
    }

    asset = &cache_it.value();

    if (entry.state == Asset_State::LOADED)
    {
        info->unload(asset->load_result);
        asset->load_result = {};
    }

    entry.last_write_time = last_write_time;
    entry.state = Asset_State::PENDING;

    Reload_Asset_Job_Data data =
    {
        .asset_handle = asset_handle 
    };

    Job_Data job_data =
    {
        .parameters =
        {
            .data = &data,
            .size = sizeof(Reload_Asset_Job_Data),
            .alignment = alignof(Reload_Asset_Job_Data)
        },
        .proc = &reload_asset_job
    };

    Job_Handle wait_for_jobs[] = { entry.job, parent_job }; 
    entry.job = execute_job(job_data, to_array_view(wait_for_jobs));

    auto dependency_it = asset_manager_state->asset_dependency.find(asset_handle.uuid);
    if (dependency_it != asset_manager_state->asset_dependency.iend())
    {
        Dynamic_Array< U64 > &children = dependency_it.value();
        for (U32 i = 0; i < children.count; i++)
        {
            Asset_Handle child_asset_handle = { .uuid = children[i] };
            internal_reload_asset(child_asset_handle, entry.job, true);
        }
    }
}

void reload_asset(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    internal_reload_asset(asset_handle);
}

static void on_file_changes(Watch_Directory_Result result, String old_path, String new_path)
{
    using enum Watch_Directory_Result;

    sanitize_path(old_path);
    sanitize_path(new_path);

    String ext = get_extension(old_path);
    bool is_directory = ext.count == 0; 
    if (is_directory)
    {
        return;
    }

    switch (result)
    {
        case FILE_ADDED:
        {
            HE_LOG(Assets, Trace, "[Import]: %.*s\n", HE_EXPAND_STRING(old_path));
            Asset_Handle asset_handle = import_asset(old_path);
            reload_asset(asset_handle);
            serialize_asset_registry();
        } break;

        case FILE_RENAMED:
        {
            platform_lock_mutex(&asset_manager_state->asset_mutex);
            HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };
            
            Asset_Handle asset_handle = get_asset_handle(old_path);
            if (!is_asset_handle_valid(asset_handle))
            {
                return;
            }

            Memory_Context memory_context = grab_memory_context();

            Asset_Registry_Entry &entry = internal_get_asset_registry_entry(asset_handle);

            HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, (void *)entry.path.data);
            entry.path = copy_string(new_path, memory_context.general_allocator);
            HE_LOG(Assets, Trace, "[Rename]: %.*s to %.*s \n", HE_EXPAND_STRING(old_path), HE_EXPAND_STRING(new_path));
            
            serialize_asset_registry();
        } break;

        case FILE_MODIFIED:
        {
            HE_LOG(Assets, Trace, "[Modified]: %.*s\n", HE_EXPAND_STRING(old_path));
            Asset_Handle asset_handle = get_asset_handle(old_path);
            append(&asset_manager_state->pending_reload_assets, asset_handle);
        } break;

        case FILE_DELETED:
        {
            HE_LOG(Assets, Trace, "[Deleted]: %.*s\n", HE_EXPAND_STRING(old_path));

            platform_lock_mutex(&asset_manager_state->asset_mutex);
            HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };
            
            Asset_Handle asset_handle = get_asset_handle(old_path);
            if (!is_asset_handle_valid(asset_handle))
            {
                return;
            }
            
            Asset_Registry_Entry &entry = internal_get_asset_registry_entry(asset_handle);
            entry.is_deleted = true;

            serialize_asset_registry();
        } break;
    }
}

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

    Memory_Context memory_context = grab_memory_context();
    asset_manager_state = HE_ALLOCATOR_ALLOCATE(memory_context.permenent_allocator, Asset_Manager);
    asset_manager_state->asset_path = copy_string(asset_path, memory_context.permenent_allocator);

    init(&asset_manager_state->asset_infos);

    asset_manager_state->asset_registry = Asset_Registry();
    asset_manager_state->asset_cache = Asset_Cache();
    asset_manager_state->embeded_cache = Embeded_Asset_Cache();
    asset_manager_state->asset_dependency = Asset_Dependency();

    init(&asset_manager_state->pending_reload_assets);

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
            HE_STRING_LITERAL("hdr"),
        };

        register_asset(HE_STRING_LITERAL("environment_map"), to_array_view(extensions), &load_environment_map, &unload_environment_map);
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
            HE_STRING_LITERAL("hamaterial"),
        };
        register_asset(HE_STRING_LITERAL("material"), to_array_view(extensions), &load_material, &unload_material);
    }

    {
        String extensions[] =
        {
            HE_STRING_LITERAL("hastaticmesh"),
        };
        register_asset(HE_STRING_LITERAL("static_mesh"), to_array_view(extensions), &load_static_mesh, &unload_static_mesh);
    }

    {
        String extensions[] =
        {
            HE_STRING_LITERAL("gltf"),
            HE_STRING_LITERAL("glb")
        };

        register_asset(HE_STRING_LITERAL("model"), to_array_view(extensions), &load_model, &unload_model, &on_import_model);
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

        register_asset(HE_STRING_LITERAL("scene"), to_array_view(extensions), &load_scene, &unload_scene);
    }

    String asset_registry_path = format_string(memory_context.temp_allocator, "%.*s/%s", HE_EXPAND_STRING(asset_manager_state->asset_path), HE_ASSET_REGISTRY_FILE_NAME);

    asset_manager_state->asset_registry_path = copy_string(asset_registry_path, memory_context.permenent_allocator);

    if (file_exists(asset_manager_state->asset_registry_path))
    {
        bool success = deserialize_asset_registry();
        if (!success)
        {
            HE_LOG(Assets, Error, "init_asset_manager -- failed to deserialize asset registry\n");
            return false;
        }
    }

    bool success = platform_watch_directory(asset_path.data, &on_file_changes);
    if (!success)
    {
        HE_LOG(Assets, Error, "init_asset_manager -- watch asset directory for changes\n");
        return false;
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

void reload_assets()
{
    for (U32 i = 0; i < asset_manager_state->pending_reload_assets.count; i++)
    {
        Asset_Handle asset_handle = asset_manager_state->pending_reload_assets[i];
        reload_asset(asset_handle);
    }

    reset(&asset_manager_state->pending_reload_assets);
}

String get_asset_path()
{
    return asset_manager_state->asset_path;
}

bool register_asset(String name, Array_View< String > extensions, load_asset_proc load, unload_asset_proc unload, on_import_asset_proc on_import)
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

    Memory_Context memory_context = grab_memory_context();

    Asset_Info &asset_info = append(&asset_manager_state->asset_infos);
    asset_info.name = copy_string(name, memory_context.permenent_allocator);

    asset_info.extensions = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.permenent_allocator, String, extensions.count);
    asset_info.extension_count = extensions.count;

    for (U32 i = 0; i < extensions.count; i++)
    {
        asset_info.extensions[i] = copy_string(extensions[i], memory_context.permenent_allocator);
    }

    asset_info.on_import = on_import;
    asset_info.load = load;
    asset_info.unload = unload;

    return true;
}

static bool internal_is_asset_handle_valid(Asset_Handle asset_handle)
{
    auto it = asset_manager_state->asset_registry.find(asset_handle.uuid);
    return it != asset_manager_state->asset_registry.iend() && !it.value().is_deleted;
}

bool is_asset_handle_valid(Asset_Handle asset_handle)
{
    if (asset_handle.uuid == 0)
    {
        return false;
    }

    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER{ platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    return internal_is_asset_handle_valid(asset_handle);
}

bool is_asset_of_type(Asset_Handle asset_handle, String type)
{
    const Asset_Info *asset_info = get_asset_info(asset_handle);
    return asset_info->name == type;
}

static bool internal_is_asset_loaded(Asset_Handle asset_handle)
{
    auto it = asset_manager_state->asset_cache.find(asset_handle.uuid);
    return it != asset_manager_state->asset_cache.iend() && it.value().load_result.success;
}

bool is_asset_loaded(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER{ platform_unlock_mutex(&asset_manager_state->asset_mutex); };
    return internal_is_asset_loaded(asset_handle);
}

static Job_Handle internal_acquire_asset(Asset_Handle asset_handle)
{
    Asset_Registry &asset_registry = asset_manager_state->asset_registry;

    auto entry_it = asset_registry.find(asset_handle.uuid);
    HE_ASSERT(entry_it != asset_registry.iend());
    Asset_Registry_Entry &entry = entry_it.value();
    entry.ref_count++;

    if (entry.state == Asset_State::UNLOADED)
    {
        entry.state = Asset_State::PENDING;

        Job_Handle parent_job = Resource_Pool< Job >::invalid_handle;
        if (internal_is_asset_handle_valid(entry.parent))
        {
            parent_job = internal_acquire_asset(entry.parent);
        }

        String path = entry.path;
        const Asset_Info *asset_info = &asset_manager_state->asset_infos[entry.type_info_index];

        Load_Asset_Job_Data load_asset_job_data =
        {
            .asset_handle = asset_handle,
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
    }

    return entry.job;
}

Job_Handle acquire_asset(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };
    return internal_acquire_asset(asset_handle);
}

Load_Asset_Result get_asset(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    Asset_Cache &asset_cache = asset_manager_state->asset_cache;
    auto it = asset_cache.find(asset_handle.uuid);
    HE_ASSERT(it != asset_cache.iend());
    Asset* asset = &it.value();
    return asset->load_result;
}

void release_asset(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    Asset_Registry &asset_registry = asset_manager_state->asset_registry;

    auto entry_it = asset_registry.find(asset_handle.uuid);
    if (entry_it == asset_registry.iend())
    {
        return;
    }

    Asset_Registry_Entry &entry = entry_it.value();

    Asset_Cache &asset_cache = asset_manager_state->asset_cache;
    auto it = asset_cache.find(asset_handle.uuid);
    HE_ASSERT(it != asset_cache.iend());

    HE_ASSERT(entry.ref_count);
    entry.ref_count--;

    if (entry.ref_count == 0)
    {
        Asset_Info &info = asset_manager_state->asset_infos[entry.type_info_index];
        HE_ASSERT(info.unload);

        Asset *asset = &it.value();
        info.unload(asset->load_result);

        HE_LOG(Assets, Trace, "unloaded asset: %.*s\n", HE_EXPAND_STRING(entry.path));
        asset_cache.erase(it);

        entry.state = Asset_State::UNLOADED;
    }
}

static Asset_Handle internal_get_asset_handle(String path)
{
    for (auto it = asset_manager_state->asset_registry.ibegin(); it != asset_manager_state->asset_registry.iend(); it++)
    {
        const Asset_Registry_Entry &entry = it.value();
        if (entry.path == path && !entry.is_deleted)
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
    return internal_get_asset_handle(path);
}

static void internal_add_embeded_asset(Asset_Handle embeder_asset_handle, Asset_Handle asset_handle)
{
    auto &embeded_cache = asset_manager_state->embeded_cache;
    
    auto it = embeded_cache.find(embeder_asset_handle.uuid);
    if (it == embeded_cache.iend())
    {
        Dynamic_Array<U64> embeded;
        init(&embeded);
        append(&embeded, asset_handle.uuid);
        embeded_cache.emplace(embeder_asset_handle.uuid, embeded);
    }
    else
    {
        Dynamic_Array<U64> &embeded = it.value();
        if (find(&embeded, asset_handle.uuid) == -1)
        {
            append(&embeded, asset_handle.uuid);
        }
    }
}

static void internal_add_asset_dependency(Asset_Handle parent_handle, Asset_Handle asset_handle)
{
    Asset_Dependency &dependency = asset_manager_state->asset_dependency;
    
    auto it = dependency.find(parent_handle.uuid);
    if (it == dependency.iend())
    {
        Dynamic_Array<U64> children;
        init(&children);
        append(&children, asset_handle.uuid);
        dependency.emplace(parent_handle.uuid, children);
    }
    else
    {
        Dynamic_Array<U64> &children = it.value();
        if (find(&children, asset_handle.uuid) == -1)
        {
            append(&children, asset_handle.uuid);
        }
    }
}

Asset_Handle import_asset(String path)
{
    if (path.count == 0)
    {
        HE_LOG(Assets, Error, "import_asset -- failed to import asset file path is empty\n");
        return {};
    }

    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    Memory_Context memory_context = grab_memory_context();

    path = copy_string(path, memory_context.temp_allocator);
    sanitize_path(path);

    auto &registry = asset_manager_state->asset_registry;

    String name_with_extension = get_name_with_extension(path);

    for (auto it = asset_manager_state->asset_registry.ibegin(); it != asset_manager_state->asset_registry.iend(); it++)
    {
        Asset_Registry_Entry &entry = it.value();
        if (name_with_extension == get_name_with_extension(entry.path) && entry.is_deleted)
        {
            HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, (void *)entry.path.data);
            entry.path = copy_string(path, memory_context.general_allocator);
            entry.is_deleted = false;
            return { .uuid = it.key() };
        }
        else if (path == entry.path)
        {
            if (entry.is_deleted)
            {
                return {};
            }
            else
            {
                return { .uuid = it.key() };
            }
        }
    }

    Asset_Handle embeder = {};
    bool is_embeded = is_asset_embeded(path, &embeder);
    
    if (is_embeded)
    {
        if (!internal_is_asset_handle_valid(embeder))
        {
            HE_LOG(Assets, Error, "import_asset -- failed to import emdeded asset file: %.*s --> embeder %llu is invalid\n", HE_EXPAND_STRING(path), embeder.uuid);
            return {};
        }
    }
    else
    {
        String absolute_path = format_string(memory_context.temp_allocator, "%.*s/%.*s", HE_EXPAND_STRING(asset_manager_state->asset_path), HE_EXPAND_STRING(path));
        
        if (!file_exists(absolute_path))
        {
            HE_LOG(Assets, Error, "import_asset -- failed to import asset file: %.*s --> filepath doesn't exist\n", HE_EXPAND_STRING(path));
            return {};
        }
    }

    String extension = get_extension(path);
    const Asset_Info *asset_info = get_asset_info_from_extension(extension);
    if (!asset_info)
    {
        HE_LOG(Assets, Error, "import_asset -- failed to import asset file: %.*s --> file extension: %.*s isn't registered", HE_EXPAND_STRING(path), HE_EXPAND_STRING(extension));
        return {};
    }

    U32 type_info_index = index_of(&asset_manager_state->asset_infos, asset_info);

    Asset_Registry_Entry entry =
    {
        .path = copy_string(path, memory_context.general_allocator),
        .type_info_index = u32_to_u16(type_info_index),
        .parent = {},
        .last_write_time = 0,
        .ref_count = 0,
        .state = Asset_State::UNLOADED,
        .job = Resource_Pool< Job >::invalid_handle,
        .is_deleted = false
    };

    Asset_Handle asset_handle = { .uuid = generate_uuid() };
    registry.emplace(asset_handle.uuid, entry);

    if (is_embeded && internal_is_asset_handle_valid(embeder))
    {   
        internal_add_embeded_asset(embeder, asset_handle);
        internal_add_asset_dependency(embeder, asset_handle);
    }

    if (asset_info->on_import)
    {
        asset_info->on_import(asset_handle);
    }

    HE_LOG(Assets, Trace, "Imported Asset: %.*s\n", HE_EXPAND_STRING(entry.path));
    return asset_handle;
}

void set_parent(Asset_Handle asset, Asset_Handle parent)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    Asset_Registry &registry = asset_manager_state->asset_registry;
    Asset_Dependency &dependency = asset_manager_state->asset_dependency;
    auto it = registry.find(asset.uuid);
    auto parent_it = registry.find(parent.uuid);
    HE_ASSERT(it != registry.iend());
    Asset_Registry_Entry &entry = it.value();
    if (entry.parent.uuid != 0)
    {
        auto dependency_it = dependency.find(entry.parent.uuid);
        if (dependency_it != dependency.iend())
        {
            Dynamic_Array< U64 > &children = dependency_it.value();
            S64 index = find(&children, asset.uuid);
            if (index != -1)
            {
                remove_and_swap_back(&children, (S32)index);
            }
        }
    }

    if (parent_it != registry.iend())
    {
        internal_add_asset_dependency(parent, asset);
    }

    if (parent.uuid == 0 || parent_it != registry.iend())
    {
        entry.parent = parent;
    }
    else
    {
        HE_LOG(Assets, Error, "set_parent -- failed to set parent of asset %.*s-%llu parent asset %llu is invalid", HE_EXPAND_STRING(entry.path), asset.uuid, parent.uuid);
    }
}

bool is_asset_embeded(String path, Asset_Handle *out_embeder, U64 *out_data_id)
{
    Memory_Context memory_context = grab_memory_context();

    Asset_Handle embeder = {};
    U64 data_id = 0;
    char *name = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, char, 128); // todo(amer): @Hardcoding
    S32 count = sscanf(path.data, "@%llu-%llu/%s", &embeder.uuid, &data_id, name);
    if (out_embeder)
    {
        *out_embeder = embeder;
    }
    if (out_data_id)
    {
        *out_data_id = data_id;
    }
    return count == 3;
}

bool is_asset_embeded(Asset_Handle asset_handle)
{
    const Asset_Registry_Entry &entry = get_asset_registry_entry(asset_handle);
    return is_asset_embeded(entry.path);
}

Array_View< U64 > get_embeded_assets(Asset_Handle asset_handle)
{
    auto it = asset_manager_state->embeded_cache.find(asset_handle.uuid);
    if (it == asset_manager_state->embeded_cache.iend())
    {
        return {};
    }
    return to_array_view(it.value());
}

static Asset_Registry_Entry& internal_get_asset_registry_entry(Asset_Handle asset_handle)
{
    auto it = asset_manager_state->asset_registry.find(asset_handle.uuid);
    HE_ASSERT(it != asset_manager_state->asset_registry.iend());
    return it.value();
}

const Asset_Registry_Entry& get_asset_registry_entry(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };
    return internal_get_asset_registry_entry(asset_handle);
}

const Asset_Info* get_asset_info(Asset_Handle asset_handle)
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    const Asset_Registry_Entry &entry = internal_get_asset_registry_entry(asset_handle);
    return &asset_manager_state->asset_infos[entry.type_info_index];
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

const Asset_Info *get_asset_info(U16 type_info_index)
{
    HE_ASSERT(type_info_index >= 0 && type_info_index < asset_manager_state->asset_infos.count);
    return &asset_manager_state->asset_infos[type_info_index];
}

Load_Asset_Result *get_asset_load_result(Asset_Handle asset)
{
    auto it = asset_manager_state->asset_cache.find(asset.uuid);
    HE_ASSERT(it != asset_manager_state->asset_cache.iend());
    return &it.value().load_result;
}

//
// helpers
//

const Asset_Info *get_asset_info_from_extension(String extension)
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

    Memory_Context memory_context = grab_memory_context();

    const Asset_Registry_Entry &asset_entry = get_asset_registry_entry(job_data->asset_handle);
    String relative_path = asset_entry.path;
    load_asset_proc load = asset_manager_state->asset_infos[asset_entry.type_info_index].load;

    Asset_Handle embedder_asset = {};
    U64 data_id = 0;
    bool is_embeded = is_asset_embeded(asset_entry.path, &embedder_asset, &data_id);
    
    if (is_embeded)
    {
        const Asset_Registry_Entry &embedder_entry = get_asset_registry_entry(embedder_asset);
        relative_path = embedder_entry.path;
        load = asset_manager_state->asset_infos[embedder_entry.type_info_index].load;
    }

    HE_ASSERT(load);

    String path = format_string(memory_context.temp_allocator, "%.*s/%.*s", HE_EXPAND_STRING(asset_manager_state->asset_path), HE_EXPAND_STRING(relative_path));

    Embeded_Asset_Params embeded_params =
    {
        .name = get_name(asset_entry.path),
        .type_info_index = asset_entry.type_info_index,
        .data_id = data_id,
    };

    Load_Asset_Result load_result = load(path, is_embeded ? &embeded_params : nullptr);

    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };
    
    Asset_Registry_Entry &entry = internal_get_asset_registry_entry(job_data->asset_handle);
        
    if (!load_result.success)
    {
        entry.state = Asset_State::FAILED_TO_LOAD;
        HE_LOG(Assets, Error, "load_asset_job -- failed to load asset: %.*s\n", HE_EXPAND_STRING(asset_entry.path));
        return Job_Result::FAILED;
    }
    
    entry.state = Asset_State::LOADED;
    asset_manager_state->asset_cache.emplace(job_data->asset_handle.uuid, Asset { .load_result = load_result });
    
    HE_LOG(Assets, Trace, "loaded asset: %.*s\n", HE_EXPAND_STRING(asset_entry.path));
    return Job_Result::SUCCEEDED;

}

static bool serialize_asset_registry()
{
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    Memory_Context memory_context = grab_memory_context();

    Asset_Registry &registry = asset_manager_state->asset_registry;
    Asset_Dependency &dependency = asset_manager_state->asset_dependency;

    Asset_Handle *handles = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, Asset_Handle, registry.size());
    
    {
        U32 handle_index = 0;
        for (auto it = registry.ibegin(); it != registry.iend(); ++it)
        {
            handles[handle_index++] = { .uuid = it.key() };
        }

        std::sort(handles, handles + registry.size(), [&dependency](Asset_Handle a, Asset_Handle b)
        {
            U32 a_count = 0;
            U32 b_count = 0;

            {
                const Asset_Registry_Entry &a_entry = internal_get_asset_registry_entry(a);
                if (a_entry.parent.uuid != 0)
                {
                    a_count++;
                }
                if (is_asset_embeded(a))
                {
                    a_count++;
                }
            }

            {
                const Asset_Registry_Entry &b_entry = internal_get_asset_registry_entry(b);
                if (b_entry.parent.uuid != 0)
                {
                    b_count++;
                }
                if (is_asset_embeded(b))
                {
                    b_count++;
                }
            }

            if (a_count != b_count)
            {
                return a_count < b_count;
            }

            return a.uuid < b.uuid;
        });
    }
    
    String_Builder builder = {};
    begin_string_builder(&builder, memory_context.temprary_memory.arena);
    
    append(&builder, "version 1\n");
    append(&builder, "entry_count %u\n", registry.size());

    for (U32 i = 0; i < registry.size(); i++)
    {
        const Asset_Registry_Entry &entry = registry[handles[i].uuid];
        append(&builder, "\nasset %llu\n", handles[i].uuid);
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
    platform_lock_mutex(&asset_manager_state->asset_mutex);
    HE_DEFER { platform_unlock_mutex(&asset_manager_state->asset_mutex); };

    Memory_Context memory_context = grab_memory_context();

    Read_Entire_File_Result file_result = read_entire_file(asset_manager_state->asset_registry_path, memory_context.temp_allocator);
    if (!file_result.success)
    {
        HE_LOG(Assets, Error, "deserialize_asset_registry -- failed to open file: %.*s\n", HE_EXPAND_STRING(asset_manager_state->asset_registry_path));
        return false;
    }

    Asset_Registry &registry = asset_manager_state->asset_registry;
    Asset_Dependency &dependency = asset_manager_state->asset_dependency;
    Embeded_Asset_Cache &embeded_cache = asset_manager_state->embeded_cache;

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
        const Asset_Info *asset_info = get_asset_info_from_extension(extension);
        HE_ASSERT(asset_info);

        Asset_Handle asset_handle = { .uuid = asset_uuid };
        Asset_Handle parent_handle = { .uuid = parent_uuid };

        Asset_Registry_Entry entry = {};
        entry.path = copy_string(path, memory_context.general_allocator);
        entry.type_info_index = index_of(&asset_manager_state->asset_infos, asset_info);
        entry.last_write_time = 0;
        entry.parent = { .uuid = parent_uuid };
        entry.ref_count = 0;
        entry.state = Asset_State::UNLOADED;
        entry.job = Resource_Pool< Job >::invalid_handle;
        
        String absolute_path = internal_get_asset_absolute_path(entry, memory_context.temp_allocator);
        entry.is_deleted = !file_exists(absolute_path);

        registry.emplace(asset_uuid, entry);
        
        Asset_Handle embeder_handle = {};
        bool is_embeded = is_asset_embeded(path, &embeder_handle);
        if (is_embeded && internal_is_asset_handle_valid(embeder_handle))
        {
            internal_add_embeded_asset(embeder_handle, asset_handle);
            internal_add_asset_dependency(embeder_handle, asset_handle);
        }

        if (internal_is_asset_handle_valid(parent_handle))
        {
            internal_add_asset_dependency(parent_handle, asset_handle);
        }
    }

    return true;
}

String format_embedded_asset(Asset_Handle asset_handle, U64 data_id, String name, Allocator allocator)
{
    return format_string(allocator, "@%llu-%llu/%.*s", asset_handle.uuid, data_id, HE_EXPAND_STRING(name));
}