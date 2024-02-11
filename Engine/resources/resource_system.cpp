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
        return hash_string(str);
    }
};

static std::unordered_map< String, U32 > path_to_resource_index;
static std::unordered_map< String, U32 > path_to_asset_index;

static Resource_System_State *resource_system_state;

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

// ========================== Resources ====================================
static bool condition_texture_to_resource(Asset *asset, Resource *resource, Open_File_Result *asset_file, Open_File_Result *resource_file, Memory_Arena *arena)
{
    bool success = true;
    U8 *data = HE_ALLOCATE_ARRAY(arena, U8, asset_file->size);
    success &= platform_read_data_from_file(asset_file, 0, data, asset_file->size);

    S32 width;
    S32 height;
    S32 channels;

    stbi_uc *pixels = stbi_load_from_memory(data, u64_to_u32(asset_file->size), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
        return false;
    }

    HE_DEFER { stbi_image_free(pixels); };

    U64 offset = 0;

    Resource_Header header = make_resource_header(Asset_Type::TEXTURE, resource->asset_uuid, resource->uuid);

    success &= platform_write_data_to_file(resource_file, offset, &header, sizeof(Resource_Header));
    offset += sizeof(Resource_Header);

    Texture_Resource_Info texture_resource_info =
    {
        .width = (U32)width,
        .height = (U32)height,
        .format = Texture_Format::R8G8B8A8_SRGB,
        .mipmapping = true,
        .data_offset = sizeof(Resource_Header) + sizeof(Texture_Resource_Info)
    };

    success &= platform_write_data_to_file(resource_file, offset, &texture_resource_info, sizeof(Texture_Resource_Info));
    offset += sizeof(Texture_Resource_Info);

    success &= platform_write_data_to_file(resource_file, offset, pixels, width * height * sizeof(U32));

    return success;
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
    HE_ASSERT(resource->state == Resource_State::LOADED);
    Texture_Handle texture_handle = { resource->index, resource->generation };
    renderer_destroy_texture(texture_handle);
}

static bool condition_shader_to_resource(Asset *asset, Resource *resource, Open_File_Result *asset_file, Open_File_Result *resource_file, Memory_Arena *arena)
{
    // todo(amer): @Hack until we have a compiler instead of using cmd with glslangValidator
    platform_close_file(asset_file);
    platform_close_file(resource_file);

    String asset_path = asset->absolute_path;
    String resource_path = resource->absolute_path;

    String command = format_string(arena, "glslangValidator.exe -V --auto-map-locations %.*s -o %.*s", HE_EXPAND_STRING(asset_path), HE_EXPAND_STRING(resource_path));
    bool executed = platform_execute_command(command.data);
    HE_ASSERT(executed);

    Read_Entire_File_Result spirv_binary_read_result = read_entire_file(resource_path.data, arena);
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

    Resource_Header header = make_resource_header(Asset_Type::SHADER, resource->asset_uuid, resource->uuid);

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

static bool load_shader_resource(Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena)
{
    bool success = true;

    Shader_Resource_Info info;
    success &= platform_read_data_from_file(open_file_result, sizeof(Resource_Header), &info, sizeof(info));

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

static bool condition_material_to_resource(Asset *asset, Resource *resource, Open_File_Result *asset_file, Open_File_Result *resource_file, Memory_Arena *arena)
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

    // wait_for_resource_refs_to_load(resource);

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

    Material_Property_Info *infos = nullptr;

    if (info.property_count)
    {
        infos = HE_ALLOCATE_ARRAY(arena, Material_Property_Info, info.property_count);
        success &= platform_read_data_from_file(open_file_result, info.property_data_offset, (void *)infos, sizeof(Material_Property_Info) * info.property_count);
    }

    Material_Descriptor material_descriptor =
    {
        .pipeline_state_handle = pipeline_state_handle,
        .property_info_count = info.property_count,
        .property_infos = infos
    };

    Material_Handle material_handle = renderer_create_material(material_descriptor);

    if (success)
    {
        resource->index = material_handle.index;
        resource->generation = material_handle.generation;
        resource->ref_count++;
        resource->state = Resource_State::LOADED;
    }
    else
    {
        resource->state = Resource_State::UNLOADED;
    }

    return success;
}

static void unload_material_resource(Resource *resource)
{
    HE_ASSERT(resource->state == Resource_State::LOADED);
    Shader_Handle shader_handle = { resource->index, resource->generation };
    renderer_destroy_shader(shader_handle);
}

static void* _cgltf_alloc(void* user, cgltf_size size)
{
    return allocate(get_general_purpose_allocator(), size, 8);
}

static void _cgltf_free(void* user, void *ptr)
{
    deallocate(get_general_purpose_allocator(), ptr);
}

static bool write_attribute_for_all_sub_meshes(Open_File_Result *resource_file, U64 *file_offset, cgltf_mesh *mesh, cgltf_attribute_type attribute_type)
{
    bool success = true;

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

                success &= platform_write_data_to_file(resource_file, *file_offset, data, element_size * element_count);
                *file_offset = *file_offset + element_size * element_count;
            }
        }
    }

    return success;
}

static bool create_static_mesh_resource(Resource *resource, cgltf_mesh *mesh, cgltf_data *data, U64 *material_uuids)
{
    resource->type = Asset_Type::STATIC_MESH;
    resource->state = Resource_State::UNLOADED;
    resource->ref_count = 0;
    resource->index = -1;
    resource->generation = 0;

    platform_create_mutex(&resource->mutex);

    auto it = uuid_to_asset_index.find(resource->asset_uuid);
    HE_ASSERT(it != uuid_to_asset_index.end());

    asset_mutex.lock();
    Asset &asset = resource_system_state->assets[it->second];
    append(&asset.resource_refs, resource->uuid);
    asset_mutex.unlock();

    Open_File_Result open_file_result = platform_open_file(resource->absolute_path.data, Open_File_Flags(OpenFileFlag_Write|OpenFileFlag_Truncate));

    if (!open_file_result.success)
    {
        HE_LOG(Resource, Fetal, "failed to open file: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
        return false;
    }

    HE_DEFER { platform_close_file(&open_file_result); };

    Open_File_Result *resource_file = &open_file_result;
    bool success = true;

    U64 file_offset = 0;
    Resource_Header header = make_resource_header(Asset_Type::STATIC_MESH, resource->asset_uuid, resource->uuid);
    success &= platform_write_data_to_file(resource_file, file_offset, &header, sizeof(header));
    file_offset += sizeof(header);

    Static_Mesh_Resource_Info info =
    {
        .sub_mesh_count = (U16)mesh->primitives_count,
        .sub_mesh_data_offset = file_offset + sizeof(Static_Mesh_Resource_Info),
        .data_offset = file_offset + sizeof(Static_Mesh_Resource_Info) + sizeof(Sub_Mesh_Info) * mesh->primitives_count
    };

    success &= platform_write_data_to_file(resource_file, file_offset, &info, sizeof(info));
    file_offset += sizeof(info);

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

        success &= platform_write_data_to_file(resource_file, file_offset, &sub_mesh_info, sizeof(sub_mesh_info));
        file_offset += sizeof(sub_mesh_info);
    }

    for (U32 sub_mesh_index = 0; sub_mesh_index < (U32)mesh->primitives_count; sub_mesh_index++)
    {
        cgltf_primitive *primitive = &mesh->primitives[sub_mesh_index];
        HE_ASSERT(primitive->type == cgltf_primitive_type_triangles);

        const auto *accessor = primitive->indices;
        const auto *view = accessor->buffer_view;
        U8 *data = (U8 *)view->buffer->data + view->offset + accessor->offset;

        success &= platform_write_data_to_file(resource_file, file_offset, data, sizeof(U16) * primitive->indices->count);
        file_offset += sizeof(U16) * primitive->indices->count;
    }

    success &= write_attribute_for_all_sub_meshes(resource_file, &file_offset, mesh, cgltf_attribute_type_position);
    success &= write_attribute_for_all_sub_meshes(resource_file, &file_offset, mesh, cgltf_attribute_type_texcoord);
    success &= write_attribute_for_all_sub_meshes(resource_file, &file_offset, mesh, cgltf_attribute_type_normal);
    success &= write_attribute_for_all_sub_meshes(resource_file, &file_offset, mesh, cgltf_attribute_type_tangent);

    return success;
}

static bool condition_static_mesh_to_resource(Asset *asset, Resource *resource, Open_File_Result *asset_file, Open_File_Result *resource_file, Memory_Arena *arena)
{
    return true;
}


static bool condition_scene_to_resource(Asset *asset, Resource *resource, Open_File_Result *asset_file, Open_File_Result *resource_file, Memory_Arena *arena)
{
    String asset_path = asset->absolute_path;
    String asset_name = get_name(asset_path);
    String asset_parent_path = get_parent_path(asset_path);

    bool success = true;

    U8 *asset_file_data = HE_ALLOCATE_ARRAY(arena, U8, asset_file->size);
    success &= platform_read_data_from_file(asset_file, 0, asset_file_data, asset_file->size);

    cgltf_options options = {};
    options.memory.alloc_func = _cgltf_alloc;
    options.memory.free_func = _cgltf_free;

    cgltf_data *data = nullptr;

    if (cgltf_parse(&options, asset_file_data, asset_file->size, &data) != cgltf_result_success)
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
        String material_resource_relative_path = sub_string(material_resource_absloute_path, resource_system_state->resource_path.count + 1);

        resource_mutex.lock();
        U64 material_uuid = generate_uuid();
        HE_ASSERT(resource_system_state->resources.count < resource_system_state->resources.capacity);
        Resource &material_resource = append(&resource_system_state->resources);
        U32 resource_index = index_of(&resource_system_state->resources, material_resource);
        uuid_to_resource_index.emplace(material_uuid, resource_index);
        path_to_resource_index.emplace(material_resource_relative_path, resource_index);
        resource_mutex.unlock();

        material_resource.absolute_path = material_resource_absloute_path;
        material_resource.relative_path = material_resource_relative_path;
        material_resource.uuid = material_uuid;
        material_resource.asset_uuid = asset->uuid;

        String render_pass = HE_STRING_LITERAL("opaque");

        Resource_Ref shaders[] =
        {
            find_resource(HE_STRING_LITERAL("opaque_pbr_vert.hres")),
            find_resource(HE_STRING_LITERAL("opaque_pbr_frag.hres"))
        };

        init(&material_resource.resource_refs);
        init(&material_resource.children);

        for (Resource_Ref ref : shaders)
        {
            HE_ASSERT(ref.uuid != HE_MAX_U64);
            append(&material_resource.resource_refs, ref.uuid);

            auto it = uuid_to_resource_index.find(ref.uuid);
            Resource &shader_resource = resource_system_state->resources[it->second];

            platform_lock_mutex(&shader_resource.mutex);
            append(&shader_resource.children, material_resource.uuid);
            platform_unlock_mutex(&shader_resource.mutex);
        }

        Pipeline_State_Settings pipeline_state_settings =
        {
            .cull_mode = Cull_Mode::BACK,
            .front_face = Front_Face::COUNTER_CLOCKWISE,
            .fill_mode = Fill_Mode::SOLID,
            .sample_shading = true,
        };

        wait_for_resource_refs_to_condition(to_array_view(shaders));

        for (Resource_Ref ref : shaders)
        {
            Resource *resource = get_resource(ref);
            HE_ASSERT(resource->conditioned);
            HE_ASSERT(resource->type == Asset_Type::SHADER);

            bool aquired = aquire_resource(ref);
            HE_ASSERT(aquired);
        }

        wait_for_resource_refs_to_load(to_array_view(shaders));

        Shader_Struct *properties_struct = find_material_properties(to_array_view(shaders));

        Material_Property_Info *properties = HE_ALLOCATE_ARRAY(arena, Material_Property_Info, properties_struct->member_count);

        for (U32 member_index = 0; member_index < properties_struct->member_count; member_index++)
        {
            Shader_Struct_Member *member = &properties_struct->members[member_index];

            Material_Property_Info *property = &properties[member_index];
            property->is_texture_resource = ends_with(member->name, "_texture_index") && member->data_type == Shader_Data_Type::U32;
            property->is_color = ends_with(member->name, "_color") && (member->data_type == Shader_Data_Type::VECTOR3F || member->data_type == Shader_Data_Type::VECTOR4F);
        }

        auto set_property = [&](const char *property_name, Material_Property_Data data, Material_Property_Data default_data = {})
        {
            String name = HE_STRING(property_name);

            for (U32 member_index = 0; member_index < properties_struct->member_count; member_index++)
            {
                Shader_Struct_Member *member = &properties_struct->members[member_index];
                if (member->name == name)
                {
                    Material_Property_Info *property_info = &properties[member_index];
                    property_info->data = data;
                    property_info->default_data = default_data;
                    return;
                }
            }
        };

        Render_Context render_context = get_render_context();
        Renderer_State *renderer_state = render_context.renderer_state;

        set_property("albedo_texture_index",                       { .u64 = albedo_texture_uuid }, { .u32 = (U32)renderer_state->white_pixel_texture.index });
        set_property("normal_texture_index",                       { .u64 = normal_texture_uuid }, { .u32 = (U32)renderer_state->normal_pixel_texture.index });
        set_property("occlusion_roughness_metallic_texture_index", { .u64 = occlusion_roughness_metallic_texture_uuid }, { .u32 = (U32)renderer_state->white_pixel_texture.index });
        set_property("albedo_color",                               { .v3 = *(glm::vec3 *)material->pbr_metallic_roughness.base_color_factor }, { .v3 = { 1.0f, 1.0f, 1.0f } });
        set_property("roughness_factor",                           { .f32 = material->pbr_metallic_roughness.roughness_factor }, { .f32 = 1.0f });
        set_property("metallic_factor",                            { .f32 = material->pbr_metallic_roughness.metallic_factor }, { .f32 = 1.0f });
        set_property("reflectance",                                { .f32 = 0.04f }, { .f32 = 0.04f });

        create_material_resource(&material_resource, render_pass, pipeline_state_settings, properties, properties_struct->member_count);
        material_uuids[material_index] = material_uuid;
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
        String static_mesh_resource_relative_path = sub_string(static_mesh_resource_absloute_path, resource_system_state->resource_path.count + 1);

        resource_mutex.lock();
        U64 static_mesh_uuid = generate_uuid();
        Resource &static_mesh_resource = append(&resource_system_state->resources);
        U32 resource_index = index_of(&resource_system_state->resources, static_mesh_resource);
        uuid_to_resource_index.emplace(static_mesh_uuid, resource_index);
        path_to_resource_index.emplace(static_mesh_resource_relative_path, resource_index);
        resource_mutex.unlock();

        static_mesh_resource.absolute_path = static_mesh_resource_absloute_path;
        static_mesh_resource.relative_path = static_mesh_resource_relative_path;
        static_mesh_resource.uuid = static_mesh_uuid;
        static_mesh_resource.asset_uuid = asset->uuid;

        init(&static_mesh_resource.resource_refs);
        init(&static_mesh_resource.children);

        create_static_mesh_resource(&static_mesh_resource, static_mesh, data, material_uuids);
        static_mesh_uuids[static_mesh_index] = static_mesh_uuid;
    }

    U64 file_offset = 0;
    Resource_Header header = make_resource_header(Asset_Type::SCENE, resource->asset_uuid, resource->uuid);
    success &= platform_write_data_to_file(resource_file, 0, &header, sizeof(header));
    file_offset += sizeof(header);

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
        .node_data_offset = file_offset + sizeof(Scene_Resource_Info) + string_size
    };

    success &= platform_write_data_to_file(resource_file, file_offset, &info, sizeof(info));
    file_offset += sizeof(info);

    U64 node_name_data_file_offset = file_offset;

    for (U32 node_index = 0; node_index < data->nodes_count; node_index++)
    {
        cgltf_node *node = scene->nodes[node_index];

        String node_name = HE_STRING(node->name);
        U64 node_name_size = sizeof(char) * node_name.count;

        success &= platform_write_data_to_file(resource_file, file_offset, (void *)node_name.data, node_name_size);

        file_offset += node_name_size;
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

        success &= platform_write_data_to_file(resource_file, file_offset, &scene_node_info, sizeof(scene_node_info));
        file_offset += sizeof(scene_node_info);

        node_name_data_file_offset += node_name.count;
    }

    return success;
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

    U64 index_count = 0;
    U64 vertex_count = 0;

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
    // todo(amer): unloade scene resource
}

static bool load_scene_resource(Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena)
{
    U64 file_offset = sizeof(Resource_Header) + sizeof(U64) * resource->resource_refs.count;

    Render_Context render_context = get_render_context();

    bool success = true;

    Scene_Resource_Info info;
    success &= platform_read_data_from_file(open_file_result, file_offset, &info, sizeof(info));

    Scene_Node *scene = nullptr;
    Scene_Node *first_node;

    Dynamic_Array< Scene_Node > &nodes = render_context.renderer_state->nodes;
    platform_lock_mutex(&render_context.renderer_state->nodes_mutex);

    if (info.node_count == 1)
    {
        Scene_Node *node = &append(&nodes);
        scene = node;
        first_node = node;
    }
    else
    {
        scene = &nodes.data[nodes.count];
        // todo(amer): bulk append in array and dynamic array
        nodes.count += 1 + info.node_count;
        HE_ASSERT(nodes.count <= nodes.capacity);
        first_node = scene + 1;
    }

    HE_ASSERT(scene);
    HE_ASSERT(first_node);

    scene->parent = nullptr;
    scene->first_child = nullptr;
    scene->last_child = nullptr;
    scene->next_sibling = nullptr;
    scene->name = get_name(resource->relative_path);
    scene->static_mesh_uuid = HE_MAX_U64;
    scene->transform = get_identity_transform();

    platform_unlock_mutex(&render_context.renderer_state->nodes_mutex);

    for (U32 node_index = 0; node_index < info.node_count; node_index++)
    {
        Scene_Node_Info node_info;
        success &= platform_read_data_from_file(open_file_result, info.node_data_offset + sizeof(Scene_Node_Info) * node_index, &node_info, sizeof(node_info));

        U64 node_name_data_size = sizeof(char) * node_info.name_count;
        char *node_name_data = HE_ALLOCATE_ARRAY(get_general_purpose_allocator(), char, node_info.name_count + 1);
        node_name_data[node_info.name_count] = '\0';

        success &= platform_read_data_from_file(open_file_result, node_info.name_offset, node_name_data, node_name_data_size);

        String node_name = { node_name_data, node_info.name_count };
        Scene_Node *node = first_node + node_index;
        node->parent = nullptr;
        node->first_child = nullptr;
        node->last_child = nullptr;
        node->next_sibling = nullptr;

        node->name = node_name;
        node->static_mesh_uuid = node_info.static_mesh_uuid;
        node->transform = node_info.transform;

        Resource_Ref static_mesh_ref = { node_info.static_mesh_uuid };
        aquire_resource(static_mesh_ref);

        HE_ASSERT(first_node);
        Scene_Node *parent = node_info.parent_index != -1 ? first_node + node_info.parent_index : scene;
        if (node != scene)
        {
            platform_lock_mutex(&render_context.renderer_state->nodes_mutex);
            add_child(parent, node);
            platform_unlock_mutex(&render_context.renderer_state->nodes_mutex);
        }
    }

    // todo(amer): @temprary
    platform_lock_mutex(&render_context.renderer_state->nodes_mutex);
    add_child(render_context.renderer_state->root_scene_node, scene);
    platform_unlock_mutex(&render_context.renderer_state->nodes_mutex);

    resource->index = index_of(&render_context.renderer_state->nodes, scene);
    resource->ref_count++;
    resource->state = Resource_State::LOADED;
    return success;
}

static void unload_scene_resource(Resource *resource)
{
    // todo(amer): unloade scene resource
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
    Resource *resource = &resource_system_state->resources[job_data->resource_index];

    String asset_path = asset->absolute_path;
    String resource_path = resource->absolute_path;

    Asset_Conditioner &conditioner = resource_system_state->resource_type_infos[(U32)resource->type].conditioner;

    Open_File_Result asset_file_result = platform_open_file(asset_path.data, OpenFileFlag_Read);
    if (!asset_file_result.success)
    {
        HE_LOG(Resource, Trace, "failed to open asset file: %.*s\n", HE_EXPAND_STRING(asset_path));
        return Job_Result::FAILED;
    }

    HE_DEFER { platform_close_file(&asset_file_result); };

    Open_File_Result resource_file_result = platform_open_file(resource_path.data, Open_File_Flags(OpenFileFlag_Write|OpenFileFlag_Truncate));

    if (!resource_file_result.success)
    {
        HE_LOG(Resource, Trace, "failed to open resource file: %.*s\n", HE_EXPAND_STRING(resource_path));
        return Job_Result::FAILED;
    }

    HE_DEFER { platform_close_file(&resource_file_result); };

    if (!conditioner.condition_asset(asset, resource, &asset_file_result, &resource_file_result, params.arena))
    {
        HE_LOG(Resource, Trace, "failed to condition asset: %.*s\n", HE_EXPAND_STRING(asset_path));
        return Job_Result::FAILED;
    }

    HE_LOG(Resource, Trace, "successfully conditioned asset: %.*s\n", HE_EXPAND_STRING(asset_path));
    resource->conditioned = true;
    return Job_Result::SUCCEEDED;
}

// struct Save_Resource_Job_Data
// {
//     Resource *resource;
// };

// static Job_Result save_resource_job(const Job_Parameters &params)
// {
//     Save_Resource_Job_Data *job_data = (Save_Resource_Job_Data *)params.data;
//     Resource *resource = job_data->resource;
//     Resource_Conditioner &conditioner = resource_system_state->resource_type_infos[resource->type].conditioner;

//     Open_File_Result open_file_result = platform_open_file(resource->absolute_path.data, Open_File_Flags(OpenFileFlag_Write|OpenFileFlag_Truncate));
//     if (!open_file_result.success)
//     {
//         HE_LOG(Resource, Trace, "failed to open file %.*s\n", HE_EXPAND_STRING(resource->absolute_path));
//         return Job_Result::FAILED;
//     }

//     HE_DEFER { platform_close_file(&open_file_result); };
//     if (conditioner.save(resource, &open_file_result, params.temprary_memory_arena))
//     {
//         HE_LOG(Resource, Trace, "failed to save resource: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
//         return Job_Result::FAILED;
//     }

//     HE_LOG(Resource, Trace, "successfully saved resource: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
//     return Job_Result::SUCCEEDED;
// }

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

    Resource_Type_Info &info = resource_system_state->resource_type_infos[(U32)resource->type];
    bool use_allocation_group = info.loader.use_allocation_group;

    if (use_allocation_group)
    {
        Renderer_Semaphore_Descriptor semaphore_descriptor =
        {
            .initial_value = 0
        };

        resource->allocation_group.resource_name = resource->relative_path;
        resource->allocation_group.semaphore = renderer_create_semaphore(semaphore_descriptor);
        resource->allocation_group.resource_index = (S32)index_of(&resource_system_state->resources, resource);
    }

    Open_File_Result open_file_result = platform_open_file(resource->absolute_path.data, OpenFileFlag_Read);

    if (!open_file_result.success)
    {
        HE_LOG(Resource, Fetal, "failed to open resource file: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
        return Job_Result::FAILED;
    }

    HE_DEFER { platform_close_file(&open_file_result); };

    bool success = info.loader.load(&open_file_result, resource, params.arena);

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

U32 create_resource(String absolute_path, Asset_Type type, U64 asset_uuid, U64 uuid = HE_MAX_U64)
{
    Resource &resource = append(&resource_system_state->resources);
    U32 resource_index = index_of(&resource_system_state->resources, resource);
    if (uuid == HE_MAX_U64)
    {
        uuid = generate_uuid();
    }
    resource.absolute_path = absolute_path;
    resource.relative_path = sub_string(absolute_path, resource_system_state->resource_path.count + 1);
    resource.uuid = uuid;
    resource.asset_uuid = asset_uuid;
    resource.state = Resource_State::UNLOADED;
    resource.type = type;
    resource.index = -1;
    resource.conditioned = false;
    resource.generation = 0;
    resource.ref_count = 0;

    platform_create_mutex(&resource.mutex);
    init(&resource.resource_refs);
    init(&resource.children);

    HE_ASSERT(uuid_to_resource_index.find(resource.uuid) == uuid_to_resource_index.end());
    uuid_to_resource_index.emplace(resource.uuid, resource_index);
    path_to_resource_index.emplace(resource.relative_path, resource_index);
    return resource_index;
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
    String relative_path = sub_string(absolute_path, resource_system_state->resource_path.count + 1);
    String extension = get_extension(relative_path);

    if (extension == "hres")
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
        platform_read_data_from_file(&result, sizeof(Resource_Header), resource.resource_refs.data, sizeof(U64) * header.resource_ref_count);
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
        Asset &asset = append(&resource_system_state->assets);
        asset_index = index_of(&resource_system_state->assets, asset);

        asset.type = asset_type;

        asset.absolute_path = absolute_path;
        asset.relative_path = sub_string(absolute_path, resource_system_state->resource_path.count + 1);

        asset.uuid = generate_uuid();
        asset.last_write_time = platform_get_file_last_write_time(absolute_path.data);

        init(&asset.resource_refs);

        path_to_asset_index.emplace(relative_path, asset_index);
        uuid_to_asset_index.emplace(asset.uuid, asset_index);
    }
    else
    {
        asset_index = it->second;
    }

    Asset &asset = resource_system_state->assets[asset_index];
    String parent = get_parent_path(asset.absolute_path);
    String name = get_name(asset.absolute_path);
    String resource_absolute_path = format_string(arena, "%.*s/%.*s.hres", HE_EXPAND_STRING(parent), HE_EXPAND_STRING(name));

    asset.resource_absolute_path = resource_absolute_path;
    asset.resource_relative_path = sub_string(resource_absolute_path, resource_system_state->resource_path.count + 1);

    U64 last_write_time = platform_get_file_last_write_time(asset.absolute_path.data);

    if (!file_exists(resource_absolute_path))
    {
        U32 resource_index = create_resource(resource_absolute_path, asset.type, asset.uuid);
        Resource &resource = resource_system_state->resources[resource_index];
        append(&asset.resource_refs, resource.uuid);

        append(&resource_system_state->assets_to_condition, (U32)asset_index);
    }
    else if (last_write_time != asset.last_write_time)
    {
        append(&resource_system_state->assets_to_condition, (U32)asset_index);
        asset.last_write_time = last_write_time;
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
        Asset &asset = append(&resource_system_state->assets);
        U32 asset_index = index_of(&resource_system_state->assets, asset);

        asset.uuid = info->uuid;
        asset.last_write_time = info->last_write_time;
        asset.absolute_path = absolute_path;
        asset.relative_path = copy_string(relative_path, arena);

        String extension = get_extension(asset.relative_path);
        asset.type = find_asset_type_from_extension(extension);

        if (info->resource_refs_count)
        {
            init(&asset.resource_refs, info->resource_refs_count);
            copy_memory(asset.resource_refs.data, resource_refs, sizeof(U64) * info->resource_refs_count);
        }
        else
        {
            init(&asset.resource_refs);
        }

        path_to_asset_index.emplace(asset.relative_path, asset_index);
        uuid_to_asset_index.emplace(asset.uuid, asset_index);
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

    Memory_Arena *arena = get_permenent_arena();
    Free_List_Allocator *allocator = get_general_purpose_allocator();
    resource_system_state = HE_ALLOCATE(arena, Resource_System_State);

    // todo(amer): @Hack using HE_MAX_U16 at initial capacity so we can append without copying
    // because we need to hold pointers to resources.
    init(&resource_system_state->assets, 0, HE_MAX_U16);
    init(&resource_system_state->resources, 0, HE_MAX_U16);

    String working_directory = get_current_working_directory(arena);
    sanitize_path(working_directory);

    String resource_path = format_string(arena, "%.*s/%.*s", HE_EXPAND_STRING(working_directory), HE_EXPAND_STRING(resource_directory_name));
    if (!directory_exists(resource_path))
    {
        HE_LOG(Resource, Fetal, "invalid resource path: %.*s\n", HE_EXPAND_STRING(resource_path));
        return false;
    }

    resource_system_state->asset_database_path = format_string(arena, "%.*s/assets.hdb", HE_EXPAND_STRING(resource_path));

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
        Asset &asset = resource_system_state->assets[asset_index];
        auto it = path_to_resource_index.find(asset.resource_relative_path);
        HE_ASSERT(it != path_to_resource_index.end());
        Condition_Asset_Job_Data data;
        data.asset_index = asset_index;
        data.resource_index = it->second;

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
    Dynamic_Array< Job_Handle > jobs;
    init(&jobs);

    for (U64 uuid : resource->resource_refs)
    {
        auto it = uuid_to_resource_index.find(uuid);
        HE_ASSERT(it != uuid_to_resource_index.end());
        Resource *r = &resource_system_state->resources[it->second];

        platform_lock_mutex(&r->mutex);

        if (r->state == Resource_State::UNLOADED)
        {
            r->state = Resource_State::PENDING;

            Load_Resource_Job_Data data =
            {
                .resource = r
            };

            Job_Data job_data = {};
            job_data.parameters.data = &data;
            job_data.parameters.size = sizeof(data);
            job_data.proc = load_resource_job;
            Job_Handle job_handle = execute_job(job_data);
            r->job_handle = job_handle;

            append(&jobs, job_handle);
        }
        else if (r->state == Resource_State::PENDING)
        {
            append(&jobs, r->job_handle);
            r->ref_count++;
        }
        else
        {
            r->ref_count++;
        }

        platform_unlock_mutex(&r->mutex);
    }

    platform_lock_mutex(&resource->mutex);

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
        resource->job_handle = execute_job(job_data, to_array_view(jobs));
    }
    else
    {
        resource->ref_count++;
        platform_unlock_mutex(&resource->mutex);
    }

    deinit(&jobs);
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

// @Hack: we should rely on the job system
bool wait_for_resource_refs_to_condition(Array_View< Resource_Ref > resource_refs)
{
    while (true)
    {
        bool all_conditioned = true;

        for (Resource_Ref ref : resource_refs)
        {
            Resource *resource = get_resource(ref);

            if (!resource->conditioned)
            {
                all_conditioned = false;
                break;
            }
        }

        if (all_conditioned)
        {
            return true;
        }
    }

    return false;
}

// @Hack: we should rely on the job system
bool wait_for_resource_refs_to_load(Array_View< Resource_Ref > resource_refs)
{
    while (true)
    {
        bool all_loaded = true;

        for (Resource_Ref ref : resource_refs)
        {
            Resource *r = get_resource(ref);
            if (r->state == Resource_State::UNLOADED)
            {
                return false;
            }
            else if (r->state == Resource_State::PENDING)
            {
                all_loaded = false;
                break;
            }
        }

        if (all_loaded)
        {
            return true;
        }
    }

    return false;
}

bool wait_for_resource_refs_to_load(Resource *resource)
{
    while (true)
    {
        bool all_loaded = true;

        for (U64 uuid : resource->resource_refs)
        {
            Resource_Ref ref = { uuid };
            Resource *r = get_resource(ref);
            if (r->state == Resource_State::UNLOADED)
            {
                return false;
            }
            else if (r->state == Resource_State::PENDING)
            {
                all_loaded = false;
                break;
            }
        }

        if (all_loaded)
        {
            return true;
        }
    }

    return false;
}

Shader_Struct *find_material_properties(Array_View<U64> shaders)
{
    for (U64 uuid : shaders)
    {
        Resource_Ref ref = { uuid };
        Shader_Handle shader_handle = get_resource_handle_as<Shader>(ref);
        Shader *shader = renderer_get_shader(shader_handle);

        for (U32 struct_index = 0; struct_index < shader->struct_count; struct_index++)
        {
            Shader_Struct *struct_ = &shader->structs[struct_index];
            if (struct_->name == "Material_Properties")
            {
                return struct_;
            }
        }
    }

    return nullptr;
}

Shader_Struct *find_material_properties(Array_View<Resource_Ref> shaders)
{
    for (Resource_Ref ref : shaders)
    {
        Shader_Handle shader_handle = get_resource_handle_as<Shader>(ref);
        Shader *shader = renderer_get_shader(shader_handle);

        for (U32 struct_index = 0; struct_index < shader->struct_count; struct_index++)
        {
            Shader_Struct *struct_ = &shader->structs[struct_index];
            if (struct_->name == "Material_Properties")
            {
                return struct_;
            }
        }
    }

    return nullptr;
}

bool create_material_resource(Resource *resource, const String &render_pass_name, const Pipeline_State_Settings &settings, Material_Property_Info *properties, U16 property_count)
{
    resource->type = Asset_Type::MATERIAL;
    resource->state = Resource_State::UNLOADED;
    resource->ref_count = 0;
    resource->index = -1;
    resource->generation = 0;

    platform_create_mutex(&resource->mutex);

    auto it = uuid_to_asset_index.find(resource->asset_uuid);
    HE_ASSERT(it != uuid_to_asset_index.end());

    asset_mutex.lock();
    Asset &asset = resource_system_state->assets[it->second];
    append(&asset.resource_refs, resource->uuid);
    asset_mutex.unlock();

    bool success = true;

    Resource_Header header = make_resource_header(Asset_Type::MATERIAL, resource->asset_uuid, resource->uuid);
    header.resource_ref_count = resource->resource_refs.count;

    Open_File_Result open_file_result = platform_open_file(resource->absolute_path.data, Open_File_Flags(OpenFileFlag_Write|OpenFileFlag_Truncate));
    if (!open_file_result.success)
    {
        HE_LOG(Resource, Fetal, "failed to open resource file: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
        return false;
    }

    HE_DEFER { platform_close_file(&open_file_result); };

    U64 file_offset = 0;
    success &= platform_write_data_to_file(&open_file_result, file_offset, &header, sizeof(header));
    file_offset += sizeof(header);

    success &= platform_write_data_to_file(&open_file_result, file_offset, resource->resource_refs.data, sizeof(U64) * resource->resource_refs.count);
    file_offset += sizeof(U64) * resource->resource_refs.count;

    Material_Resource_Info info =
    {
        .settings = settings,
        .render_pass_name_count = render_pass_name.count,
        .render_pass_name_offset = file_offset + sizeof(Material_Resource_Info),
        .property_count = property_count,
        .property_data_offset = (S64)(file_offset + sizeof(Material_Resource_Info) + render_pass_name.count)
    };

    success &= platform_write_data_to_file(&open_file_result, file_offset, &info, sizeof(info));
    file_offset += sizeof(info);

    success &= platform_write_data_to_file(&open_file_result, file_offset, (void *)render_pass_name.data, sizeof(char) * render_pass_name.count);
    file_offset += sizeof(char) * render_pass_name.count;

    U64 property_size = sizeof(Material_Property_Info) * property_count;
    success &= platform_write_data_to_file(&open_file_result, file_offset, (void *)properties, property_size);
    file_offset += property_size;

    return success;
}

const Dynamic_Array< Resource >& get_resources()
{
    return resource_system_state->resources;
}

void reload_resources()
{
    for (U32 asset_index = 0; asset_index < resource_system_state->assets.count; asset_index++)
    {
        Asset &asset = resource_system_state->assets[asset_index];
        U64 last_write_time = platform_get_file_last_write_time(asset.absolute_path.data);
        if (last_write_time != asset.last_write_time)
        {
            asset.last_write_time = last_write_time;

            U64 resource_uuid = asset.resource_refs[0];
            auto it = uuid_to_resource_index.find(resource_uuid);
            HE_ASSERT(it != uuid_to_resource_index.end());
            U32 resource_index = it->second;

            Condition_Asset_Job_Data data;
            data.asset_index = asset_index;
            data.resource_index = resource_index;

            Job_Data job =
            {
                .parameters =
                {
                    .data = &data,
                    .size = sizeof(data)
                },
                .proc = &condition_asset_job
            };
            execute_job(job);
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