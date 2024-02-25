#include "resource_system.h"

#include "core/engine.h"
#include "core/file_system.h"
#include "core/logging.h"
#include "core/job_system.h"
#include "core/platform.h"

#include "containers/hash_map.h"

#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"

#include <unordered_map> // todo(amer): to be removed
#include <mutex>

#include <cgltf.h>
#include <stb/stb_image.h>
#include <imgui.h>

#include <shaderc/shaderc.h>

struct Resource_Type_Info
{
    String name;
    U32 version;
    Asset_Conditioner conditioner;
    Resource_Loader loader;
};

struct Resource_System_State
{
    Free_List_Allocator *resource_allocator;

    String asset_database_path;
    String resource_path;
    Resource_Type_Info resource_type_infos[(U32)Asset_Type::COUNT]; // todo(amer): make this a dynamic array...

    Dynamic_Array< Asset > assets;
    Dynamic_Array< Resource > resources;

    Dynamic_Array< U32 > assets_to_condition;
};

static std::mutex resource_mutex;
static std::mutex asset_mutex;
static std::unordered_map< U64, U32 > uuid_to_asset_index;
static std::unordered_map< U64, U32 > uuid_to_resource_index;
static constexpr const char *resource_extension = "hres";

template<>
struct std::hash<String>
{
    std::size_t operator()(const String &str) const
    {
        return hash_key(str);
    }
};

static std::unordered_map< String, U32 > path_to_resource_index;
static std::unordered_map< String, U32 > path_to_asset_index;

static Resource_System_State *resource_system_state;

// helpers
Resource_Header make_resource_header(Asset_Type type, U64 asset_uuid, U64 uuid)
{
    Resource_Header result;
    result.magic_value[0] = 'H';
    result.magic_value[1] = 'O';
    result.magic_value[2] = 'P';
    result.magic_value[3] = 'E';
    result.type = (U32)type;
    result.version = resource_system_state->resource_type_infos[(U32)type].version;
    result.uuid = uuid;
    result.asset_uuid = asset_uuid;
    result.resource_ref_count = 0;
    result.child_count = 0;
    return result;
}

#include <random> // todo(amer): to be removed

static U64 generate_uuid()
{
    static std::random_device device;
    static std::mt19937 engine(device());
    static std::uniform_int_distribution<U64> dist(0, HE_MAX_U64);
    U64 uuid = dist(engine);
    HE_ASSERT(uuid_to_resource_index.find(uuid) == uuid_to_resource_index.end());
    HE_ASSERT(uuid_to_asset_index.find(uuid) == uuid_to_asset_index.end());
    return uuid;
}

static String make_resource_absloute_path(String asset_absolute_path, Memory_Arena *arena)
{
    String parent = get_parent_path(asset_absolute_path);
    String name = get_name(asset_absolute_path);
    return format_string(arena, "%.*s/%.*s.hres", HE_EXPAND_STRING(parent), HE_EXPAND_STRING(name));
}

static String absolute_path_to_relative(String absolute_path)
{
    return sub_string(absolute_path, resource_system_state->resource_path.count + 1);
}

static U32 create_asset(String absolute_path, U64 uuid, Asset_Type type)
{
    Asset &asset = append(&resource_system_state->assets);
    U32 asset_index = index_of(&resource_system_state->assets, asset);
    asset.type = type;
    asset.job_handle = Resource_Pool<Job>::invalid_handle;

    asset.uuid = uuid;
    asset.last_write_time = platform_get_file_last_write_time(absolute_path.data);
    asset.state = Asset_State::UNCONDITIONED;

    init(&asset.resource_refs);

    asset.absolute_path = absolute_path;
    asset.relative_path = absolute_path_to_relative(absolute_path);

    HE_ASSERT(path_to_asset_index.find(asset.relative_path) == path_to_asset_index.end());
    HE_ASSERT(uuid_to_asset_index.find(uuid) == uuid_to_asset_index.end());

    path_to_asset_index.emplace(asset.relative_path, asset_index);
    uuid_to_asset_index.emplace(asset.uuid, asset_index);

    platform_create_mutex(&asset.mutex);

    return asset_index;
}

static U32 create_resource(String absolute_path, Asset_Type type, U64 asset_uuid, U64 uuid = HE_MAX_U64)
{
    if (uuid == HE_MAX_U64)
    {
        uuid = generate_uuid();
    }

    resource_mutex.lock();

    Resource &resource = append(&resource_system_state->resources);
    U32 resource_index = index_of(&resource_system_state->resources, resource);

    resource.absolute_path = absolute_path;
    resource.relative_path = absolute_path_to_relative(absolute_path);
    resource.uuid = uuid;

    HE_ASSERT(uuid_to_resource_index.find(resource.uuid) == uuid_to_resource_index.end());
    HE_ASSERT(path_to_resource_index.find(resource.relative_path) == path_to_resource_index.end());
    uuid_to_resource_index.emplace(resource.uuid, resource_index);
    path_to_resource_index.emplace(resource.relative_path, resource_index);

    resource_mutex.unlock();

    resource.asset_uuid = asset_uuid;
    resource.state = Resource_State::UNLOADED;
    resource.type = type;
    resource.index = -1;
    resource.generation = 0;
    resource.ref_count = 0;
    resource.job_handle = Resource_Pool<Job>::invalid_handle;

    platform_create_mutex(&resource.mutex);

    init(&resource.resource_refs);
    init(&resource.children);

    return resource_index;
}

// ========================== Resources ====================================

static bool condition_texture_to_resource(Read_Entire_File_Result *asset_file_result, Asset *asset, Resource *resource, Memory_Arena *arena)
{
    S32 width;
    S32 height;
    S32 channels;

    stbi_uc *pixels = stbi_load_from_memory(asset_file_result->data, u64_to_u32(asset_file_result->size), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
        return false;
    }

    HE_DEFER { stbi_image_free(pixels); };

    U64 offset = 0;
    U8 *buffer = &arena->base[arena->offset];

    Resource_Header header = make_resource_header(Asset_Type::TEXTURE, resource->asset_uuid, resource->uuid);
    copy_memory(&buffer[offset], &header, sizeof(header));
    offset += sizeof(header);

    Texture_Resource_Info texture_resource_info =
    {
        .width = (U32)width,
        .height = (U32)height,
        .format = Texture_Format::R8G8B8A8_SRGB,
        .mipmapping = true,
        .data_offset = sizeof(Resource_Header) + sizeof(Texture_Resource_Info)
    };

    copy_memory(&buffer[offset], &texture_resource_info, sizeof(Texture_Resource_Info));
    offset += sizeof(Texture_Resource_Info);

    copy_memory(&buffer[offset], pixels, width * height * sizeof(U32)); // todo(amer): @Hardcoding
    offset += width * height * sizeof(U32);

    bool result = write_entire_file(resource->absolute_path.data, buffer, offset);
    return result;
}

static bool load_texture_resource(Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena)
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
    bool success = platform_read_data_from_file(open_file_result, info.data_offset, data, data_size);

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
    return success;
}

static void unload_texture_resource(Resource *resource)
{
    HE_ASSERT(resource->state != Resource_State::UNLOADED);

    Render_Context render_context = get_render_context();

    Texture_Handle texture_handle = { resource->index, resource->generation };

    if (is_valid_handle(&render_context.renderer_state->textures, texture_handle) &&
        (texture_handle != render_context.renderer_state->white_pixel_texture || texture_handle != render_context.renderer_state->normal_pixel_texture))
    {
        renderer_destroy_texture(texture_handle);
        resource->index = render_context.renderer_state->white_pixel_texture.index;
        resource->generation = render_context.renderer_state->white_pixel_texture.generation;
    }
}

static bool condition_shader_to_resource(Read_Entire_File_Result *asset_file_result, Asset *asset, Resource *resource, Memory_Arena *arena)
{
    String ext = get_extension(asset->relative_path);
    shaderc_shader_kind shader_kind;

    if (ext == "vert")
    {
        shader_kind = shaderc_vertex_shader;
    }
    else if (ext == "frag")
    {
        shader_kind = shaderc_fragment_shader;
    }
    else
    {
        HE_ASSERT(!"we only support vert and frag shaders");
    }

    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_zero);
    shaderc_compile_options_set_auto_map_locations(options, true);
    // shaderc_compile_options_set_target_spirv(options, shaderc_spirv_version_1_0);

    HE_DEFER
    {
        shaderc_compiler_release(compiler);
        shaderc_compile_options_release(options);
    };

    shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, (const char *)asset_file_result->data, asset_file_result->size, shader_kind, (const char *)asset->relative_path.data, "main", options);

    HE_DEFER
    {
        shaderc_result_release(result);
    };

    shaderc_compilation_status status = shaderc_result_get_compilation_status(result);

    if (status != shaderc_compilation_status_success)
    {
        HE_LOG(Resource, Fetal, "%s\n", shaderc_result_get_error_message(result));
        return false;
    }

    U64 size = shaderc_result_get_length(result);
    const char *data = shaderc_result_get_bytes(result);

    bool success = true;
    U64 offset = 0;

    // todo(amer): buffer abstraction...
    U8 *buffer = arena->base + arena->offset;

    Resource_Header header = make_resource_header(Asset_Type::SHADER, resource->asset_uuid, resource->uuid);
    header.resource_ref_count = resource->resource_refs.count;
    header.child_count = resource->children.count;

    copy_memory(&buffer[offset], &header, sizeof(header));
    offset += sizeof(header);

    if (resource->resource_refs.count)
    {
        copy_memory(&buffer[offset], resource->resource_refs.data, sizeof(U64) * resource->resource_refs.count);
        offset += sizeof(U64) * resource->resource_refs.count;
    }

    if (resource->children.count)
    {
        copy_memory(&buffer[offset], resource->children.data, sizeof(U64) * resource->children.count);
        offset += sizeof(U64) * resource->children.count;
    }

    Shader_Resource_Info info =
    {
        .data_offset = sizeof(Resource_Header) + sizeof(Shader_Resource_Info) + sizeof(U64) * resource->resource_refs.count + sizeof(U64) * resource->children.count,
        .data_size = size
    };

    copy_memory(&buffer[offset], &info, sizeof(info));
    offset += sizeof(info);


    copy_memory(&buffer[offset], data, size);
    offset += size;

    success &= write_entire_file(resource->absolute_path.data, buffer, offset);
    return success;
}

static bool load_shader_resource(Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena)
{
    bool success = true;

    U64 offset = 0;

    if (resource->resource_refs.count)
    {
        offset += sizeof(U64) * resource->resource_refs.count;
    }

    if (resource->children.count)
    {
        offset += sizeof(U64) * resource->children.count;
    }

    Shader_Resource_Info info;
    success &= platform_read_data_from_file(open_file_result, sizeof(Resource_Header) + offset, &info, sizeof(info));

    U8 *data = HE_ALLOCATE_ARRAY(arena, U8, info.data_size);
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
    return true;
}

static void unload_shader_resource(Resource *resource)
{
    Render_Context render_context = get_render_context();
    HE_ASSERT(resource->state != Resource_State::UNLOADED);

    Shader_Handle shader_handle = { resource->index, resource->generation };
    if (is_valid_handle(&render_context.renderer_state->shaders, shader_handle) && (shader_handle != render_context.renderer_state->default_vertex_shader || shader_handle != render_context.renderer_state->default_fragment_shader))
    {
        Shader *shader = renderer_get_shader(shader_handle);

        switch (shader->stage)
        {
            case Shader_Stage::VERTEX:
            {
                resource->index = render_context.renderer_state->default_vertex_shader.index;
                resource->generation = render_context.renderer_state->default_vertex_shader.generation;
            } break;

            case Shader_Stage::FRAGMENT:
            {
                resource->index = render_context.renderer_state->default_fragment_shader.index;
                resource->generation = render_context.renderer_state->default_fragment_shader.generation;
            } break;

            default:
            {
                HE_ASSERT(false);
            } break;
        }

        renderer_destroy_shader(shader_handle);
    }
}

static bool condition_material_to_resource(Read_Entire_File_Result *asset_file_result, Asset *asset, Resource *resource, Memory_Arena *arena)
{
    return true;
}

static bool load_material_resource(Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena)
{
    bool success = true;

    U64 file_offset = sizeof(Resource_Header) + sizeof(U64) * resource->resource_refs.count;

    Material_Resource_Info info;
    success &= platform_read_data_from_file(open_file_result, file_offset, &info, sizeof(info));
    file_offset += sizeof(info);

    char *string_buffer = HE_ALLOCATE_ARRAY(arena, char, 1024);
    HE_ASSERT(info.render_pass_name_count <= 1024);
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
        .pipeline_state_handle = pipeline_state_handle,
    };

    Material_Handle material_handle = renderer_create_material(material_descriptor);
    resource->index = material_handle.index;
    resource->generation = material_handle.generation;

    if (info.property_count)
    {
        Material_Property_Info *property_infos = HE_ALLOCATE_ARRAY(arena, Material_Property_Info, info.property_count);
        success &= platform_read_data_from_file(open_file_result, info.property_data_offset, (void *)property_infos, sizeof(Material_Property_Info) * info.property_count);

        U64 offset = info.property_data_offset + sizeof(Material_Property_Info) * info.property_count;

        for (U32 property_index = 0; property_index < info.property_count; property_index++)
        {
            U64 property_name_count = 0;

            success &= platform_read_data_from_file(open_file_result, offset, &property_name_count, sizeof(U64));
            offset += sizeof(U64);

            HE_ASSERT(property_name_count);

            char *property_name = HE_ALLOCATE_ARRAY(arena, char, property_name_count + 1);
            property_name[property_name_count] = '\0';

            success &= platform_read_data_from_file(open_file_result, offset, property_name, sizeof(char) * property_name_count);
            offset += sizeof(char) * property_name_count;

            set_property(material_handle, property_name, property_infos[property_index].data);
        }
    }

    return success;
}

static void unload_material_resource(Resource *resource)
{
    HE_ASSERT(resource->state != Resource_State::UNLOADED)
    Material_Handle material_handle = { resource->index, resource->generation };

    Render_Context render_context = get_render_context();
    if (is_valid_handle(&render_context.renderer_state->materials, material_handle) && material_handle != render_context.renderer_state->default_material)
    {
        renderer_destroy_material(material_handle);
        resource->index = render_context.renderer_state->default_material.index;
        resource->generation = render_context.renderer_state->default_material.generation;
    }
}

static void* _cgltf_alloc(void* user, cgltf_size size)
{
    return allocate(get_general_purpose_allocator(), size, 8);
}

static void _cgltf_free(void* user, void *ptr)
{
    deallocate(get_general_purpose_allocator(), ptr);
}

static void write_attribute_for_all_sub_meshes(U8 *buffer, U64 *offset, cgltf_mesh *mesh, cgltf_attribute_type attribute_type)
{
    for (U32 sub_mesh_index = 0; sub_mesh_index < (U32)mesh->primitives_count; sub_mesh_index++)
    {
        cgltf_primitive *primitive = &mesh->primitives[sub_mesh_index];

        for (U32 attribute_index = 0; attribute_index < primitive->attributes_count; attribute_index++)
        {
            cgltf_attribute *attribute = &primitive->attributes[attribute_index];
            const auto *accessor = attribute->data;
            const auto *view = accessor->buffer_view;
            U8 *data_ptr = (U8 *)view->buffer->data;

            if (attribute->type == attribute_type)
            {
                U64 element_size = attribute->data->stride;
                U64 element_count = attribute->data->count;
                U8 *data = data_ptr + view->offset + accessor->offset;

                copy_memory(&buffer[*offset], data, element_size * element_count);
                *offset = *offset + element_size * element_count;
            }
        }
    }
}

static bool save_static_mesh_resource(Resource *resource, cgltf_mesh *mesh, cgltf_data *data, U64 *material_uuids)
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    bool success = true;

    U64 offset = 0;
    U8 *buffer = &scratch_memory.arena->base[scratch_memory.arena->offset];

    Resource_Header header = make_resource_header(Asset_Type::STATIC_MESH, resource->asset_uuid, resource->uuid);
    copy_memory(&buffer[offset], &header, sizeof(header));
    offset += sizeof(header);

    Static_Mesh_Resource_Info info =
    {
        .sub_mesh_count = (U16)mesh->primitives_count,
        .sub_mesh_data_offset = offset + sizeof(Static_Mesh_Resource_Info),
        .data_offset = offset + sizeof(Static_Mesh_Resource_Info) + sizeof(Sub_Mesh_Info) * mesh->primitives_count
    };

    copy_memory(&buffer[offset], &info, sizeof(info));
    offset += sizeof(info);

    for (U32 sub_mesh_index = 0; sub_mesh_index < (U32)mesh->primitives_count; sub_mesh_index++)
    {
        cgltf_primitive *primitive = &mesh->primitives[sub_mesh_index];
        HE_ASSERT(primitive->type == cgltf_primitive_type_triangles);

        U64 index_count = primitive->indices->count;
        HE_ASSERT(index_count);

        U64 position_count = 0;
        U64 uv_count = 0;
        U64 normal_count = 0;
        U64 tangent_count = 0;

        for (U32 attribute_index = 0; attribute_index < primitive->attributes_count; attribute_index++)
        {
            cgltf_attribute *attribute = &primitive->attributes[attribute_index];
            HE_ASSERT(attribute->type != cgltf_attribute_type_invalid);

            HE_ASSERT(primitive->indices->type == cgltf_type_scalar);
            HE_ASSERT(primitive->indices->component_type == cgltf_component_type_r_16u);
            HE_ASSERT(primitive->indices->stride == sizeof(U16));

            switch (attribute->type)
            {
                case cgltf_attribute_type_position:
                {
                    HE_ASSERT(attribute->data->type == cgltf_type_vec3);
                    HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                    U64 stride = attribute->data->stride;
                    HE_ASSERT(stride == sizeof(glm::vec3));

                    position_count = attribute->data->count;
                } break;

                case cgltf_attribute_type_texcoord:
                {
                    HE_ASSERT(attribute->data->type == cgltf_type_vec2);
                    HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                    U64 stride = attribute->data->stride;
                    HE_ASSERT(stride == sizeof(glm::vec2));

                    uv_count = attribute->data->count;
                } break;

                case cgltf_attribute_type_normal:
                {
                    HE_ASSERT(attribute->data->type == cgltf_type_vec3);
                    HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                    U64 stride = attribute->data->stride;
                    HE_ASSERT(stride == sizeof(glm::vec3));

                    normal_count = attribute->data->count;
                } break;

                case cgltf_attribute_type_tangent:
                {
                    HE_ASSERT(attribute->data->type == cgltf_type_vec4);
                    HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                    U64 stride = attribute->data->stride;
                    HE_ASSERT(stride == sizeof(glm::vec4));

                    tangent_count = attribute->data->count;
                } break;
            }
        }

        HE_ASSERT(position_count == uv_count);
        HE_ASSERT(position_count == normal_count);
        HE_ASSERT(position_count == tangent_count);

        HE_ASSERT(primitive->material - data->materials >= 0);
        U32 material_index = (U32)(primitive->material - data->materials);

        Sub_Mesh_Info sub_mesh_info =
        {
            .vertex_count = (U16)position_count,
            .index_count = (U32)index_count,
            .material_uuid = material_uuids[material_index]
        };

        copy_memory(&buffer[offset], &sub_mesh_info, sizeof(sub_mesh_info));
        offset += sizeof(sub_mesh_info);
    }

    for (U32 sub_mesh_index = 0; sub_mesh_index < (U32)mesh->primitives_count; sub_mesh_index++)
    {
        cgltf_primitive *primitive = &mesh->primitives[sub_mesh_index];
        HE_ASSERT(primitive->type == cgltf_primitive_type_triangles);

        const auto *accessor = primitive->indices;
        const auto *view = accessor->buffer_view;
        U8 *data = (U8 *)view->buffer->data + view->offset + accessor->offset;

        copy_memory(&buffer[offset], data, sizeof(U16) * primitive->indices->count);
        offset += sizeof(U16) * primitive->indices->count;
    }

    write_attribute_for_all_sub_meshes(buffer, &offset, mesh, cgltf_attribute_type_position);
    write_attribute_for_all_sub_meshes(buffer, &offset, mesh, cgltf_attribute_type_texcoord);
    write_attribute_for_all_sub_meshes(buffer, &offset, mesh, cgltf_attribute_type_normal);
    write_attribute_for_all_sub_meshes(buffer, &offset, mesh, cgltf_attribute_type_tangent);

    bool result = write_entire_file(resource->absolute_path.data, buffer, offset);
    return result;
}

static bool condition_static_mesh_to_resource(Read_Entire_File_Result *asset_file_result, Asset *asset, Resource *resource, Memory_Arena *arena)
{
    return true;
}

static bool save_material_resource(Resource *resource, String render_pass_name, Pipeline_State_Settings pipeline_state_settings, String *property_names, Material_Property_Info *property_infos, U32 property_count)
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    Resource_Header header = make_resource_header(Asset_Type::MATERIAL, resource->asset_uuid, resource->uuid);
    header.resource_ref_count = resource->resource_refs.count;
    header.child_count = resource->children.count;

    U64 offset = 0;
    U8 *buffer = &scratch_memory.arena->base[scratch_memory.arena->offset];

    copy_memory(&buffer[offset], &header, sizeof(header));
    offset += sizeof(header);

    if (resource->resource_refs.count)
    {
        copy_memory(&buffer[offset], resource->resource_refs.data, sizeof(U64) * resource->resource_refs.count);
        offset += sizeof(U64) * resource->resource_refs.count;
    }

    if (resource->children.count)
    {
        copy_memory(&buffer[offset], resource->children.data, sizeof(U64) * resource->children.count);
        offset += sizeof(U64) * resource->children.count;
    }

    Material_Resource_Info info =
    {
        .settings = pipeline_state_settings,
        .render_pass_name_count = render_pass_name.count,
        .render_pass_name_offset = offset + sizeof(Material_Resource_Info),
        .property_count = u32_to_u16(property_count),
        .property_data_offset = (S64)(offset + sizeof(Material_Resource_Info) + render_pass_name.count)
    };

    copy_memory(&buffer[offset], &info, sizeof(info));
    offset += sizeof(info);

    copy_memory(&buffer[offset], (void *)render_pass_name.data, sizeof(char) * render_pass_name.count);
    offset += sizeof(char) * render_pass_name.count;

    U64 property_size = sizeof(Material_Property_Info) * property_count;
    copy_memory(&buffer[offset], (void *)property_infos, property_size);
    offset += property_size;

    for (U32 property_index = 0; property_index < property_count; property_index++)
    {
        String property_name = property_names[property_index];

        copy_memory(&buffer[offset], &property_name.count, sizeof(U64));
        offset += sizeof(U64);

        copy_memory(&buffer[offset], (void *)property_name.data, sizeof(char) * property_name.count);
        offset += sizeof(char) * property_name.count;
    }

    bool result = write_entire_file(resource->absolute_path.data, buffer, offset);
    return result;
}

static bool condition_scene_to_resource(Read_Entire_File_Result *asset_file_result, Asset *asset, Resource *resource, Memory_Arena *arena)
{
    Free_List_Allocator *allocator = get_general_purpose_allocator();

    String asset_path = asset->absolute_path;
    String asset_name = get_name(asset_path);
    String asset_parent_path = get_parent_path(asset_path);

    cgltf_options options = {};
    options.memory.alloc_func = _cgltf_alloc;
    options.memory.free_func = _cgltf_free;

    cgltf_data *data = nullptr;

    if (cgltf_parse(&options, asset_file_result->data, asset_file_result->size, &data) != cgltf_result_success)
    {
        HE_LOG(Resource, Fetal, "unable to parse asset file: %.*s\n", HE_EXPAND_STRING(asset_path));
        return false;
    }

    if (cgltf_load_buffers(&options, data, asset_path.data) != cgltf_result_success)
    {
        HE_LOG(Resource, Fetal, "unable to load buffers from asset file: %.*s\n", HE_EXPAND_STRING(asset_path));
        return false;
    }

    HE_DEFER { cgltf_free(data); };

    U64 *material_uuids = HE_ALLOCATE_ARRAY(arena, U64, data->materials_count);

    auto get_texture_uuid = [&](const cgltf_image *image) -> U64
    {
        if (!image->uri)
        {
            return HE_MAX_U64;
        }

        String uri = HE_STRING(image->uri);
        String name = get_name(uri);
        String parent_path = get_parent_path(asset_path);
        String resource_path = format_string(arena, "%.*s/%.*s.hres", HE_EXPAND_STRING(parent_path), HE_EXPAND_STRING(name));
        String relative_resource_path = sub_string(resource_path, resource_system_state->resource_path.count + 1);;

        auto it = path_to_resource_index.find(relative_resource_path);
        if (it == path_to_resource_index.end())
        {
            return HE_MAX_U64;
        }

        Resource &resource = resource_system_state->resources[it->second];
        return resource.uuid;
    };

    for (U32 material_index = 0; material_index < data->materials_count; material_index++)
    {
        cgltf_material *material = &data->materials[material_index];
        String material_resource_name;

        if (material->name)
        {
            String name = HE_STRING(material->name);
            material_resource_name = format_string(arena, "material_%.*s", HE_EXPAND_STRING(name));
        }
        else
        {
            material_resource_name = format_string(arena, "material_%.*s_%d", HE_EXPAND_STRING(asset_name), material_index);
        }

        U64 albedo_texture_uuid = HE_MAX_U64;
        U64 occlusion_roughness_metallic_texture_uuid = HE_MAX_U64;
        U64 normal_texture_uuid = HE_MAX_U64;

        if (material->has_pbr_metallic_roughness)
        {
            if (material->pbr_metallic_roughness.base_color_texture.texture)
            {
                const cgltf_image *image = material->pbr_metallic_roughness.base_color_texture.texture->image;
                albedo_texture_uuid = get_texture_uuid(image);
            }

            if (material->pbr_metallic_roughness.metallic_roughness_texture.texture)
            {
                const cgltf_image *image = material->pbr_metallic_roughness.metallic_roughness_texture.texture->image;
                occlusion_roughness_metallic_texture_uuid = get_texture_uuid(image);
            }
        }

        if (material->normal_texture.texture)
        {
            const cgltf_image *image = material->normal_texture.texture->image;
            normal_texture_uuid = get_texture_uuid(image);
        }

        String material_resource_absloute_path = format_string(arena, "%.*s/%.*s.hres", HE_EXPAND_STRING(asset_parent_path), HE_EXPAND_STRING(material_resource_name));
        String material_resource_relative_path = absolute_path_to_relative(material_resource_absloute_path);
        
        auto it = path_to_resource_index.find(material_resource_relative_path);
        U32 material_resource_index;
        
        if (it == path_to_resource_index.end())
        {
            material_resource_index = create_resource(copy_string(material_resource_absloute_path, allocator), Asset_Type::MATERIAL, asset->uuid);
            Resource *material_resource = &resource_system_state->resources[material_resource_index];
            append(&asset->resource_refs, material_resource->uuid);

            Resource_Ref shaders[] =
            {
                find_resource(HE_STRING_LITERAL("opaque_pbr_vert.hres")),
                find_resource(HE_STRING_LITERAL("opaque_pbr_frag.hres"))
            };

            for (Resource_Ref ref : shaders)
            {
                HE_ASSERT(ref.uuid != HE_MAX_U64);
                append(&material_resource->resource_refs, ref.uuid);

                auto it = uuid_to_resource_index.find(ref.uuid);
                Resource *shader_resource = &resource_system_state->resources[it->second];

                platform_lock_mutex(&shader_resource->mutex);
                append(&shader_resource->children, material_resource->uuid);
                platform_unlock_mutex(&shader_resource->mutex);
            }    
        }
        else
        {
            material_resource_index = it->second;
        }
        
        Resource *material_resource = &resource_system_state->resources[material_resource_index];
        material_uuids[material_index] = material_resource->uuid;

        // todo(amer): transparent materials...
        String render_pass_name = HE_STRING_LITERAL("opaque");

        Pipeline_State_Settings pipeline_state_settings =
        {
            .cull_mode = Cull_Mode::BACK,
            .front_face = Front_Face::COUNTER_CLOCKWISE,
            .fill_mode = Fill_Mode::SOLID,
            .depth_testing = true,
            .sample_shading = true,
        };

        String property_names[] =
        {
            HE_STRING_LITERAL("albedo_texture"),
            HE_STRING_LITERAL("albedo_color"),
            HE_STRING_LITERAL("normal_texture"),
            HE_STRING_LITERAL("occlusion_roughness_metallic_texture"),
            HE_STRING_LITERAL("roughness_factor"),
            HE_STRING_LITERAL("metallic_factor"),
            HE_STRING_LITERAL("reflectance"),
        };

        U32 property_count = HE_ARRAYCOUNT(property_names);
        Material_Property_Info *properties = HE_ALLOCATE_ARRAY(arena, Material_Property_Info, property_count);

        auto set_property = [&](const char *_property_name, Material_Property_Data data)
        {
            String property_name = HE_STRING(_property_name);
            for (U32 property_index = 0; property_index < property_count; property_index++)
            {
                if (property_name == property_names[property_index])
                {
                    Material_Property_Info *property_info = &properties[property_index];
                    property_info->data = data;
                    return;
                }
            }
        };

        set_property("albedo_texture", { .u64 = albedo_texture_uuid });
        set_property("normal_texture", { .u64 = normal_texture_uuid });
        set_property("occlusion_roughness_metallic_texture", { .u64 = occlusion_roughness_metallic_texture_uuid });
        set_property("albedo_color", { .v3 = *(glm::vec3 *)material->pbr_metallic_roughness.base_color_factor });
        set_property("roughness_factor", { .f32 = material->pbr_metallic_roughness.roughness_factor });
        set_property("metallic_factor", { .f32 = material->pbr_metallic_roughness.metallic_factor });
        set_property("reflectance", { .f32 = 0.04f });
        
        save_material_resource(material_resource, render_pass_name, pipeline_state_settings, property_names, properties, property_count);
    }

    U64 *static_mesh_uuids = HE_ALLOCATE_ARRAY(arena, U64, data->meshes_count);

    for (U32 static_mesh_index = 0; static_mesh_index < data->meshes_count; static_mesh_index++)
    {
        cgltf_mesh *static_mesh = &data->meshes[static_mesh_index];

        String static_mesh_resource_name;

        if (static_mesh->name)
        {
            String name = HE_STRING(static_mesh->name);
            static_mesh_resource_name = format_string(arena, "static_mesh_%.*s", HE_EXPAND_STRING(name));
        }
        else
        {
            static_mesh_resource_name = format_string(arena, "static_mesh_%.*s_%d", HE_EXPAND_STRING(asset_name), static_mesh_index);
        }

        String static_mesh_resource_absloute_path = format_string(arena, "%.*s/%.*s.hres", HE_EXPAND_STRING(asset_parent_path), HE_EXPAND_STRING(static_mesh_resource_name));
        String static_mesh_resource_relative_path = absolute_path_to_relative(static_mesh_resource_absloute_path);

        U32 static_mesh_resource_index;
        auto it = path_to_resource_index.find(static_mesh_resource_relative_path);

        if (it == path_to_resource_index.end())
        {
            static_mesh_resource_index = create_resource(copy_string(static_mesh_resource_absloute_path, allocator), Asset_Type::STATIC_MESH, asset->uuid);
            
            Resource *static_mesh_resource = &resource_system_state->resources[static_mesh_resource_index];
            append(&asset->resource_refs, static_mesh_resource->uuid);
        }
        else
        {
            static_mesh_resource_index = it->second;
        }

        Resource *static_mesh_resource = &resource_system_state->resources[static_mesh_resource_index];
        static_mesh_uuids[static_mesh_index] = static_mesh_resource->uuid;

        save_static_mesh_resource(static_mesh_resource, static_mesh, data, material_uuids);
    }

    U64 offset = 0;
    U8 *buffer = &arena->base[arena->offset];
    Resource_Header header = make_resource_header(Asset_Type::SCENE, resource->asset_uuid, resource->uuid);
    copy_memory(&buffer[offset], &header, sizeof(header));
    offset += sizeof(header);

    if (resource->resource_refs.count)
    {
        copy_memory(&buffer[offset], resource->resource_refs.data, sizeof(U64) * resource->resource_refs.count);
        offset += sizeof(U64) * resource->resource_refs.count;
    }

    if (resource->children.count)
    {
        copy_memory(&buffer[offset], resource->children.data, sizeof(U64) * resource->children.count);
        offset += sizeof(U64) * resource->children.count;
    }

    U64 string_size = 0;
    cgltf_scene *scene = &data->scenes[0];

    for (U32 node_index = 0; node_index < scene->nodes_count; node_index++)
    {
        cgltf_node *node = scene->nodes[node_index];
        HE_ASSERT(node->name);

        String node_name = HE_STRING(node->name);
        string_size += node_name.count;
    }

    Scene_Resource_Info info =
    {
        .node_count = u64_to_u32(data->nodes_count),
        .node_data_offset = offset + sizeof(Scene_Resource_Info) + string_size
    };

    copy_memory(&buffer[offset], &info, sizeof(info));
    offset += sizeof(info);

    U64 node_name_data_file_offset = offset;

    for (U32 node_index = 0; node_index < data->nodes_count; node_index++)
    {
        cgltf_node *node = scene->nodes[node_index];

        String node_name = HE_STRING(node->name);
        U64 node_name_size = sizeof(char) * node_name.count;

        copy_memory(&buffer[offset], (void *)node_name.data, node_name_size);
        offset += node_name_size;
    }

    for (U32 node_index = 0; node_index < data->nodes_count; node_index++)
    {
        cgltf_node *node = &data->nodes[node_index];

        U64 static_mesh_uuid = HE_MAX_U64;
        if (node->mesh)
        {
            U32 mesh_index = (U32)(node->mesh - data->meshes);
            static_mesh_uuid = static_mesh_uuids[mesh_index];
        }

        String node_name = HE_STRING(node->name);

        glm::quat rotation = { node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2] };

        S32 parent_index = node->parent ? (S32)(node->parent - data->nodes) : -1;

        Scene_Node_Info scene_node_info =
        {
            .name_count = u64_to_u32(node_name.count),
            .name_offset = node_name_data_file_offset,
            .parent_index = parent_index,
            .transform =
            {
                .position = *(glm::vec3*)&node->translation,
                .rotation = rotation,
                .euler_angles = glm::degrees(glm::eulerAngles(rotation)),
                .scale = *(glm::vec3*)&node->scale
            },
            .static_mesh_uuid = static_mesh_uuid
        };

        copy_memory(&buffer[offset], &scene_node_info, sizeof(scene_node_info));
        offset += sizeof(scene_node_info);

        node_name_data_file_offset += node_name.count;
    }
    
    bool result = write_entire_file(resource->absolute_path.data, buffer, offset);
    return result;
}

static Asset_Type find_asset_type_from_extension(const String &extension, Resource_Type_Info *info = nullptr)
{
    for (U32 i = 0; i < (U32)Asset_Type::COUNT; i++)
    {
        Asset_Conditioner &conditioner = resource_system_state->resource_type_infos[i].conditioner;
        for (U32 j = 0; j < conditioner.extension_count; j++)
        {
            if (conditioner.extensions[j] == extension)
            {
                if (info)
                {
                    info = &resource_system_state->resource_type_infos[i];
                }
                return (Asset_Type)i;
            }
        }
    }
    return Asset_Type::COUNT;
}

static bool load_static_mesh_resource(Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena)
{
    U64 file_offset = sizeof(Resource_Header) + sizeof(U64) * resource->resource_refs.count;

    bool success = true;

    Static_Mesh_Resource_Info info;
    success &= platform_read_data_from_file(open_file_result, file_offset, &info, sizeof(info));
    file_offset += sizeof(info);

    Free_List_Allocator *allocator = get_general_purpose_allocator();

    Sub_Mesh_Info *sub_mesh_infos = (Sub_Mesh_Info *)HE_ALLOCATE_ARRAY(allocator, U8, sizeof(Sub_Mesh_Info) * info.sub_mesh_count);
    success &= platform_read_data_from_file(open_file_result, info.sub_mesh_data_offset, sub_mesh_infos, sizeof(Sub_Mesh_Info) * info.sub_mesh_count);

    U64 data_size = open_file_result->size - info.data_offset;
    U8 *data = HE_ALLOCATE_ARRAY(resource_system_state->resource_allocator, U8, data_size);
    success &= platform_read_data_from_file(open_file_result, info.data_offset, data, data_size);

    U32 index_count = 0;
    U32 vertex_count = 0;

    Dynamic_Array< Sub_Mesh > sub_meshes;
    init(&sub_meshes, info.sub_mesh_count);

    for (U32 sub_mesh_index = 0; sub_mesh_index < info.sub_mesh_count; sub_mesh_index++)
    {
        Sub_Mesh_Info *sub_mesh_info = &sub_mesh_infos[sub_mesh_index];

        Sub_Mesh *sub_mesh = &sub_meshes[sub_mesh_index];
        sub_mesh->vertex_count = sub_mesh_info->vertex_count;
        sub_mesh->index_count = sub_mesh_info->index_count;
        sub_mesh->vertex_offset = u64_to_u32(vertex_count);
        sub_mesh->index_offset = u64_to_u32(index_count);
        sub_mesh->material_uuid = sub_mesh_info->material_uuid;

        Resource_Ref material_ref = { sub_mesh_info->material_uuid };
        aquire_resource(material_ref);

        index_count += sub_mesh_info->index_count;
        vertex_count += sub_mesh_info->vertex_count;
    }

    U16 *indices = (U16 *)data;
    U8 *vertex_data = data + sizeof(U16) * index_count;
    glm::vec3 *positions = (glm::vec3 *)vertex_data;
    glm::vec2 *uvs = (glm::vec2 *)(vertex_data + sizeof(glm::vec3) * vertex_count);
    glm::vec3 *normals = (glm::vec3 *)(vertex_data + (sizeof(glm::vec3) + sizeof(glm::vec2)) * vertex_count);
    glm::vec4 *tangents = (glm::vec4 *)(vertex_data + (sizeof(glm::vec3) + sizeof(glm::vec2) + sizeof(glm::vec3)) * vertex_count);

    append(&resource->allocation_group.allocations, (void *)data);    

    Static_Mesh_Descriptor static_mesh_descriptor =
    {
        .vertex_count = vertex_count,
        .index_count = index_count,
        .positions = positions,
        .normals = normals,
        .uvs = uvs,
        .tangents = tangents,
        .indices = indices,
        .sub_meshes = sub_meshes,
        .allocation_group = &resource->allocation_group,
    };

    Static_Mesh_Handle static_mesh_handle = renderer_create_static_mesh(static_mesh_descriptor);
    resource->index = static_mesh_handle.index;
    resource->generation = static_mesh_handle.generation;
    return success;
}

static void unload_static_mesh_resource(Resource *resource)
{
    HE_ASSERT(resource->state != Resource_State::UNLOADED);

    Render_Context render_context = get_render_context();

    Static_Mesh_Handle static_mesh_handle = { resource->index, resource->generation };

    if (is_valid_handle(&render_context.renderer_state->static_meshes, static_mesh_handle) && static_mesh_handle != render_context.renderer_state->default_static_mesh)
    {
        renderer_destroy_static_mesh(static_mesh_handle);
        resource->index = render_context.renderer_state->default_static_mesh.index;
        resource->generation = render_context.renderer_state->default_static_mesh.generation;
    }
}

static bool load_scene_resource(Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena)
{
    U64 file_offset = sizeof(Resource_Header) + sizeof(U64) * resource->resource_refs.count + sizeof(U64) * resource->children.count;

    Render_Context render_context = get_render_context();

    bool success = true;

    Scene_Resource_Info info;
    success &= platform_read_data_from_file(open_file_result, file_offset, &info, sizeof(info));
    
    Scene_Handle scene_handle = renderer_create_scene(info.node_count, info.node_count);
    Scene *scene = renderer_get_scene(scene_handle);

    for (U32 node_index = 0; node_index < info.node_count; node_index++)
    {
        Scene_Node_Info node_info;
        success &= platform_read_data_from_file(open_file_result, info.node_data_offset + sizeof(Scene_Node_Info) * node_index, &node_info, sizeof(node_info));

        U64 node_name_data_size = sizeof(char) * node_info.name_count;
        char *node_name_data = HE_ALLOCATE_ARRAY(get_general_purpose_allocator(), char, node_info.name_count + 1);
        node_name_data[node_info.name_count] = '\0';

        success &= platform_read_data_from_file(open_file_result, node_info.name_offset, node_name_data, node_name_data_size);

        String node_name = { node_name_data, node_info.name_count };
        Scene_Node *node = &scene->nodes[node_index];
        node->parent = nullptr;
        node->first_child = nullptr;
        node->last_child = nullptr;
        node->next_sibling = nullptr;
        node->prev_sibling = nullptr;

        node->name = node_name;
        node->static_mesh_uuid = node_info.static_mesh_uuid;
        node->transform = node_info.transform;

        Resource_Ref static_mesh_ref = { node_info.static_mesh_uuid };
        aquire_resource(static_mesh_ref);

        if (node_info.parent_index != -1)
        {
            Scene_Node *parent = &scene->nodes[node_info.parent_index];   
            add_child(parent, node);
        }
    }
    
    // todo(amer): temprary we add the this root scene to.
    platform_lock_mutex(&render_context.renderer_state->root_scene_node_mutex);
    add_child(&render_context.renderer_state->root_scene_node, &scene->nodes[0]);
    platform_unlock_mutex(&render_context.renderer_state->root_scene_node_mutex);

    resource->index = scene_handle.index;
    resource->generation = scene_handle.generation;
    return success;
}

static void unload_scene_resource(Resource *resource)
{
    Render_Context render_context = get_render_context();

    Scene_Handle scene_handle = { .index = resource->index, .generation = resource->generation };
    Scene *scene = renderer_get_scene(scene_handle);

    platform_lock_mutex(&render_context.renderer_state->root_scene_node_mutex);
    remove_child(&render_context.renderer_state->root_scene_node, &scene->nodes[0]);
    platform_unlock_mutex(&render_context.renderer_state->root_scene_node_mutex);
    
    renderer_destroy_scene(scene_handle);

    // todo(amer): default scene node.
    resource->index = -1;
    resource->generation = 0;
}

//==================================== Jobs ==================================================
struct Condition_Asset_Job_Data
{
    U32 asset_index;
    U32 resource_index;
};

static Job_Result condition_asset_job(const Job_Parameters &params)
{
    Condition_Asset_Job_Data *job_data = (Condition_Asset_Job_Data *)params.data;

    Asset *asset = &resource_system_state->assets[job_data->asset_index];
    HE_ASSERT(asset->state == Asset_State::PENDING);

    platform_lock_mutex(&asset->mutex);
    HE_DEFER { platform_unlock_mutex(&asset->mutex); };

    Resource *resource = &resource_system_state->resources[job_data->resource_index];

    String asset_path = asset->absolute_path;
    String resource_path = resource->absolute_path;

    Asset_Conditioner &conditioner = resource_system_state->resource_type_infos[(U32)resource->type].conditioner;

    Read_Entire_File_Result asset_file_result = read_entire_file(asset_path.data, params.arena);

    if (!asset_file_result.success)
    {
        HE_LOG(Resource, Trace, "failed to open asset file: %.*s\n", HE_EXPAND_STRING(asset_path));
        asset->state = Asset_State::UNCONDITIONED;
        return Job_Result::FAILED;
    }

    if (!conditioner.condition_asset(&asset_file_result, asset, resource, params.arena))
    {
        HE_LOG(Resource, Trace, "failed to condition asset: %.*s\n", HE_EXPAND_STRING(asset_path));
        asset->state = Asset_State::UNCONDITIONED;
        return Job_Result::FAILED;
    }

    HE_LOG(Resource, Trace, "successfully conditioned asset: %.*s\n", HE_EXPAND_STRING(asset_path));

    asset->state = Asset_State::CONDITIONED;
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

    HE_ASSERT(resource->asset_uuid != HE_MAX_U64);
    auto it = uuid_to_asset_index.find(resource->asset_uuid);
    HE_ASSERT(it != uuid_to_asset_index.end());
    Asset *asset = &resource_system_state->assets[it->second];
    if (asset->state != Asset_State::CONDITIONED)
    {
        // todo(amer): should we delete the resource file here...
        HE_LOG(Resource, Trace, "failed to load resource: %.*s asset is not conditioned\n", HE_EXPAND_STRING(resource->relative_path));
        return Job_Result::FAILED;
    }

    platform_lock_mutex(&resource->mutex);
    HE_DEFER {  platform_unlock_mutex(&resource->mutex); };

    Resource_Type_Info &info = resource_system_state->resource_type_infos[(U32)resource->type];
    bool use_allocation_group = info.loader.use_allocation_group;

    resource->allocation_group.resource_name = resource->relative_path;

    if (use_allocation_group)
    {
        Renderer_Semaphore_Descriptor semaphore_descriptor =
        {
            .initial_value = 0
        };

        reset(&resource->allocation_group.allocations);
        resource->allocation_group.semaphore = renderer_create_semaphore(semaphore_descriptor);
        resource->allocation_group.resource_index = (S32)index_of(&resource_system_state->resources, resource);
        resource->allocation_group.target_value = 0;
    }

    Open_File_Result open_file_result = platform_open_file(resource->absolute_path.data, OpenFileFlag_Read);

    if (!open_file_result.success)
    {
        // todo(amer): we should free or reset the semaphore here is we are using allocation groups
        resource->state = Resource_State::UNLOADED;
        resource->ref_count = 0;
        HE_LOG(Resource, Fetal, "failed to open resource file: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
        return Job_Result::FAILED;
    }

    HE_DEFER { platform_close_file(&open_file_result); };

    bool success = info.loader.load(&open_file_result, resource, params.arena);

    if (!success)
    {
        // todo(amer): we should free or reset the semaphore here is we are using allocation groups
        resource->state = Resource_State::UNLOADED;
        resource->ref_count = 0;

        HE_LOG(Resource, Trace, "failed to load resource: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
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
        resource->state = Resource_State::LOADED;
        HE_LOG(Resource, Trace, "resource loaded: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
    }

    return Job_Result::SUCCEEDED;
}

static void on_path(String *path, bool is_directory)
{
    if (is_directory)
    {
        return;
    }

    Memory_Arena *arena = get_permenent_arena();
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    String absolute_path = copy_string(*path, arena);
    String relative_path = absolute_path_to_relative(absolute_path);
    String extension = get_extension(relative_path);

    if (extension == resource_extension)
    {
        Open_File_Result result = platform_open_file(absolute_path.data, OpenFileFlag_Read);

        if (!result.success)
        {
            HE_LOG(Resource, Fetal, "failed to open resource file: %.*s\n", HE_EXPAND_STRING(relative_path));
            return;
        }

        HE_DEFER { platform_close_file(&result); };

        Resource_Header header;
        bool success = platform_read_data_from_file(&result, 0, &header, sizeof(header));
        if (!success)
        {
            HE_LOG(Resource, Fetal, "failed to read header of resource file: %.*s\n", HE_EXPAND_STRING(relative_path));
            return;
        }

        if (strncmp(header.magic_value, "HOPE", 4) != 0)
        {
            HE_LOG(Resource, Fetal, "invalid header magic value of resource file: %.*s, expected 'HOPE' found: %.*s\n", HE_EXPAND_STRING(relative_path), header.magic_value);
            return;
        }

        if (header.type > (U32)Asset_Type::COUNT)
        {
            HE_LOG(Resource, Fetal, "unregistered type of resource file: %.*s, max type value is '%d' found: '%d'\n", HE_EXPAND_STRING(relative_path), (U32)Asset_Type::COUNT - 1, header.type);
            return;
        }

        Resource_Type_Info &info = resource_system_state->resource_type_infos[header.type];
        if (header.version != info.version)
        {
            // todo(amer): condition assets that's doesn't have the last version.
            HE_LOG(Resource, Fetal, "invalid version of resource file: %.*s, expected version '%d' found: '%d'\n", HE_EXPAND_STRING(relative_path), info.version, header.version);
            return;
        }

        U32 resource_index = create_resource(absolute_path, (Asset_Type)header.type, header.asset_uuid, header.uuid);
        Resource &resource = resource_system_state->resources[resource_index];
        set_count(&resource.resource_refs, header.resource_ref_count);
        set_count(&resource.children, header.child_count);

        U64 offset = 0;

        if (header.resource_ref_count)
        {
            platform_read_data_from_file(&result, sizeof(Resource_Header), resource.resource_refs.data, sizeof(U64) * header.resource_ref_count);
            offset += sizeof(sizeof(U64) * header.resource_ref_count);
        }

        if (header.child_count)
        {
            platform_read_data_from_file(&result, sizeof(Resource_Header) + offset, resource.children.data, sizeof(U64) * header.child_count);
        }

        return;
    }

    Asset_Type asset_type = find_asset_type_from_extension(extension);
    if (asset_type == Asset_Type::COUNT)
    {
        return;
    }

    S32 asset_index = -1;
    auto it = path_to_asset_index.find(relative_path);
    if (it == path_to_asset_index.end())
    {
        asset_index = create_asset(absolute_path, generate_uuid(), asset_type);
    }
    else
    {
        asset_index = it->second;
    }

    Asset *asset = &resource_system_state->assets[asset_index];
    String resource_absolute_path = make_resource_absloute_path(asset->absolute_path, scratch_memory.arena);
    U64 last_write_time = platform_get_file_last_write_time(asset->absolute_path.data);

    if (!file_exists(resource_absolute_path))
    {
        U32 resource_index = create_resource(copy_string(resource_absolute_path, arena), asset->type, asset->uuid);
        Resource *resource = &resource_system_state->resources[resource_index];
        append(&asset->resource_refs, resource->uuid);
        append(&resource_system_state->assets_to_condition, (U32)asset_index);
    }
    else if (last_write_time != asset->last_write_time)
    {
        append(&resource_system_state->assets_to_condition, (U32)asset_index);
        asset->last_write_time = last_write_time;
    }
    else
    {
        asset->state = Asset_State::CONDITIONED;
    }
}

static bool serialize_asset_database(const String &path)
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    U8 *buffer = &scratch_memory.arena->base[scratch_memory.arena->offset];
    U64 offset = scratch_memory.arena->offset;

    Asset_Database_Header *header = HE_ALLOCATE(scratch_memory.arena, Asset_Database_Header);
    header->asset_count = resource_system_state->assets.count;

    for (Asset &asset : resource_system_state->assets)
    {
        Asset_Info *info = HE_ALLOCATE(scratch_memory.arena, Asset_Info);
        info->relative_path_count = asset.relative_path.count;
        info->uuid = asset.uuid;
        info->last_write_time = asset.last_write_time;
        info->resource_refs_count = asset.resource_refs.count;

        char *relative_path = HE_ALLOCATE_ARRAY_UNALIGNED(scratch_memory.arena, char, asset.relative_path.count);
        copy_memory(relative_path, asset.relative_path.data, sizeof(char) * asset.relative_path.count);

        if (asset.resource_refs.count)
        {
            U64 *uuids = HE_ALLOCATE_ARRAY_UNALIGNED(scratch_memory.arena, U64, asset.resource_refs.count);
            copy_memory(uuids, asset.resource_refs.data, sizeof(U64) * asset.resource_refs.count);
        }
    }

    bool success = write_entire_file(path.data, buffer, scratch_memory.arena->offset - offset);
    if (!success)
    {
        HE_LOG(Resource, Fetal, "failed to serialize asset database\n");
    }
    return success;
}

static bool deserialize_asset_database(const String &path)
{
    Memory_Arena *arena = get_permenent_arena();
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    Read_Entire_File_Result result = read_entire_file(path.data, scratch_memory.arena);
    if (!result.success)
    {
        HE_LOG(Resource, Fetal, "failed to deserialize asset database\n");
        return false;
    }

    U8 *data = result.data;
    U64 offset = 0;

    Asset_Database_Header *header = (Asset_Database_Header *)(&data[offset]);
    offset += sizeof(Asset_Database_Header);

    for (U32 i = 0; i < header->asset_count; i++)
    {
        Asset_Info *info = (Asset_Info *)(&data[offset]);
        offset += sizeof(Asset_Info);

        char *relative_path_data = (char *)(&data[offset]);
        offset += sizeof(char) * info->relative_path_count;

        String relative_path = { relative_path_data, info->relative_path_count };
        String absolute_path = format_string(arena, "%.*s/%.*s", HE_EXPAND_STRING(resource_system_state->resource_path), HE_EXPAND_STRING(relative_path));

        U64 *resource_refs = (U64 *)(&data[offset]);
        offset += sizeof(U64) * info->resource_refs_count;

        // todo(amer): asset validation here...
        String extension = get_extension(relative_path);
        Asset_Type asset_type = find_asset_type_from_extension(extension);

        U32 asset_index = create_asset(absolute_path, info->uuid, asset_type);
        Asset *asset = &resource_system_state->assets[asset_index];
        asset->last_write_time = info->last_write_time;

        if (info->resource_refs_count)
        {
            set_count(&asset->resource_refs, info->resource_refs_count);
            copy_memory(asset->resource_refs.data, resource_refs, sizeof(U64) * info->resource_refs_count);
        }
    }

    return true;
}

bool init_resource_system(const String &resource_directory_name, Engine *engine)
{
    if (resource_system_state)
    {
        HE_LOG(Resource, Fetal, "resource system already initialized\n");
        return false;
    }

    uuid_to_resource_index[HE_MAX_U64] = -1;
    uuid_to_asset_index[HE_MAX_U64] = -1;

    Memory_Context memory_context = use_memory_context();

    resource_system_state = HE_ALLOCATE(memory_context.permenent, Resource_System_State);

    // todo(amer): @Hack using HE_MAX_U16 at initial capacity so we can append without copying
    // because we need to hold pointers to resources.
    init(&resource_system_state->assets, 0, HE_MAX_U16);
    init(&resource_system_state->resources, 0, HE_MAX_U16);

    String working_directory = get_current_working_directory(memory_context.permenent);
    sanitize_path(working_directory);

    String resource_path = format_string(memory_context.permenent, "%.*s/%.*s", HE_EXPAND_STRING(working_directory), HE_EXPAND_STRING(resource_directory_name));
    if (!directory_exists(resource_path))
    {
        HE_LOG(Resource, Fetal, "invalid resource path: %.*s\n", HE_EXPAND_STRING(resource_path));
        return false;
    }

    resource_system_state->asset_database_path = format_string(memory_context.permenent, "%.*s/assets.hdb", HE_EXPAND_STRING(resource_path));

    Render_Context render_context = get_render_context();
    resource_system_state->resource_path = resource_path;
    resource_system_state->resource_allocator = &render_context.renderer_state->transfer_allocator;

    {
        static String extensions[] =
        {
            HE_STRING_LITERAL("jpeg"),
            HE_STRING_LITERAL("png"),
            HE_STRING_LITERAL("tga"),
            HE_STRING_LITERAL("psd")
        };

        Asset_Conditioner conditioner =
        {
            .extension_count = HE_ARRAYCOUNT(extensions),
            .extensions = extensions,
            .condition_asset = &condition_texture_to_resource
        };

        Resource_Loader loader =
        {
            .use_allocation_group = true,
            .load = &load_texture_resource,
            .unload = &unload_texture_resource
        };

        register_asset(Asset_Type::TEXTURE, "texture", 1, conditioner, loader);
    }

    {
        static String extensions[] =
        {
            HE_STRING_LITERAL("vert"),
            HE_STRING_LITERAL("frag"),
        };

        Asset_Conditioner conditioner =
        {
            .extension_count = HE_ARRAYCOUNT(extensions),
            .extensions = extensions,
            .condition_asset = &condition_shader_to_resource,
        };

        Resource_Loader loader =
        {
            .use_allocation_group = false,
            .load = &load_shader_resource,
            .unload = &unload_shader_resource,
        };

        register_asset(Asset_Type::SHADER, "shader", 1, conditioner, loader);
    }

    {
        static String extensions[] =
        {
            HE_STRING_LITERAL("matxxx"),  // todo(amer): are we going to support material assets
        };

        Asset_Conditioner conditioner =
        {
            .extension_count = HE_ARRAYCOUNT(extensions),
            .extensions = extensions,
            .condition_asset = &condition_material_to_resource,
        };

        Resource_Loader loader =
        {
            .use_allocation_group = false,
            .load = &load_material_resource,
            .unload = &unload_material_resource,
        };

        register_asset(Asset_Type::MATERIAL, "material", 1, conditioner, loader);
    }

    {
        static String extensions[] =
        {
            HE_STRING_LITERAL("static_meshxxx") // todo(amer): are we going to support static mesh assets
        };

        Asset_Conditioner conditioner =
        {
            .extension_count = HE_ARRAYCOUNT(extensions),
            .extensions = extensions,
            .condition_asset = &condition_static_mesh_to_resource
        };

        Resource_Loader loader =
        {
            .use_allocation_group = true,
            .load = &load_static_mesh_resource,
            .unload = &unload_static_mesh_resource
        };

        register_asset(Asset_Type::STATIC_MESH, "static mesh", 1, conditioner, loader);
    }

    {
        static String extensions[] =
        {
            HE_STRING_LITERAL("gltf")
        };

        Asset_Conditioner conditioner =
        {
            .extension_count = HE_ARRAYCOUNT(extensions),
            .extensions = extensions,
            .condition_asset = &condition_scene_to_resource
        };

        Resource_Loader loader =
        {
            .use_allocation_group = false,
            .load = &load_scene_resource,
            .unload = &unload_scene_resource
        };

        register_asset(Asset_Type::SCENE, "scene", 1, conditioner, loader);
    }

    if (file_exists(resource_system_state->asset_database_path))
    {
        deserialize_asset_database(resource_system_state->asset_database_path);
    }

    init(&resource_system_state->assets_to_condition);

    bool recursive = true;
    platform_walk_directory(resource_path.data, recursive, &on_path);

    for (U32 asset_index : resource_system_state->assets_to_condition)
    {
        Asset *asset = &resource_system_state->assets[asset_index];

        String resource_absloute_path = make_resource_absloute_path(asset->absolute_path, memory_context.temp);
        String resource_relative_path = absolute_path_to_relative(resource_absloute_path);
        auto it = path_to_resource_index.find(resource_relative_path);
        HE_ASSERT(it != path_to_resource_index.end());

        platform_lock_mutex(&asset->mutex);
        asset->state = Asset_State::PENDING;
        platform_unlock_mutex(&asset->mutex);

        Condition_Asset_Job_Data data =
        {
            .asset_index = asset_index,
            .resource_index = it->second
        };

        Job_Data job_data =
        {
            .parameters =
            {
                .data = &data,
                .size = sizeof(Condition_Asset_Job_Data)
            },
            .proc = &condition_asset_job
        };

        execute_job(job_data);
    }

    return true;
}

void deinit_resource_system()
{
    serialize_asset_database(resource_system_state->asset_database_path);
}

bool register_asset(Asset_Type type, const char *name, U32 version, Asset_Conditioner conditioner, Resource_Loader loader)
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

Resource_Ref find_resource(const char *relative_path)
{
    return find_resource(HE_STRING(relative_path));
}

static void aquire_resource(Resource *resource)
{
    Dynamic_Array< Job_Handle > wait_for_jobs;
    init(&wait_for_jobs);
    HE_DEFER { deinit(&wait_for_jobs); };

    for (U64 uuid : resource->resource_refs)
    {
        auto it = uuid_to_resource_index.find(uuid);
        HE_ASSERT(it != uuid_to_resource_index.end());
        Resource *ref_resource = &resource_system_state->resources[it->second];
        aquire_resource(ref_resource);
        append(&wait_for_jobs, ref_resource->job_handle);
    }

    platform_lock_mutex(&resource->mutex);
    resource->ref_count++;

    if (resource->state == Resource_State::UNLOADED)
    {
        resource->state = Resource_State::PENDING;
        platform_unlock_mutex(&resource->mutex);

        Load_Resource_Job_Data data
        {
            .resource = resource
        };

        Job_Data job_data = {};
        job_data.parameters.data = &data;
        job_data.parameters.size = sizeof(data);
        job_data.proc = load_resource_job;
        resource->job_handle = execute_job(job_data, to_array_view(wait_for_jobs));
    }
    else
    {
        platform_unlock_mutex(&resource->mutex);
    }
}

Resource_Ref aquire_resource(const String &relative_path)
{
    auto it = path_to_resource_index.find(relative_path);
    if (it == path_to_resource_index.end())
    {
        return { HE_MAX_U64 };
    }
    Resource *resource = &resource_system_state->resources[it->second];
    aquire_resource(resource);
    Resource_Ref ref = { resource->uuid };
    return ref;
}

Resource_Ref aquire_resource(const char *relative_path)
{
    return aquire_resource(HE_STRING(relative_path));
}

bool aquire_resource(Resource_Ref ref)
{
    if (ref.uuid == HE_MAX_U64)
    {
        return false;
    }

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
        Resource_Type_Info &info = resource_system_state->resource_type_infos[(U32)resource->type];
        info.loader.unload(resource);
        resource->index = -1;
        resource->generation = 0;
        resource->state = Resource_State::UNLOADED;
    }
    platform_unlock_mutex(&resource->mutex);
}

Resource *get_resource(Resource_Ref ref)
{
    auto it = uuid_to_resource_index.find(ref.uuid);
    if (it == uuid_to_resource_index.end())
    {
        return nullptr;
    }
    return &resource_system_state->resources[it->second];
}

Resource *get_resource(U32 index)
{
    HE_ASSERT(index >= 0 && index < resource_system_state->resources.count);
    return &resource_system_state->resources[index];
}

const Dynamic_Array< Resource >& get_resources()
{
    return resource_system_state->resources;
}

static void reload_child_resources(Resource *parent)
{
    for (U64 child_uuid : parent->children)
    {
        Dynamic_Array< Job_Handle > wait_for_jobs;
        init(&wait_for_jobs);

        HE_DEFER { deinit(&wait_for_jobs); };

        Resource *child_resource = get_resource({ .uuid = child_uuid });
        platform_lock_mutex(&child_resource->mutex);

        Resource_Type_Info &info = resource_system_state->resource_type_infos[(U32)child_resource->type];
        if (child_resource->state == Resource_State::LOADED)
        {
            info.loader.unload(child_resource);
        }

        if (child_resource->state == Resource_State::UNLOADED || child_resource->state == Resource_State::LOADED)
        {
            child_resource->state = Resource_State::PENDING;
        }
        else
        {
            append(&wait_for_jobs, child_resource->job_handle);
        }

        platform_unlock_mutex(&child_resource->mutex);

        Load_Resource_Job_Data data
        {
            .resource = child_resource
        };

        Job_Data job_data =
        {
            .parameters =
            {
                .data = &data,
                .size = sizeof(data)
            },
            .proc = &load_resource_job
        };

        for (U64 _uuid : child_resource->resource_refs)
        {
            Resource *r = get_resource({ .uuid = _uuid });
            append(&wait_for_jobs, r->job_handle);
        }

        child_resource->job_handle = execute_job(job_data, to_array_view(wait_for_jobs));
    }
}

void reload_resources()
{
    bool wait_once = false;

    for (U32 asset_index = 0; asset_index < resource_system_state->assets.count; asset_index++)
    {
        Asset &asset = resource_system_state->assets[asset_index];
        U64 last_write_time = platform_get_file_last_write_time(asset.absolute_path.data);
        if (last_write_time != asset.last_write_time)
        {
            // todo(amer): @Hack.
            if (!wait_once)
            {
                renderer_wait_for_gpu_to_finish_all_work();
                wait_once = true;
            }
            
            {
                Dynamic_Array<Job_Handle> wait_for_jobs;
                init(&wait_for_jobs);

                HE_DEFER { deinit(&wait_for_jobs); };

                platform_lock_mutex(&asset.mutex);

                asset.last_write_time = last_write_time;

                if (asset.state == Asset_State::UNCONDITIONED || asset.state == Asset_State::CONDITIONED)
                {
                    asset.state = Asset_State::PENDING;
                }
                else
                {
                    append(&wait_for_jobs, asset.job_handle);
                }

                platform_unlock_mutex(&asset.mutex);

                Condition_Asset_Job_Data data =
                {
                    .asset_index = asset_index,
                    .resource_index = uuid_to_resource_index.find(asset.resource_refs[0])->second
                };

                Job_Data job_data =
                {
                    .parameters =
                    {
                        .data = &data,
                        .size = sizeof(data)
                    },
                    .proc = &condition_asset_job
                };

                asset.job_handle = execute_job(job_data, to_array_view(wait_for_jobs));
            }

            {
                for (U32 i = 0; i < asset.resource_refs.count; i++)
                {
                    Dynamic_Array<Job_Handle> wait_for_jobs;
                    init(&wait_for_jobs);
                    HE_DEFER { deinit(&wait_for_jobs); };

                    Resource *r = get_resource({ .uuid = asset.resource_refs[i] });
                    platform_lock_mutex(&r->mutex);

                    Resource_Type_Info &info = resource_system_state->resource_type_infos[(U32)r->type];
                    if (r->state == Resource_State::LOADED)
                    {
                        info.loader.unload(r);
                    }

                    if (r->state == Resource_State::UNLOADED || r->state == Resource_State::LOADED)
                    {
                        r->state = Resource_State::PENDING;
                    }
                    else
                    {
                        append(&wait_for_jobs, r->job_handle);
                    }

                    platform_unlock_mutex(&r->mutex);

                    Load_Resource_Job_Data data =
                    {
                        .resource = r
                    };

                    Job_Data job_data =
                    {
                        .parameters =
                        {
                            .data = &data,
                            .size = sizeof(data)
                        },
                        .proc = &load_resource_job
                    };

                    append(&wait_for_jobs, asset.job_handle);

                    for (U64 uuid : r->resource_refs)
                    {
                        Resource *rr = get_resource({ .uuid = uuid });
                        append(&wait_for_jobs, rr->job_handle);
                    }

                    r->job_handle = execute_job(job_data, to_array_view(wait_for_jobs));
                    reload_child_resources(r);
                }
            }
        }
    }
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
    {
        ImGui::Begin("Assets");

        const char* coloum_names[] =
        {
            "No.",
            "UUID",
            "Type",
            "Asset",
            "Resource Refs"
        };

        ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTable("Table", HE_ARRAYCOUNT(coloum_names), flags))
        {
            for (U32 col = 0; col < HE_ARRAYCOUNT(coloum_names); col++)
            {
                ImGui::TableSetupColumn(coloum_names[col], ImGuiTableColumnFlags_WidthStretch);
            }

            ImGui::TableHeadersRow();

            for (U32 row = 0; row < resource_system_state->assets.count; row++)
            {
                Asset& asset = resource_system_state->assets[row];
                Resource_Type_Info& info = resource_system_state->resource_type_infos[(U32)asset.type];

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("%d", row + 1);

                ImGui::TableNextColumn();
                ImGui::Text("%#x", asset.uuid);

                ImGui::TableNextColumn();
                ImGui::Text("%.*s", HE_EXPAND_STRING(info.name));

                ImGui::TableNextColumn();
                ImGui::Text("%.*s", HE_EXPAND_STRING(asset.relative_path));

                ImGui::TableNextColumn();
                if (asset.resource_refs.count)
                {
                    for (U64 uuid : asset.resource_refs)
                    {
                        ImGui::Text("%#x", uuid);
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

    {
        ImGui::Begin("Resources");

        const char* coloum_names[] =
        {
            "No.",
            "UUID",
            "Asset UUID",
            "Type",
            "Resource",
            "State",
            "Ref Count",
            "Refs",
            "Children"
        };

        ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTable("Table", HE_ARRAYCOUNT(coloum_names), flags))
        {
            for (U32 col = 0; col < HE_ARRAYCOUNT(coloum_names); col++)
            {
                ImGui::TableSetupColumn(coloum_names[col], ImGuiTableColumnFlags_WidthStretch);
            }

            ImGui::TableHeadersRow();

            for (U32 row = 0; row < resource_system_state->resources.count; row++)
            {
                Resource& resource = resource_system_state->resources[row];
                Resource_Type_Info& info = resource_system_state->resource_type_infos[(U32)resource.type];

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("%d", row + 1);

                ImGui::TableNextColumn();
                ImGui::Text("%#x", resource.uuid);

                ImGui::TableNextColumn();
                ImGui::Text("%#x", resource.asset_uuid);

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
                    for (U64 uuid : resource.resource_refs)
                    {
                        ImGui::Text("%#x", uuid);
                    }
                }
                else
                {
                    ImGui::Text("None");
                }

                ImGui::TableNextColumn();
                if (resource.children.count)
                {
                    for (U64 uuid : resource.children)
                    {
                        ImGui::Text("%#x", uuid);
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
}