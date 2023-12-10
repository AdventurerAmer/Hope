#include "resource_system.h"

#include "core/engine.h"
#include "core/file_system.h"
#include "core/debugging.h"
#include "core/job_system.h"
#include "core/platform.h"

#include "containers/hash_map.h"

#include "rendering/renderer.h"

#include <stb/stb_image.h>
#include <ImGui/imgui.h>
#include <unordered_map> // todo(amer): to be removed

struct Resource_Type_Info
{
    String name;
    U32 version;
    Resource_Conditioner conditioner;
    Resource_Loader loader;
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
static constexpr const char *resource_extension = "hres";

template<>
struct std::hash<String>
{
    std::size_t operator()(const String &str) const
    {
        return he_hash(str);
    }
};

static std::unordered_map< String, U32 > path_to_resource_index;

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
    Pipeline_State_Settings settings;

    U64 render_pass_name_count;
    U64 render_pass_name_offset;

    U64 data_size;
    U64 data_offset;
};

#pragma pack(pop)

Resource_Header make_resource_header(U32 type, U64 uuid)
{
    Resource_Header result;
    result.magic_value[0] = 'H';
    result.magic_value[1] = 'O';
    result.magic_value[2] = 'P';
    result.magic_value[3] = 'E';
    result.type = type;
    result.version = resource_system_state->resource_type_infos[type].version;
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
    
    uuid_to_resource_index.emplace(uuid, HE_MAX_U32);
    return uuid;
}

// ========================== Resources ====================================

static bool condition_texture_to_resource(const String &asset_path, const String &resource_path, Temprary_Memory_Arena *temp_arena)
{
    Read_Entire_File_Result read_result = read_entire_file(asset_path.data, temp_arena);
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

    Open_File_Result open_file_result = platform_open_file(resource_path.data, (Open_File_Flags)(OpenFileFlag_Write|OpenFileFlag_Truncate));
    if (!open_file_result.success)
    {
        return false;
    }
    HE_DEFER { platform_close_file(&open_file_result); };
    
    U64 offset = 0;
    bool success = true;
    
    U64 uuid = generate_uuid();
    Resource_Header header = make_resource_header((U32)Resource_Type::TEXTURE, uuid);
    
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

static bool condition_shader_to_resource(const String &asset_path, const String &resource_path, Temprary_Memory_Arena *temp_arena)
{
    String command = format_string(temp_arena->arena, "glslangValidator.exe -V --auto-map-locations %.*s -o %.*s", HE_EXPAND_STRING(asset_path), HE_EXPAND_STRING(resource_path));
    bool executed = platform_execute_command(command.data);
    HE_ASSERT(executed);

    Read_Entire_File_Result spirv_binary_read_result = read_entire_file(resource_path.data, temp_arena);
    if (!spirv_binary_read_result.success)
    {
        return false;
    }
    
    Open_File_Result open_file_result = platform_open_file(resource_path.data, Open_File_Flags(OpenFileFlag_Write|OpenFileFlag_Truncate));
    if (!open_file_result.success)
    {
        return false;
    }

    bool success = true;
    U64 offset = 0;

    U64 uuid = generate_uuid();
    Resource_Header header = make_resource_header((U32)Resource_Type::SHADER, uuid);
    
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

static bool condition_material_to_resource(const String &asset_path, const String &resource_path, Temprary_Memory_Arena *arena)
{
    return true;
}

static bool save_material_resource(Resource *resource, Open_File_Result *open_file_result, struct Temprary_Memory_Arena *arena)
{
    Material *material = renderer_get_material({ resource->index, resource->generation });
    Pipeline_State *pipeline_state = renderer_get_pipeline_state(material->pipeline_state_handle);
    
    Render_Pass *render_pass = renderer_get_render_pass(pipeline_state->descriptor.render_pass);
    String &render_pass_name = render_pass->name;

    Shader_Group *shader_group = renderer_get_shader_group(pipeline_state->descriptor.shader_group);
    
    Resource_Header header = make_resource_header((U32)Resource_Type::MATERIAL, resource->uuid);
    header.resource_ref_count += shader_group->shaders.count;

    bool success = true;

    U64 file_offset = 0;
    success &= platform_write_data_to_file(open_file_result, file_offset, &header, sizeof(header));
    file_offset += sizeof(header);
    
    success &= platform_write_data_to_file(open_file_result, file_offset, resource->resource_refs.data, sizeof(U64) * resource->resource_refs.count);
    file_offset += sizeof(U64) * resource->resource_refs.count;

    Material_Resource_Info info =
    {
        .settings = pipeline_state->descriptor.settings,
        .render_pass_name_count = render_pass_name.count,
        .render_pass_name_offset = file_offset + sizeof(Material_Resource_Info),
        .data_size = material->size,
        .data_offset = file_offset + sizeof(Material_Resource_Info) + render_pass_name.count
    };

    success &= platform_write_data_to_file(open_file_result, file_offset, &info, sizeof(info));
    file_offset += sizeof(info);

    success &= platform_write_data_to_file(open_file_result, file_offset, (void *)render_pass_name.data, render_pass_name.count);
    file_offset += sizeof(render_pass_name.count);

    success &= platform_write_data_to_file(open_file_result, file_offset, material->data, material->size);
    file_offset += material->size;

    return success;
} 

static bool load_material_resource(Open_File_Result *open_file_result, Resource *resource)
{
    bool success = true;

    U64 file_offset = sizeof(Resource_Header) + sizeof(U64) * resource->resource_refs.count;

    Material_Resource_Info info;
    success &= platform_read_data_from_file(open_file_result, file_offset, &info, sizeof(info));
    file_offset += sizeof(info);

    char string_buffer[256];
    string_buffer[info.render_pass_name_count] = '\0';
    String render_pass_name = { string_buffer, info.render_pass_name_count };
    success &= platform_read_data_from_file(open_file_result, info.render_pass_name_offset, string_buffer, info.render_pass_name_count);

    Array< Shader_Handle, HE_MAX_SHADER_COUNT_PER_PIPELINE > shaders;

    for (U64 uuid : resource->resource_refs)
    {
        Resource_Ref ref = { uuid };
        Shader_Handle shader_handle = get_resource_handle_as< Shader >(ref);
        append(&shaders, shader_handle);
    }
    
    Shader_Group_Descriptor shader_group_descriptor =
    {
        .shaders = shaders
    };

    Shader_Group_Handle shader_group = renderer_create_shader_group(shader_group_descriptor);

    Render_Context render_context = get_render_context();
    Render_Pass_Handle render_pass = get_render_pass(&render_context.renderer_state->render_graph, render_pass_name.data);

    Pipeline_State_Descriptor pipeline_state_descriptor =
    {
        .settings = info.settings,
        .shader_group = shader_group,
        .render_pass = render_pass,
    };

    Pipeline_State_Handle pipeline_state_handle = renderer_create_pipeline_state(pipeline_state_descriptor);

    Material_Descriptor material_descriptor =
    {
        .pipeline_state_handle = pipeline_state_handle
    };
    
    Material_Handle material_handle = renderer_create_material(material_descriptor);
    Material *material = renderer_get_material(material_handle);
    success &= platform_read_data_from_file(open_file_result, info.data_offset, material->data, material->size);

    if (success)
    {
        resource->index = material_handle.index;
        resource->generation = material_handle.generation;
        resource->ref_count++;
        resource->state = Resource_State::LOADED;
    }

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
        Resource_Conditioner &conditioner = resource_system_state->resource_type_infos[i].conditioner;
        for (U32 j = 0; j < conditioner.extension_count; j++)
        {
            if (conditioner.extensions[j] == extension)
            {
                return &resource_system_state->resource_type_infos[i];
            }
        }
    }
    return nullptr;
}

//==================================== Jobs ==================================================

struct Condition_Resource_Job_Data
{
    U32 resource_type;
    String asset_absloute_path;
    String resource_absloute_path;
};

static Job_Result condition_resource_job(const Job_Parameters &params)
{
    Condition_Resource_Job_Data *job_data = (Condition_Resource_Job_Data *)params.data;
    Resource_Conditioner &conditioner = resource_system_state->resource_type_infos[job_data->resource_type].conditioner;
    if (!conditioner.condition(job_data->asset_absloute_path, job_data->resource_absloute_path, params.temprary_memory_arena))
    {
        HE_LOG(Resource, Trace, "failed to condition asset: %.*s\n", HE_EXPAND_STRING(job_data->asset_absloute_path));
        return Job_Result::FAILED;
    }
    HE_LOG(Resource, Trace, "successfully conditioned asset: %.*s\n", HE_EXPAND_STRING(job_data->asset_absloute_path));
    return Job_Result::SUCCEEDED;
}

struct Save_Resource_Job_Data
{
    Resource *resource;
};

static Job_Result save_resource_job(const Job_Parameters &params)
{
    Save_Resource_Job_Data *job_data = (Save_Resource_Job_Data *)params.data;
    Resource *resource = job_data->resource;
    Resource_Conditioner &conditioner = resource_system_state->resource_type_infos[resource->type].conditioner;
    
    Open_File_Result open_file_result = platform_open_file(resource->absloute_path.data, Open_File_Flags(OpenFileFlag_Write|OpenFileFlag_Truncate));
    if (!open_file_result.success)
    {
        HE_LOG(Resource, Trace, "failed to open file %.*s\n", HE_EXPAND_STRING(resource->absloute_path));
        return Job_Result::FAILED;
    }

    HE_DEFER { platform_close_file(&open_file_result); };
    if (conditioner.save(resource, &open_file_result, params.temprary_memory_arena))
    {
        HE_LOG(Resource, Trace, "failed to save resource: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
        return Job_Result::FAILED;
    }

    HE_LOG(Resource, Trace, "successfully saved resource: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
    return Job_Result::SUCCEEDED;
}

struct Load_Resource_Job_Data
{
    Resource *resource;
};

static Job_Result load_resource_job(const Job_Parameters &params)
{
    Load_Resource_Job_Data *job_data = (Load_Resource_Job_Data *)params.data;
    Resource *resource = job_data->resource;

    platform_lock_mutex(&resource->mutex);
    HE_DEFER {  platform_unlock_mutex(&resource->mutex); };
    
    Resource_Type_Info &info = resource_system_state->resource_type_infos[resource->type];
    bool use_allocation_group = info.loader.use_allocation_group;

    if (use_allocation_group)
    {
        Renderer_Semaphore_Descriptor semaphore_descriptor =
        {
            .initial_value = 0
        };

        resource->allocation_group.resource_name = resource->relative_path;
        resource->allocation_group.type = Allocation_Group_Type::GENERAL;
        resource->allocation_group.semaphore = renderer_create_semaphore(semaphore_descriptor);
        resource->allocation_group.resource_index = (S32)index_of(&resource_system_state->resources, resource);
    }
    Open_File_Result open_file_result = platform_open_file(resource->absloute_path.data, OpenFileFlag_Read);
    
    if (!open_file_result.success)
    {
        HE_LOG(Resource, Fetal, "failed to open resource file: %.*s", HE_EXPAND_STRING(resource->relative_path));
        return Job_Result::FAILED;
    }

    HE_DEFER { platform_close_file(&open_file_result); };

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
    else
    {
        HE_LOG(Resource, Trace, "resource loaded: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
    }
    
    return Job_Result::SUCCEEDED; 
}

// ======================================================================================

static void on_asset(const char *data, U64 count)
{
    String asset_absolute_path = { data, count };
    String extension = get_extension(asset_absolute_path);
    
    Resource_Type_Info *resource_type_info = find_resource_type_from_extension(extension);
    if (!resource_type_info)
    {
        return;
    }

    String name = get_name(asset_absolute_path);
    String asset_parent_path = get_parent_path(asset_absolute_path);
    String absloute_resource_path = format_string(resource_system_state->arena,
                                                  "%.*s/%.*s.%s",
                                                  HE_EXPAND_STRING(asset_parent_path),
                                                  HE_EXPAND_STRING(name),
                                                  resource_extension); // @Leak
    
    if (!file_exists(absloute_resource_path))
    {
        Condition_Resource_Job_Data condition_resource_job_data = 
        {
            .resource_type = (U32)(resource_type_info - resource_system_state->resource_type_infos),
            .asset_absloute_path = copy_string(asset_absolute_path, resource_system_state->arena), // @Leak
            .resource_absloute_path = copy_string(absloute_resource_path, resource_system_state->arena) // @Leak
        };

        Job job = {};
        job.parameters.data = &condition_resource_job_data;
        job.parameters.size = sizeof(condition_resource_job_data);
        job.proc = condition_resource_job;
        execute_job(job);
    }
    else
    {
        // todo(amer): condition assets that's doesn't have the last version.
    }
}

static void on_resource(const char *data, U64 count)
{
    String absolute_path = { data, count };
    String extension = get_extension(absolute_path);
    if (extension != "hres")
    {
        return;
    }

    String resource_absolute_path = copy_string(absolute_path, resource_system_state->arena);
    String resource_relative_path = sub_string(resource_absolute_path, resource_system_state->resource_path.count + 1);
    
    Open_File_Result open_file_result = platform_open_file(resource_absolute_path.data, OpenFileFlag_Read);
    
    if (!open_file_result.success)
    {
        HE_LOG(Resource, Fetal, "failed to open file: %.*s\n", HE_EXPAND_STRING(resource_relative_path));
        return;
    }

    if (open_file_result.size < sizeof(Resource_Header))
    {
        return;
    }
    
    Resource_Header header = {};
    platform_read_data_from_file(&open_file_result, 0, &header, sizeof(header));
    
    if (strncmp(header.magic_value, "HOPE", 4) != 0)
    {
        return;
    }

    if (header.type > (U32)Resource_Type::COUNT)
    {
        return;
    }

    Resource_Type_Info &info = resource_system_state->resource_type_infos[header.type]; 
    if (header.version > info.version)
    {
        return;
    }
    
    Resource &resource = append(&resource_system_state->resources);
    U32 resource_index = index_of(&resource_system_state->resources, resource);
    
    resource.absloute_path = resource_absolute_path;
    resource.relative_path = resource_relative_path;
    resource.uuid = header.uuid;
    resource.state = Resource_State::UNLOADED;
    resource.type = header.type;
    resource.index = -1;
    resource.generation = 0;
    platform_create_mutex(&resource.mutex);
    
    if (header.resource_ref_count)
    {
        init(&resource.resource_refs, resource_system_state->free_list_allocator, header.resource_ref_count);
        bool read = platform_read_data_from_file(&open_file_result, sizeof(Resource_Header), resource.resource_refs.data, sizeof(U64) * header.resource_ref_count);
        if (!read)
        {
            HE_LOG(Resource, Fetal, "failed to read resource refs at: %.*s\n", HE_EXPAND_STRING(resource_relative_path));
            return;
        }
    }
    else
    {
        init(&resource.resource_refs, resource_system_state->free_list_allocator);
    }

    platform_close_file(&open_file_result);

    uuid_to_resource_index.emplace(resource.uuid, resource_index);
    path_to_resource_index.emplace(resource_relative_path, resource_index);
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
    
    {
        static String texture_extensions[] =
        {
            HE_STRING_LITERAL("jpeg"),
            HE_STRING_LITERAL("png"),
            HE_STRING_LITERAL("tga"),
            HE_STRING_LITERAL("psd")
        };

        Resource_Conditioner texture_conditioner =
        {
            .extension_count = HE_ARRAYCOUNT(texture_extensions),
            .extensions = texture_extensions,
            .condition = &condition_texture_to_resource
        };

        Resource_Loader texture_loader =
        {
            .use_allocation_group = true,
            .load = &load_texture_resource,
            .unload = &unload_texture_resource
        };

        register_resource(Resource_Type::TEXTURE, "texture", 1, texture_conditioner, texture_loader);
    }

    {
        static String shader_extensions[] =
        {
            HE_STRING_LITERAL("vert"),
            HE_STRING_LITERAL("frag"),
        };

        Resource_Conditioner shader_conditioner =
        {
            .extension_count = HE_ARRAYCOUNT(shader_extensions),
            .extensions = shader_extensions,
            .condition = &condition_shader_to_resource,
        };

        Resource_Loader shader_loader =
        {
            .use_allocation_group = false,
            .load = &load_shader_resource,
            .unload = &unload_shader_resource,
        };

        register_resource(Resource_Type::SHADER, "shader", 1, shader_conditioner, shader_loader);
    }

    {
        static String material_extensions[] =
        {
            HE_STRING_LITERAL("mat"),
        };

        Resource_Conditioner material_conditioner =
        {
            .extension_count = HE_ARRAYCOUNT(material_extensions),
            .extensions = material_extensions,
            .condition = &condition_material_to_resource,
            .save = &save_material_resource
        };

        Resource_Loader material_loader =
        {
            .use_allocation_group = false,
            .load = &load_material_resource,
            .unload = &unload_material_resource,
        };

        register_resource(Resource_Type::MATERIAL, "material", 1, material_conditioner, material_loader);
    }

    bool recursive = true;
    platform_walk_directory(resource_path.data, recursive, &on_asset);

    wait_for_all_jobs_to_finish();

    platform_walk_directory(resource_path.data, recursive, &on_resource);

    Resource_Ref cube_base_color = aquire_resource(HE_STRING_LITERAL("cube_base_color.hres"));
    // Resource_Ref opaque_pbr_vert = aquire_resource(HE_STRING_LITERAL("opaque_pbr_vert.hres"));
    // Resource_Ref opaque_pbr_frag = aquire_resource(HE_STRING_LITERAL("opaque_pbr_frag.hres"));

    wait_for_all_jobs_to_finish();

    // create_material_resource
    // Resource_Ref shaders[] =
    // {
    //     find_resource(HE_STRING_LITERAL("opaque_pbr_vert.hres")),
    //     find_resource(HE_STRING_LITERAL("opaque_pbr_frag.hres"))
    // };

    // Pipeline_State_Settings settings =
    // {
    //     .cull_mode = Cull_Mode::BACK,
    //     .front_face = Front_Face::COUNTER_CLOCKWISE,
    //     .fill_mode = Fill_Mode::SOLID,
    //     .sample_shading = true,
    // };

    // Resource_Ref opaque_pbr = create_material_resource(HE_STRING_LITERAL("opaque_pbr_mat.hres"), HE_STRING_LITERAL("opaque"), to_array_view(shaders), settings);

    Resource_Ref opaque_pbr_mat = aquire_resource(HE_STRING_LITERAL("opaque_pbr_mat.hres"));
    return true;
}

void deinit_resource_system()
{
}

bool register_resource(Resource_Type type, const char *name, U32 version, Resource_Conditioner conditioner, Resource_Loader loader)
{
    HE_ASSERT(name);
    HE_ASSERT(version);
    Resource_Type_Info &resource_type_info = resource_system_state->resource_type_infos[(U32)type];
    resource_type_info.name = HE_STRING(name);
    resource_type_info.version = version;
    resource_type_info.conditioner = conditioner;
    resource_type_info.loader = loader;
    return true;
}

bool is_valid(Resource_Ref ref)
{
    return ref.uuid != HE_MAX_U64 && uuid_to_resource_index.find(ref.uuid) != uuid_to_resource_index.end();
}

Resource_Ref find_resource(const String &relative_path)
{
    auto it = path_to_resource_index.find(relative_path);
    U64 uuid = HE_MAX_U64;

    if (it != path_to_resource_index.end())
    {
        uuid = resource_system_state->resources[it->second].uuid;
    }

    Resource_Ref ref = { uuid };
    return ref;
}

static void aquire_resource(Resource *resource)
{
    if (resource->resource_refs.count)
    {
        for (U64 uuid : resource->resource_refs)
        {
            Resource_Ref ref = { uuid };
            aquire_resource(ref);
        }

        // todo(amer): make this efficient with semaphores 
        while (true)
        {
            bool all_loaded = true;

            for (U64 uuid : resource->resource_refs)
            {
                Resource_Ref ref = { uuid };
                Resource *ref_resource = get_resource(ref);
                if (ref_resource->state != Resource_State::LOADED)
                {
                    all_loaded = false;
                    break;
                }
            }

            if (all_loaded)
            {
                break;
            }
        }
    }

    platform_lock_mutex(&resource->mutex);

    if (resource->state == Resource_State::UNLOADED)
    {
        resource->state = Resource_State::PENDING;
        platform_unlock_mutex(&resource->mutex);

        Load_Resource_Job_Data job_data
        {
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
    auto it = path_to_resource_index.find(path);
    if (it == path_to_resource_index.end())
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

Resource *get_resource(U32 index)
{
    HE_ASSERT(index >= 0 && index < resource_system_state->resources.count);
    return &resource_system_state->resources[index];
}

template<>
Texture *get_resource_as<Texture>(Resource_Ref ref)
{
    Resource *resource = get_resource(ref);
    HE_ASSERT(resource->state == Resource_State::LOADED);
    return renderer_get_texture({ resource->index, resource->generation });
}

template<>
Shader *get_resource_as<Shader>(Resource_Ref ref)
{
    Resource *resource = get_resource(ref);
    HE_ASSERT(resource->state == Resource_State::LOADED);
    return renderer_get_shader({ resource->index, resource->generation });
}

Resource_Ref create_material_resource(const String &relative_path, const String &render_pass_name, Array_View< Resource_Ref > shader_refs, const Pipeline_State_Settings &settings)
{
    Array< Shader_Handle, HE_MAX_SHADER_COUNT_PER_PIPELINE > shaders;

    for (Resource_Ref ref : shader_refs)
    {
        aquire_resource(ref);
        Shader_Handle shader_handle = get_resource_handle_as< Shader >(ref);
        append(&shaders, shader_handle);
    }

    wait_for_all_jobs_to_finish(); // todo(amer): wait for all jobs won't work for use allocation group resources like textures

    // todo(amer): wait for all refs.
    Shader_Group_Descriptor shader_group_descriptor =
    {
        .shaders = shaders
    };
    Shader_Group_Handle shader_group = renderer_create_shader_group(shader_group_descriptor);

    Render_Context render_context = get_render_context();
    Render_Pass_Handle render_pass = get_render_pass(&render_context.renderer_state->render_graph, render_pass_name.data);

    Pipeline_State_Descriptor pipeline_state_descriptor =
    {
        .settings = settings,
        .shader_group = shader_group,
        .render_pass = render_pass,
    };

    Pipeline_State_Handle pipeline_state_handle = renderer_create_pipeline_state(pipeline_state_descriptor);

    Material_Descriptor material_descriptor =
    {
        .pipeline_state_handle = pipeline_state_handle
    };
    Material_Handle material_handle = renderer_create_material(material_descriptor);

    Resource &resource = append(&resource_system_state->resources);
    resource.uuid = generate_uuid();
    resource.type = (U32)Resource_Type::MATERIAL;
    resource.absloute_path = format_string(resource_system_state->arena,
                                           "%.*s/%.*s",
                                           HE_EXPAND_STRING(resource_system_state->resource_path),
                                           HE_EXPAND_STRING(relative_path)); // todo(amer): absolute path fill this
    resource.relative_path = copy_string(relative_path, resource_system_state->arena);
    resource.index = material_handle.index;
    resource.generation = material_handle.generation;
    resource.ref_count = 1;
    resource.state = Resource_State::LOADED;

    init(&resource.resource_refs, resource_system_state->free_list_allocator);

    for (Resource_Ref ref : shader_refs)
    {
        append(&resource.resource_refs, ref.uuid);
    }

    platform_create_mutex(&resource.mutex);

    uuid_to_resource_index.emplace(resource.uuid, index_of(&resource_system_state->resources, resource));
    path_to_resource_index.emplace(relative_path, index_of(&resource_system_state->resources, resource));

    Save_Resource_Job_Data save_resource_job_data = 
    {
        .resource = &resource,
    };

    Job job = {};
    job.parameters.data = &save_resource_job_data;
    job.parameters.size = sizeof(save_resource_job_data);
    job.proc = save_resource_job;
    execute_job(job);

    Resource_Ref ref = { resource.uuid };
    return ref;
}

// =============================== Editor ==========================================

static String get_resource_state_string(Resource_State resource_state)
{
    switch (resource_state)
    {
        case Resource_State::UNLOADED:
            return HE_STRING_LITERAL("Unloaded");

        case Resource_State::PENDING:
            return HE_STRING_LITERAL("Pending");

        case Resource_State::LOADED:
            return HE_STRING_LITERAL("Loaded");

        default:
            HE_ASSERT("unsupported resource state");
            break;
    }

    return HE_STRING_LITERAL("");
}

void imgui_draw_resource_system()
{
    ImGui::Begin("Resources");

    const char* coloum_names[] =
    {
        "No.",
        "UUID",
        "Type",
        "Resource",
        "State",
        "Ref Count",
        "Refs"
    };

    ImGuiTableFlags flags = ImGuiTableFlags_Borders|ImGuiTableFlags_Resizable;

    if (ImGui::BeginTable("Table", HE_ARRAYCOUNT(coloum_names), flags))
    {
        for (U32 col = 0; col < HE_ARRAYCOUNT(coloum_names); col++)
        {
            ImGui::TableSetupColumn(coloum_names[col], ImGuiTableColumnFlags_WidthStretch);
        }

        ImGui::TableHeadersRow();

        for (U32 row = 0; row < resource_system_state->resources.count; row++)
        {
            Resource &resource = resource_system_state->resources[row];
            Resource_Type_Info &info = resource_system_state->resource_type_infos[resource.type];

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("%d", row + 1);

            ImGui::TableNextColumn();
            ImGui::Text("%llu", resource.uuid);

            ImGui::TableNextColumn();
            ImGui::Text("%.*s", HE_EXPAND_STRING(info.name));

            ImGui::TableNextColumn();
            ImGui::Text("%.*s", HE_EXPAND_STRING(resource.relative_path));

            ImGui::TableNextColumn();
            ImGui::Text("%.*s", HE_EXPAND_STRING(get_resource_state_string(resource.state)));

            ImGui::TableNextColumn();
            ImGui::Text("%u", resource.ref_count);

            ImGui::TableNextColumn();
            if (resource.resource_refs.count)
            {
                for (U64 ref : resource.resource_refs)
                {
                    ImGui::Text("%llu ", ref);
                }
            }
            else
            {
                ImGui::Text("None");
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}