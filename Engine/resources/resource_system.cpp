#include "resource_system.h"

#include "core/engine.h"
#include "core/file_system.h"
#include "core/debugging.h"
#include "core/job_system.h"
#include "core/platform.h"

#include "containers/hash_map.h"

#include "rendering/renderer.h"

#include <stb/stb_image.h>

struct Resource_Type_Info
{
    String name;
    U32 version;
    Resource_Converter converter;
    Resource_Loader loader;
    U32 count;
};

struct Resource_System_State
{
    Memory_Arena *arena;
    Free_List_Allocator *resource_allocator;

    String resource_path;
    Resource_Type_Info resource_type_infos[(U8)Resource_Type::COUNT];
    
    uint32_t resource_count;
    Resource *resources;

    Hash_Map< String, uint32_t > path_to_resource_index;
};

static Resource_System_State *resource_system_state;

#pragma pack(push, 1)

struct Resource_Header
{
    char magic_value[4];
    U32 type;
    U32 version;
};

struct Texture_Resource_Info
{
    uint32_t width;
    uint32_t height;
    Texture_Format format;
    bool mipmapping;
    U64 data_offset;
};

#pragma pack(pop)

Resource_Header make_resource_header(Resource_Type type)
{
    Resource_Header result;
    result.magic_value[0] = 'H';
    result.magic_value[1] = 'O';
    result.magic_value[2] = 'P';
    result.magic_value[3] = 'E';
    result.type = (U32)type;
    result.version = resource_system_state->resource_type_infos[(U32)type].version;
    return result;
}

bool condition_texture_resource(const String &path, const String &output_path, Temprary_Memory_Arena *temp_arena)
{
    Read_Entire_File_Result read_result = read_entire_file(path.data, temp_arena);
    if (!read_result.success)
    {
        return false;
    }
    
    S32 width;
    S32 height;
    S32 channels;
    stbi_uc *pixels = stbi_load_from_memory(read_result.data, read_result.size, &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels)
    {
        return false;
    }
    HE_DEFER { stbi_image_free(pixels); };
    
    Resource_Header header = make_resource_header(Resource_Type::TEXTURE);

    Texture_Resource_Info info =
    {
        .width = (U32)width,
        .height = (U32)height,
        .format = Texture_Format::R8G8B8A8_SRGB,
        .mipmapping = true,
        .data_offset = sizeof(Resource_Header) + sizeof(Texture_Resource_Info)
    };
    
    U8 *buffer = temp_arena->arena->base + temp_arena->arena->offset;
    U64 size = 0;

    memcpy(buffer + size, &header, sizeof(Resource_Header));
    size += sizeof(Resource_Header);

    memcpy(buffer + size, &info, sizeof(Texture_Resource_Info));
    size += sizeof(Texture_Resource_Info);

    U64 data_size = sizeof(U32) * width * height;
    memcpy(buffer + size, pixels, data_size);
    size += data_size;

    bool success = write_entire_file(output_path.data, buffer, size);
    return success;
}

static Resource_Type_Info* find_resource_type_from_extension(const String &extension)
{
    for (U32 i = 0; i < (U32)Resource_Type::COUNT; i++)
    {
        Resource_Converter &converter = resource_system_state->resource_type_infos[i].converter;
        for (U32 j = 0; j < converter.extension_count; j++)
        {
            if (converter.extensions[j] == extension)
            {
                return &resource_system_state->resource_type_infos[i];
            }
        }
    }

    return nullptr;
}

static void calculate_resource_count(const char *data, U64 count)
{
    String path = { data, count };
    String extension = get_extension(path);
    Resource_Type_Info *resource_type_info = find_resource_type_from_extension(extension);
    if (resource_type_info)
    {
        resource_type_info->count++;
        resource_system_state->resource_count++;
    }
}

struct Convert_Resource_Job_Data
{
    convert_resource_proc convert;
    String path;
    String output_path;
};

Job_Result convert_resource_job(const Job_Parameters &params)
{
    Convert_Resource_Job_Data *job_data = (Convert_Resource_Job_Data *)params.data;
    if (!job_data->convert(job_data->path, job_data->output_path, params.temprary_memory_arena))
    {
        HE_LOG(Resource, Trace, "failed to converted resource: %.*s\n", HE_EXPAND_STRING(job_data->path));
        return Job_Result::FAILED;
    }
    HE_LOG(Resource, Trace, "successfully converted resource: %.*s\n", HE_EXPAND_STRING(job_data->path));
    return Job_Result::SUCCEEDED;
}

static void walk_resource_directory(const char *data, U64 count)
{
    String path = { data, count };
    String relative_path = sub_string(path, resource_system_state->resource_path.count + 1);
    String extension = get_extension(relative_path);
    Resource_Type_Info *resource_type_info = find_resource_type_from_extension(extension);
    if (!resource_type_info)
    {
        return;
    }

    uint32_t resource_index = resource_system_state->resource_count++;
    Resource &resource = resource_system_state->resources[resource_index];
    
    platform_create_mutex(&resource.mutex);
    resource.state = Resource_State::UNLOADED;
    resource.ref_count = 0;
    resource.size = 0;
    resource.data = nullptr;
    
    String parent_path = get_parent_path(path);
    String name = get_name(relative_path);
    String pp = format_string(resource_system_state->arena, "%.*s", HE_EXPAND_STRING(path)); // @Leak
    String resource_path = format_string(resource_system_state->arena, "%.*s/%.*s.hres", HE_EXPAND_STRING(parent_path), HE_EXPAND_STRING(name)); // @Leak
    String relative_resource_path = sub_string(resource_path, resource_system_state->resource_path.count + 1);
    
    Renderer_Semaphore_Descriptor semaphore_descriptor =
    {
        .initial_value = 0
    };

    resource.allocation_group.resource_name = relative_resource_path;
    resource.allocation_group.type = Allocation_Group_Type::GENERAL;
    resource.allocation_group.semaphore = renderer_create_semaphore(semaphore_descriptor);

    insert(&resource_system_state->path_to_resource_index, relative_resource_path, resource_index);

    bool always_convert = false; // todo(amer): temprary for testing...
    if (always_convert || !file_exists(resource_path))
    {
        Convert_Resource_Job_Data convert_resource_job_data = 
        {
            .convert = resource_type_info->converter.convert,
            .path = pp,
            .output_path = resource_path
        };

        Job job = {};
        job.parameters.data = &convert_resource_job_data;
        job.parameters.size = sizeof(convert_resource_job_data);
        job.proc = convert_resource_job;
        execute_job(job);
    }
}

static bool load_texture_resource(Resource *resource)
{
    Texture_Resource_Info info;
    platform_read_data_from_file(&resource->open_file_result, sizeof(Resource_Header), &info, sizeof(Texture_Resource_Info));

    if (info.format >= Texture_Format::COUNT || info.width == 0 || info.height == 0)
    {
        return false;
    }
    
    U64 size = sizeof(Resource_Header) + sizeof(Texture_Resource_Info) + sizeof(U32) * info.width * info.height;
    if (resource->open_file_result.size != size)
    {
        return false;
    }
    
    U64 data_size = sizeof(U32) * info.width * info.height;
    U32 *data = HE_ALLOCATE_ARRAY(resource_system_state->resource_allocator, U32, info.width * info.height);
    platform_read_data_from_file(&resource->open_file_result, info.data_offset, data, data_size);

    void *datas[] = { data };
    append(&resource->allocation_group.allocations, (void*)data);
    
    Texture_Descriptor texture_descriptor =
    {
        .width = info.width,
        .height = info.height,
        .format = info.format,
        .data = to_array_view(datas),
        .mipmapping = info.mipmapping,
        .sample_count = 1,
        .allocation_group = &resource->allocation_group,
    };

    Texture_Handle texture_handle = renderer_create_texture(texture_descriptor);
    resource->index = texture_handle.index;
    resource->generation = texture_handle.generation;
    while (renderer_get_semaphore_value(resource->allocation_group.semaphore) != resource->allocation_group.target_value)
    {
    }
    resource->state = Resource_State::LOADED;
    HE_LOG(Resource, Trace, "loaded resource: %.*s\n", HE_EXPAND_STRING(resource->allocation_group.resource_name));
    return true;
}

static void unload_texture_resource(Resource *resource)
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
    resource_system_state->arena = &engine->memory.transient_arena;
    
    String working_directory = get_current_working_directory(arena);
    sanitize_path(working_directory);

    String resource_path = format_string(arena, "%.*s/%.*s", HE_EXPAND_STRING(working_directory), HE_EXPAND_STRING(resource_directory_name));
    if (!directory_exists(resource_path))
    {
        HE_LOG(Resource, Fetal, "invalid resource path: %.*s\n", HE_EXPAND_STRING(resource_path));
        return false;
    }
    Render_Context render_context = get_render_context();
    resource_system_state->resource_path = resource_path;
    resource_system_state->resource_allocator = &render_context.renderer_state->transfer_allocator; 
    
    Resource_Converter texture_converter = {};
    texture_converter.convert = &condition_texture_resource;
    
    static String texture_extensions[] =
    {
        HE_STRING_LITERAL("jpeg"),
        HE_STRING_LITERAL("png"),
        HE_STRING_LITERAL("tga"),
        HE_STRING_LITERAL("psd")
    };
    texture_converter.extension_count = HE_ARRAYCOUNT(texture_extensions);
    texture_converter.extensions = texture_extensions;

    Resource_Loader texture_loader;
    texture_loader.load = &load_texture_resource;
    texture_loader.unload = &unload_texture_resource;
    register_resource(Resource_Type::TEXTURE, "texture", 1, texture_converter, texture_loader);
    
    bool recursive = true;
    platform_walk_directory(resource_path.data, recursive, &calculate_resource_count);
    
    resource_system_state->resources = HE_ALLOCATE_ARRAY(&engine->memory.permanent_arena, Resource, resource_system_state->resource_count);
    init(&resource_system_state->path_to_resource_index, &engine->memory.permanent_arena, resource_system_state->resource_count);

    resource_system_state->resource_count = 0;
    platform_walk_directory(resource_path.data, recursive, &walk_resource_directory);

    Resource_Ref cube_base_color = aquire_resource(HE_STRING_LITERAL("cube_base_color.hres"));
    return true;
}

void deinit_resource_system()
{
}

bool register_resource(Resource_Type type, const char *name, U32 version, Resource_Converter converter, Resource_Loader loader)
{
    if (!name)
    {
        HE_LOG(Resource, Trace, "name is null");
        return false;
    }

    Resource_Type_Info &resource_type_info = resource_system_state->resource_type_infos[(U32)type];
    resource_type_info.name = HE_STRING(name);
    resource_type_info.version = version;
    resource_type_info.converter = converter;
    resource_type_info.loader = loader;
    return true;
}

bool is_valid(Resource_Ref ref)
{
    return ref.index != -1 && ref.index >= 0 && (U32)ref.index < resource_system_state->resource_count; 
}

struct Load_Resource_Job_Data
{
    String path;
    Resource *resource;
};

Job_Result load_resource_job(const Job_Parameters &params)
{
    Load_Resource_Job_Data *job_data = (Load_Resource_Job_Data *)params.data;

    Resource *resource = job_data->resource;
    platform_lock_mutex(&resource->mutex);
    HE_DEFER {  platform_unlock_mutex(&resource->mutex); };

    String path = format_string(params.temprary_memory_arena->arena, "%.*s/%.*s", HE_EXPAND_STRING(resource_system_state->resource_path), HE_EXPAND_STRING(job_data->path));
    
    Open_File_Result open_file_result = platform_open_file(path.data, OpenFileFlag_Read);
    if (!open_file_result.handle)
    {
        return Job_Result::FAILED;
    }
    
    HE_DEFER {  platform_close_file(&open_file_result); };

    if (open_file_result.size < sizeof(Resource_Header))
    {
        return Job_Result::ABORTED;
    }
    
    Resource_Header header = {};
    platform_read_data_from_file(&open_file_result, 0, &header, sizeof(header));
    
    if (strncmp(header.magic_value, "HOPE", 4) != 0)
    {
        return Job_Result::ABORTED;
    }

    if (header.type > (U32)Resource_Type::COUNT)
    {
        return Job_Result::ABORTED;
    }

    Resource_Type_Info &info = resource_system_state->resource_type_infos[header.type]; 
    if (header.version > info.version)
    {
        return Job_Result::ABORTED;
    }
    
    resource->open_file_result = open_file_result;
    bool success = info.loader.load(resource);
    
    if (!success)
    {
        return Job_Result::FAILED;
    }

    return Job_Result::SUCCEEDED; 
}

Resource_Ref aquire_resource(const String &path)
{
    Hash_Map_Iterator it = find(&resource_system_state->path_to_resource_index, path);
    if (!is_valid(it))
    {
        return { -1 };
    }

    Resource *resource = &resource_system_state->resources[*it.value];
    platform_lock_mutex(&resource->mutex);

    if (resource->state == Resource_State::UNLOADED)
    {
        resource->state = Resource_State::PENDING;
        platform_unlock_mutex(&resource->mutex);

        Load_Resource_Job_Data job_data
        {
            .path = path,
            .resource = resource
        };

        Job job = {};
        job.parameters.data = &job_data;
        job.parameters.size = sizeof(job_data);
        job.proc = load_resource_job;
        execute_job(job);
    }
    else
    {
        resource->ref_count++;
        platform_unlock_mutex(&resource->mutex);
    }

    Resource_Ref ref = { (S32)*it.value };
    return ref;
}

void release_resource(Resource_Ref ref)
{
}