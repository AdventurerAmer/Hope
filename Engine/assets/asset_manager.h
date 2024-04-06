#pragma once

#include "core/defines.h"
#include "core/job_system.h"
#include "containers/string.h"

struct Load_Asset_Result
{
    bool success;

    union
    {
        struct
        {
            void *data;
            U64 size;
        };

        struct
        {
            S32 index;
            U32 generation;
        };
    };
};

enum class Asset_State : U8
{
    UNLOADED,
    PENDING,
    LOADED,
};

struct Asset_Handle
{
    U64 uuid;

    HE_FORCE_INLINE bool operator==(Asset_Handle other)
    {
        return uuid == other.uuid;
    }

    HE_FORCE_INLINE bool operator!=(Asset_Handle other)
    {
        return uuid != other.uuid;
    }
};

struct Embeded_Asset_Params
{
    String name;
    U16 type_info_index;
    U64 data_id;
};

typedef void (*on_import_asset_proc)(Asset_Handle asset_handle);
typedef Load_Asset_Result (*load_asset_proc)(String path, const Embeded_Asset_Params *params);
typedef void (*unload_asset_proc)(Load_Asset_Result result);

struct Asset_Info
{
    String name;
    String *extensions;
    U32 extension_count;
    load_asset_proc load;
    unload_asset_proc unload;
    on_import_asset_proc on_import;
};

struct Asset_Registry_Entry
{
    String path;
    U16 type_info_index;
    Asset_Handle parent;

    U32 ref_count;
    Asset_State state;
    Job_Handle job;
};

bool init_asset_manager(String asset_path);
void deinit_asset_manager();

String get_asset_path();

bool register_asset(String name, Array_View< String > extensions, load_asset_proc load, unload_asset_proc unload, on_import_asset_proc on_import = nullptr);

bool is_asset_handle_valid(Asset_Handle asset_handle);

bool is_asset_loaded(Asset_Handle asset_handle);

Job_Handle aquire_asset(Asset_Handle asset_handle);

Load_Asset_Result get_asset(Asset_Handle asset_handle);

void release_asset(Asset_Handle asset_handle);

Asset_Handle get_asset_handle(String path);
Asset_Handle import_asset(String path);

void set_parent(Asset_Handle asset, Asset_Handle parent);

bool is_asset_embeded(String path, Asset_Handle *out_parent = nullptr, U64 *out_data_id = nullptr);
bool is_asset_embeded(Asset_Handle asset_handle);
Array_View< U64 > get_embeded_assets(Asset_Handle asset_handle);

const Asset_Registry_Entry& get_asset_registry_entry(Asset_Handle asset_handle);
const Asset_Info* get_asset_info_from_extension(String extension);
const Asset_Info* get_asset_info(Asset_Handle asset_handle);
const Asset_Info* get_asset_info(String name);
const Asset_Info* get_asset_info(U16 type_info_index);

template< typename T >
T* get_asset_as(Asset_Handle asset_handle)
{
    return (T *)get_asset(asset_handle).data;
}

template< typename T >
Resource_Handle< T > get_asset_handle_as(Asset_Handle asset_handle)
{
    Load_Asset_Result result = get_asset(asset_handle);
    return { .index = result.index, .generation = result.generation };
}

String format_embedded_asset(Asset_Handle asset_handle, U64 data_id, String name, Memory_Arena *arena);