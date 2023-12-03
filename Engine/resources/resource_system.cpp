#include "resource_system.h"

#include "core/engine.h"
#include "core/file_system.h"
#include "core/debugging.h"
#include "core/job_system.h"
#include "core/platform.h"

#include "containers/hash_map.h"

#include "rendering/renderer.h"

#include <stb/stb_image.h>
#include <unordered_map> // todo(amer): to be removed

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
    Free_List_Allocator *free_list_allocator;
    Free_List_Allocator *resource_allocator;

    String resource_path;
    Resource_Type_Info resource_type_infos[(U8)Resource_Type::COUNT];
    
    Dynamic_Array< Resource > resources;
};

static std::unordered_map< U64, U32 > uuid_to_resource_index;

template<>
struct std::hash<String>
{
    std::size_t operator()(const String &str) const
    {
        return he_hash(str);
    }
};

static std::unordered_map< String, U32 > string_to_resource_index;

static Resource_System_State *resource_system_state;

#pragma pack(push, 1)

struct Resource_Header
{
    char magic_value[4];
    U32 type;
    U32 version;
    U64 uuid;
    U16 resource_ref_count;
};

struct Texture_Resource_Info
{
    uint32_t width;
    uint32_t height;
    Texture_Format format;
    bool mipmapping;
    U64 data_offset;
};

struct Shader_Resource_Info
{
    U64 data_offset;
    U64 data_size;
};

struct Material_Resource_Info
{
    U64 shader_count;
    U64 shaders[16];

    U64 render_pass_name_count;
    char render_pass_name[256];

    Cull_Mode cull_mode;
    Front_Face front_face;
    Fill_Mode fill_mode;
    bool sample_shading;
};

#pragma pack(pop)

Resource_Header make_resource_header(Resource_Type type, U64 uuid)
{
    Resource_Header result;
    result.magic_value[0] = 'H';
    result.magic_value[1] = 'O';
    result.magic_value[2] = 'P';
    result.magic_value[3] = 'E';
    result.type = (U32)type;
    result.version = resource_system_state->resource_type_infos[(U32)type].version;
    result.uuid = uuid;
    result.resource_ref_count = 0;
    return result;
}

#include <random>

static U64 generate_uuid()
{
    static std::random_device device;
    static std::mt19937 engine(device());
    static std::uniform_int_distribution<U64> dist(0, HE_MAX_U64);
    U64 uuid = HE_MAX_U64;
    do
    {
        uuid = dist(engine);
    }
    while (uuid_to_resource_index.find(uuid) != uuid_to_resource_index.end());
    return uuid;
}

// ========================== Resources ====================================

static bool convert_texture_to_resource(const String &path, const String &output_path, Resource *resource, Temprary_Memory_Arena *temp_arena)
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

    Open_File_Result open_file_result = platform_open_file(output_path.data, (Open_File_Flags)(OpenFileFlag_Write|OpenFileFlag_Truncate));
    if (!open_file_result.success)
    {
        return false;
    }
    HE_DEFER { platform_close_file(&open_file_result); };
    
    U64 offset = 0;
    bool success = true;
    
    Resource_Header header = make_resource_header(Resource_Type::TEXTURE, resource->uuid);
    
    success &= platform_write_data_to_file(&open_file_result, offset, &header, sizeof(Resource_Header));
    offset += sizeof(Resource_Header);

    Texture_Resource_Info texture_resource_info =
    {
        .width = (U32)width,
        .height = (U32)height,
        .format = Texture_Format::R8G8B8A8_SRGB,
        .mipmapping = true,
        .data_offset = sizeof(Resource_Header) + sizeof(Texture_Resource_Info)
    };

    success &= platform_write_data_to_file(&open_file_result, offset, &texture_resource_info, sizeof(Texture_Resource_Info));
    offset += sizeof(Texture_Resource_Info);

    success &= platform_write_data_to_file(&open_file_result, offset, pixels, width * height * sizeof(U32));
    
    return success;
}

static bool load_texture_resource(Open_File_Result *open_file_result, Resource *resource)
{
    Texture_Resource_Info info;
    platform_read_data_from_file(open_file_result, sizeof(Resource_Header), &info, sizeof(Texture_Resource_Info));

    if (info.format >= Texture_Format::COUNT || info.width == 0 || info.height == 0)
    {
        return false;
    }
    
    U64 size = sizeof(Resource_Header) + sizeof(Texture_Resource_Info) + sizeof(U32) * info.width * info.height;
    if (open_file_result->size != size)
    {
        return false;
    }
    
    U64 data_size = sizeof(U32) * info.width * info.height;
    U32 *data = HE_ALLOCATE_ARRAY(resource_system_state->resource_allocator, U32, info.width * info.height);
    platform_read_data_from_file(open_file_result, info.data_offset, data, data_size);

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
    return true;
}

static void unload_texture_resource(Resource *resource)
{
    HE_ASSERT(resource->state == Resource_State::LOADED);
    Texture_Handle texture_handle = { resource->index, resource->generation };
    renderer_destroy_texture(texture_handle);
}

static bool convert_shader_to_resource(const String &path, const String &output_path, Resource *resource, Temprary_Memory_Arena *temp_arena)
{
    // for debugging > cmd.txt
    String command = format_string(temp_arena->arena, "glslangValidator.exe -V --auto-map-locations %.*s -o %.*s > cmd.txt", HE_EXPAND_STRING(path), HE_EXPAND_STRING(output_path));
    platform_execute_command(command.data);
    
    Read_Entire_File_Result spirv_binary_read_result = read_entire_file(output_path.data, temp_arena);
    if (!spirv_binary_read_result.success)
    {
        return false;
    }
    
    Open_File_Result open_file_result = platform_open_file(output_path.data, Open_File_Flags(OpenFileFlag_Write|OpenFileFlag_Truncate));
    if (!open_file_result.success)
    {
        return false;
    }

    bool success = true;
    U64 offset = 0;

    Resource_Header header = make_resource_header(Resource_Type::SHADER, resource->uuid);
    
    success &= platform_write_data_to_file(&open_file_result, offset, &header, sizeof(header));
    offset += sizeof(header);

    Shader_Resource_Info info =
    {
        .data_offset = sizeof(Resource_Header) + sizeof(Shader_Resource_Info),
        .data_size = spirv_binary_read_result.size
    };

    success &= platform_write_data_to_file(&open_file_result, offset, &info, sizeof(info));
    offset += sizeof(info);

    success &= platform_write_data_to_file(&open_file_result, offset, spirv_binary_read_result.data, spirv_binary_read_result.size);
    offset += spirv_binary_read_result.size;

    success &= platform_close_file(&open_file_result);
    return success;
}

static bool load_shader_resource(Open_File_Result *open_file_result, Resource *resource)
{
    bool success = true;
    
    Shader_Resource_Info info;
    success &= platform_read_data_from_file(open_file_result, sizeof(Resource_Header), &info, sizeof(info));

    U8 *data = HE_ALLOCATE_ARRAY(resource_system_state->resource_allocator, U8, info.data_size);
    success &= platform_read_data_from_file(open_file_result, info.data_offset, data, info.data_size);

    if (!success)
    {
        resource->ref_count = 0;
        return false;
    }
    
    Shader_Descriptor shader_descriptor =
    {
        .data = data,
        .size = info.data_size
    };

    Shader_Handle shader_handle = renderer_create_shader(shader_descriptor);
    resource->index = shader_handle.index;
    resource->generation = shader_handle.generation;
    resource->ref_count++;
    resource->state = Resource_State::LOADED;
    return true;
}

static void unload_shader_resource(Resource *resource)
{
    HE_ASSERT(resource->state == Resource_State::LOADED);
    Shader_Handle shader_handle = { resource->index, resource->generation };
    renderer_destroy_shader(shader_handle);
}

static bool load_material_resource(Open_File_Result *open_file_result, Resource *resource)
{
    bool success = true;

    Material_Resource_Info info;
    success &= platform_read_data_from_file(open_file_result, sizeof(Resource_Header), &info, sizeof(info));

    for (int i = 0; i < info.shader_count; i++)
    {
        Resource_Ref shader_ref = { info.shaders[i] };
    }

    String render_pass = { info.render_pass_name, info.render_pass_name_count };

    // Cull_Mode cull_mode;
    // Front_Face front_face;
    // Fill_Mode fill_mode;
    // bool sample_shading;

    return success;
}

static void unload_material_resource(Resource *resource)
{
    HE_ASSERT(resource->state == Resource_State::LOADED);
    Shader_Handle shader_handle = { resource->index, resource->generation };
    renderer_destroy_shader(shader_handle);
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

//==================================== Jobs ==================================================

struct Convert_Resource_Job_Data
{
    convert_resource_proc convert;
    Resource *resource;
    String path;
    String output_path;
};

static Job_Result convert_resource_job(const Job_Parameters &params)
{
    Convert_Resource_Job_Data *job_data = (Convert_Resource_Job_Data *)params.data;
    if (!job_data->convert(job_data->path, job_data->output_path, job_data->resource, params.temprary_memory_arena))
    {
        HE_LOG(Resource, Trace, "failed to converted resource: %.*s\n", HE_EXPAND_STRING(job_data->path));
        return Job_Result::FAILED;
    }
    HE_LOG(Resource, Trace, "successfully converted resource: %.*s\n", HE_EXPAND_STRING(job_data->path));
    return Job_Result::SUCCEEDED;
}

struct Load_Resource_Job_Data
{
    String path;
    Resource *resource;
};

static Job_Result load_resource_job(const Job_Parameters &params)
{
    Load_Resource_Job_Data *job_data = (Load_Resource_Job_Data *)params.data;

    Resource *resource = job_data->resource;
    bool use_allocation_group = resource_system_state->resource_type_infos[resource->type].loader.use_allocation_group;  
    
    if (use_allocation_group)
    {
        Renderer_Semaphore_Descriptor semaphore_descriptor =
        {
            .initial_value = 0
        };

        resource->allocation_group.resource_name = job_data->path;
        resource->allocation_group.type = Allocation_Group_Type::GENERAL;
        resource->allocation_group.semaphore = renderer_create_semaphore(semaphore_descriptor);
        resource->allocation_group.resource_index = (S32)(resource - resource_system_state->resources.data);
    }

    platform_lock_mutex(&resource->mutex);
    HE_DEFER {  platform_unlock_mutex(&resource->mutex); };

    String path = format_string(params.temprary_memory_arena->arena, "%.*s/%.*s", HE_EXPAND_STRING(resource_system_state->resource_path), HE_EXPAND_STRING(job_data->path));
    
    Open_File_Result open_file_result = platform_open_file(path.data, OpenFileFlag_Read);
    if (!open_file_result.handle)
    {
        resource->ref_count = 0;
        return Job_Result::FAILED;
    }
    
    HE_DEFER {  platform_close_file(&open_file_result); };

    if (open_file_result.size < sizeof(Resource_Header))
    {
        resource->ref_count = 0;
        return Job_Result::ABORTED;
    }
    
    Resource_Header header = {};
    platform_read_data_from_file(&open_file_result, 0, &header, sizeof(header));
    
    if (strncmp(header.magic_value, "HOPE", 4) != 0)
    {
        resource->ref_count = 0;
        return Job_Result::ABORTED;
    }

    if (header.type > (U32)Resource_Type::COUNT)
    {
        resource->ref_count = 0;
        return Job_Result::ABORTED;
    }

    Resource_Type_Info &info = resource_system_state->resource_type_infos[header.type]; 
    if (header.version > info.version)
    {
        resource->ref_count = 0;
        return Job_Result::ABORTED;
    }
    
    resource->type = header.type;
    resource->uuid = header.uuid;

    if (header.resource_ref_count)
    {
        U64 *uuids = HE_ALLOCATE_ARRAY(params.temprary_memory_arena->arena, U64, header.resource_ref_count);
        Mutex *mutexes = HE_ALLOCATE_ARRAY(params.temprary_memory_arena->arena, Mutex, header.resource_ref_count);

        bool read = platform_read_data_from_file(&open_file_result, sizeof(Resource_Header), uuids, sizeof(U64) * header.resource_ref_count);
        if (read)
        {
            set_capacity(&resource->resource_refs, header.resource_ref_count);
            memcpy(resource->resource_refs.data, uuids, sizeof(U64) * header.resource_ref_count);

            for (int i = 0; i < header.resource_ref_count; i++)
            {
                Resource_Ref ref = { uuids[i] };
                auto it = uuid_to_resource_index.find(uuids[i]);
                if (it != uuid_to_resource_index.end())
                {
                    aquire_resource(ref);
                }
                mutexes[i] = resource_system_state->resources[it->second].mutex;
            }

            platform_wait_for_mutexes(mutexes, header.resource_ref_count);
        }
    }

    bool success = info.loader.load(&open_file_result, resource);

    if (!success)
    {
        resource->ref_count = 0;
        return Job_Result::FAILED;
    }

    if (use_allocation_group)
    {
        Render_Context context = get_render_context();
        Renderer_State *renderer_state = context.renderer_state;
        platform_lock_mutex(&renderer_state->allocation_groups_mutex);
        append(&renderer_state->allocation_groups, resource->allocation_group);
        platform_unlock_mutex(&renderer_state->allocation_groups_mutex);
    }
    
    return Job_Result::SUCCEEDED; 
}

// ======================================================================================

static void walk_resource_directory(const char *data, U64 count)
{
    String absolute_path = { data, count };
    String relative_path = sub_string(absolute_path, resource_system_state->resource_path.count + 1);
    String extension = get_extension(relative_path);
    Resource_Type_Info *resource_type_info = find_resource_type_from_extension(extension);
    if (!resource_type_info)
    {
        return;
    }

    Resource &resource = append(&resource_system_state->resources);
    init(&resource.resource_refs, resource_system_state->free_list_allocator);

    U32 resource_index = index_of(&resource_system_state->resources, resource);

    platform_create_mutex(&resource.mutex);
    resource.state = Resource_State::UNLOADED;
    resource.ref_count = 0;
    
    String parent_absolute_path = get_parent_path(absolute_path);
    String name = get_name(relative_path);
    String resource_path = format_string(resource_system_state->arena, "%.*s/%.*s.hres", HE_EXPAND_STRING(parent_absolute_path), HE_EXPAND_STRING(name)); // @Leak
    String relative_resource_path = sub_string(resource_path, resource_system_state->resource_path.count + 1);

    U64 uuid = generate_uuid();
    resource.uuid = uuid;

    uuid_to_resource_index.emplace(uuid, resource_index);
    string_to_resource_index.emplace(relative_resource_path, resource_index);

    String asset_absloute_path = format_string(resource_system_state->arena, "%.*s", HE_EXPAND_STRING(absolute_path));
    resource.asset_absloute_path = asset_absloute_path;
    resource.absloute_path = resource_path;
    resource.relative_path = relative_resource_path;

    bool always_convert = true; // todo(amer): temprary for testing...
    if (always_convert || !file_exists(resource_path))
    {
        Convert_Resource_Job_Data convert_resource_job_data = 
        {
            .convert = resource_type_info->converter.convert,
            .resource = &resource,
            .path = asset_absloute_path,
            .output_path = resource_path
        };

        Job job = {};
        job.parameters.data = &convert_resource_job_data;
        job.parameters.size = sizeof(convert_resource_job_data);
        job.proc = convert_resource_job;
        execute_job(job);
    }
}

bool init_resource_system(const String &resource_directory_name, Engine *engine)
{
    if (resource_system_state)
    {
        HE_LOG(Resource, Fetal, "resource system already initialized\n");
        return false;
    }

    uuid_to_resource_index[HE_MAX_U64] = -1;

    Memory_Arena *arena = &engine->memory.permanent_arena;
    resource_system_state = HE_ALLOCATE(arena, Resource_System_State);
    resource_system_state->arena = &engine->memory.transient_arena;
    resource_system_state->free_list_allocator = &engine->memory.free_list_allocator;
    init(&resource_system_state->resources, &engine->memory.free_list_allocator);

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
    
    static String texture_extensions[] =
    {
        HE_STRING_LITERAL("jpeg"),
        HE_STRING_LITERAL("png"),
        HE_STRING_LITERAL("tga"),
        HE_STRING_LITERAL("psd")
    };

    Resource_Converter texture_converter =
    {
        .extension_count = HE_ARRAYCOUNT(texture_extensions), 
        .extensions = texture_extensions,
        .convert = &convert_texture_to_resource
    };

    Resource_Loader texture_loader = 
    {
        .use_allocation_group = true,
        .load = &load_texture_resource,
        .unload = &unload_texture_resource
    };

    register_resource(Resource_Type::TEXTURE, "texture", 1, texture_converter, texture_loader);

    static String shader_extensions[] =
    {
        HE_STRING_LITERAL("vert"),
        HE_STRING_LITERAL("frag"),
    };

    Resource_Converter shader_converter =
    {
        .extension_count = HE_ARRAYCOUNT(shader_extensions),
        .extensions = shader_extensions,
        .convert = &convert_shader_to_resource,
    };
    
    Resource_Loader shader_loader = 
    {
        .use_allocation_group = false,
        .load = &load_shader_resource,
        .unload = &unload_shader_resource,
    };

    register_resource(Resource_Type::SHADER, "shader", 1, shader_converter, shader_loader);

    bool recursive = true;
    platform_walk_directory(resource_path.data, recursive, &walk_resource_directory);

    // Resource_Ref cube_base_color = aquire_resource(HE_STRING_LITERAL("cube_base_color.hres"));
    // Resource_Ref opaque_pbr = aquire_resource(HE_STRING_LITERAL("opaque_pbr.hres"));
    return true;
}

void deinit_resource_system()
{
}

bool register_resource(Resource_Type type, const char *name, U32 version, Resource_Converter converter, Resource_Loader loader)
{
    HE_ASSERT(name);
    HE_ASSERT(version);
    Resource_Type_Info &resource_type_info = resource_system_state->resource_type_infos[(U32)type];
    resource_type_info.name = HE_STRING(name);
    resource_type_info.version = version;
    resource_type_info.converter = converter;
    resource_type_info.loader = loader;
    return true;
}

bool is_valid(Resource_Ref ref)
{
    return ref.uuid != HE_MAX_U64 && uuid_to_resource_index.find(ref.uuid) != uuid_to_resource_index.end();
}

static void aquire_resource(Resource *resource)
{
    platform_lock_mutex(&resource->mutex);

    if (resource->state == Resource_State::UNLOADED)
    {
        resource->state = Resource_State::PENDING;
        platform_unlock_mutex(&resource->mutex);

        Load_Resource_Job_Data job_data
        {
            .path = resource->absloute_path,
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
}

Resource_Ref aquire_resource(const String &path)
{
    auto it = string_to_resource_index.find(path);
    if (it == string_to_resource_index.end())
    {
        return { HE_MAX_U64 };
    }
    Resource *resource = &resource_system_state->resources[it->second];
    aquire_resource(resource);
    Resource_Ref ref = { resource->uuid };
    return ref;
}

bool aquire_resource(Resource_Ref ref)
{
    auto it = uuid_to_resource_index.find(ref.uuid);
    if (it == uuid_to_resource_index.end())
    {
        return false;
    }
    Resource *resource = &resource_system_state->resources[it->second];
    aquire_resource(resource);
    return true;
}

void release_resource(Resource_Ref ref)
{
    HE_ASSERT(is_valid(ref));
    auto it = uuid_to_resource_index.find(ref.uuid);
    HE_ASSERT(it != uuid_to_resource_index.end());
    Resource *resource = &resource_system_state->resources[it->second];
    HE_ASSERT(resource->ref_count);
    platform_lock_mutex(&resource->mutex);
    resource->ref_count--;
    if (resource->ref_count == 0)
    {
        Resource_Type_Info &info = resource_system_state->resource_type_infos[resource->type];
        info.loader.unload(resource);
        resource->index = -1;
        resource->generation = 0;
        resource->state = Resource_State::UNLOADED;
    }
    platform_unlock_mutex(&resource->mutex);
}

Resource *get_resource(Resource_Ref ref)
{
    HE_ASSERT(is_valid(ref));
    auto it = uuid_to_resource_index.find(ref.uuid);
    return &resource_system_state->resources[it->second];
}