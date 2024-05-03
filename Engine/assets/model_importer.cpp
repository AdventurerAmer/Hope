#include "assets/model_importer.h"

#include "core/file_system.h"
#include "core/logging.h"
#include "core/memory.h"
#include "core/platform.h"

#include "rendering/renderer.h"

#include <ExcaliburHash/ExcaliburHash.h>

struct Model_Instance
{
    void *data;
    U32 ref_count;
};

using Model_Cache = Excalibur::HashMap< U64,  Model_Instance>;

#pragma warning(push, 0)

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#pragma warning(pop)

static Model_Cache model_cache;
static Mutex model_cache_mutex;

static void* cgltf_alloc(void *user, cgltf_size size)
{
    return allocate((Free_List_Allocator *)user, size, 16);
}

static void cgltf_free(void *user, void *ptr)
{
    deallocate((Free_List_Allocator *)user, ptr);
}

static Asset_Handle get_texture_asset_handle(String model_relative_path, const cgltf_image *image)
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    String texture_name = {};
    String parent_path = get_parent_path(model_relative_path);

    if (image->uri)
    {
        texture_name = HE_STRING(image->uri);
    }
    else if (image->name)
    {
        texture_name = HE_STRING(image->name);
    }
    else
    {
        return { .uuid = 0 };
    }

    String texture_path = format_string(scratch_memory.arena, "%.*s/%.*s", HE_EXPAND_STRING(parent_path), HE_EXPAND_STRING(texture_name));
    Asset_Handle asset_handle = import_asset(texture_path);
    return asset_handle;
};

static String get_embedded_asset_path(cgltf_data *model_data, cgltf_material *material, Asset_Handle asset_handle, Memory_Arena *arena)
{
    U64 material_index = material - model_data->materials;
    String material_name = {};

    if (material->name)
    {
        material_name = format_string(arena, "%.*s.hamaterial", HE_EXPAND_STRING(HE_STRING(material->name)));
    }
    else
    {
        material_name = format_string(arena, "material_%d.hamaterial", material_index);
    }

    String material_path = format_embedded_asset(asset_handle, material_index, material_name, arena);
    sanitize_path(material_path);
    return material_path;
}

static String get_embedded_asset_path(cgltf_data *model_data, cgltf_mesh *mesh, Asset_Handle asset_handle, Memory_Arena *arena)
{
    U64 static_mesh_index = mesh - model_data->meshes;
    String static_mesh_name = {};

    if (mesh->name)
    {
        static_mesh_name = format_string(arena, "%.*s.hastaticmesh", HE_EXPAND_STRING(HE_STRING(mesh->name)));
    }
    else
    {
        static_mesh_name = format_string(arena, "static_mesh_%d.hastaticmesh", static_mesh_index);
    }

    String material_path = format_embedded_asset(asset_handle, static_mesh_index, static_mesh_name, arena);
    sanitize_path(material_path);
    return material_path;
}

static cgltf_data *aquire_model_from_cache(U64 asset_uuid, String path)
{
    cgltf_data *result = nullptr;

    Free_List_Allocator *allocator = get_general_purpose_allocator();

    platform_lock_mutex(&model_cache_mutex);

    auto it = model_cache.find(asset_uuid);
    if (it == model_cache.iend())
    {
        Read_Entire_File_Result file_result = read_entire_file(path, to_allocator(allocator));

        cgltf_options options = {};
        options.memory.user_data = allocator;
        options.memory.alloc_func = cgltf_alloc;
        options.memory.free_func = cgltf_free;

        if (cgltf_parse(&options, file_result.data, file_result.size, &result) != cgltf_result_success)
        {
            HE_LOG(Resource, Fetal, "load_model -- cgltf -- unable to parse asset file: %.*s\n", HE_EXPAND_STRING(path));
            return {};
        }

        if (cgltf_load_buffers(&options, result, path.data) != cgltf_result_success)
        {
            HE_LOG(Resource, Fetal, "load_model -- cgltf -- unable to load buffers from asset file: %.*s\n", HE_EXPAND_STRING(path));
            return {};
        }

        model_cache.emplace(asset_uuid, Model_Instance { .data = (void *)result, .ref_count = 1 });
    }
    else
    {
        Model_Instance &instance = it.value();
        result = (cgltf_data *)instance.data;
        instance.ref_count++;
    }

    platform_unlock_mutex(&model_cache_mutex);

    return result;
}

static void release_model_from_cache(U64 asset_uuid)
{
    platform_lock_mutex(&model_cache_mutex);

    auto it = model_cache.find(asset_uuid);
    HE_ASSERT(it != model_cache.iend());
    Model_Instance &instance = it.value();
    HE_ASSERT(instance.ref_count);
    instance.ref_count--;

    if (instance.ref_count == 0)
    {
        model_cache.erase(it);
    }

    platform_unlock_mutex(&model_cache_mutex);
}

void on_import_model(Asset_Handle asset_handle)
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
    Free_List_Allocator *allocator = get_general_purpose_allocator();

    const Asset_Registry_Entry &entry = get_asset_registry_entry(asset_handle);
    String path = format_string(scratch_memory.arena, "%.*s/%.*s", HE_EXPAND_STRING(get_asset_path()), HE_EXPAND_STRING(entry.path));

    cgltf_data *model_data = aquire_model_from_cache(asset_handle.uuid, path);
    HE_ASSERT(model_data);

    HE_DEFER
    {
       release_model_from_cache(asset_handle.uuid);
    };

    Asset_Handle opaque_pbr_shader_asset = import_asset(HE_STRING_LITERAL("opaque_pbr.glsl"));

    for (U32 material_index = 0; material_index < model_data->materials_count; material_index++)
    {
        cgltf_material *material = &model_data->materials[material_index];
        String material_path = get_embedded_asset_path(model_data, material, asset_handle, scratch_memory.arena);
        Asset_Handle asset = import_asset(material_path);
        set_parent(asset, opaque_pbr_shader_asset);
    }

    for (U32 static_mesh_index = 0; static_mesh_index < model_data->meshes_count; static_mesh_index++)
    {
        cgltf_mesh *static_mesh = &model_data->meshes[static_mesh_index];
        String static_mesh_path = get_embedded_asset_path(model_data, static_mesh, asset_handle, scratch_memory.arena);
        Asset_Handle asset = import_asset(static_mesh_path);
    }
}

Load_Asset_Result load_model(String path, const Embeded_Asset_Params *params)
{
    if (!model_cache_mutex.platform_mutex_state)
    {
        platform_create_mutex(&model_cache_mutex);
    }

    Free_List_Allocator *allocator = get_general_purpose_allocator();
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    String asset_path = get_asset_path();
    String relative_path = sub_string(path, asset_path.count + 1);

    Asset_Handle asset_handle = get_asset_handle(relative_path);

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;

    cgltf_data *model_data = aquire_model_from_cache(asset_handle.uuid, path);
    HE_ASSERT(model_data);

    HE_DEFER
    {
       release_model_from_cache(asset_handle.uuid);
    };

    bool embeded_material = false;
    bool embeded_static_mesh = false;

    if (params)
    {
        const Asset_Info *info = get_asset_info(params->type_info_index);
        embeded_material = info->name == HE_STRING_LITERAL("material");
        embeded_static_mesh = info->name == HE_STRING_LITERAL("static_mesh");
    }

    if (embeded_material)
    {
        Asset_Handle opaque_pbr_shader_asset = import_asset(HE_STRING_LITERAL("opaque_pbr.glsl"));
        if (!is_asset_loaded(opaque_pbr_shader_asset))
        {
            HE_LOG(Resource, Fetal, "load_model -- cgltf -- unable to load model asset file: %.*s --> parent asset failed to load\n", HE_EXPAND_STRING(path));
            return {};
        }

        Shader_Handle opaque_pbr_shader = get_asset_handle_as<Shader>(opaque_pbr_shader_asset);

        U32 material_index = u64_to_u32(params->data_id);
        cgltf_material *material = &model_data->materials[material_index];
        
        String material_path = get_embedded_asset_path(model_data, material, asset_handle, scratch_memory.arena);
        String material_name = get_name(material_path);

        Asset_Handle albedo_texture = {};
        Asset_Handle roughness_metallic_texture = {};
        Asset_Handle occlusion_texture = {};
        Asset_Handle normal_texture = {};

        if (material->has_pbr_metallic_roughness)
        {
            if (material->pbr_metallic_roughness.base_color_texture.texture)
            {
                const cgltf_image *image = material->pbr_metallic_roughness.base_color_texture.texture->image;
                albedo_texture = get_texture_asset_handle(relative_path, image);
            }

            if (material->pbr_metallic_roughness.metallic_roughness_texture.texture)
            {
                const cgltf_image *image = material->pbr_metallic_roughness.metallic_roughness_texture.texture->image;
                roughness_metallic_texture = get_texture_asset_handle(relative_path, image);
            }
        }

        if (material->normal_texture.texture)
        {
            const cgltf_image *image = material->normal_texture.texture->image;
            normal_texture = get_texture_asset_handle(relative_path, image);
        }

        if (material->occlusion_texture.texture)
        {
            const cgltf_image *image = material->occlusion_texture.texture->image;
            occlusion_texture = get_texture_asset_handle(relative_path, image);
        }

        Pipeline_State_Settings settings =
        {
            .cull_mode = material->double_sided ? Cull_Mode::NONE : Cull_Mode::BACK,
            .front_face = Front_Face::COUNTER_CLOCKWISE,
            .fill_mode = Fill_Mode::SOLID,
            .depth_testing = true,
            .sample_shading = true,
        };

        float alpha_cutoff = 0.0f;

        Material_Type material_type = Material_Type::OPAQUE;

        if (material->alpha_mode == cgltf_alpha_mode::cgltf_alpha_mode_mask)
        {
            alpha_cutoff = material->alpha_cutoff;
            material_type = Material_Type::ALPHA_CUTOFF;
        }
        else if (material->alpha_mode == cgltf_alpha_mode::cgltf_alpha_mode_blend)
        {
            material_type = Material_Type::TRANSPARENT;
        }

        Material_Descriptor material_descriptor =
        {
            .name = material_name,
            .type =  material_type,
            .shader = opaque_pbr_shader,
            .settings = settings,
        };

        Material_Handle material_handle = renderer_create_material(material_descriptor);

        F32 reflectance = 0.04f;
        if (material->has_ior)
        {
            F32 ior = material->ior.ior;
            F32 f = (ior - 1) / (ior + 1);
            reflectance = f * f;
        }

        set_property(material_handle, HE_STRING_LITERAL("albedo_texture"), { .u64 = albedo_texture.uuid });
        set_property(material_handle, HE_STRING_LITERAL("albedo_color"), { .v4f = *(glm::vec4 *)material->pbr_metallic_roughness.base_color_factor });
        set_property(material_handle, HE_STRING_LITERAL("normal_texture"), { .u64 = normal_texture.uuid });
        set_property(material_handle, HE_STRING_LITERAL("roughness_metallic_texture"), { .u64 = roughness_metallic_texture.uuid });
        set_property(material_handle, HE_STRING_LITERAL("roughness_factor"), { .f32 = material->pbr_metallic_roughness.roughness_factor });
        set_property(material_handle, HE_STRING_LITERAL("metallic_factor"), { .f32 = material->pbr_metallic_roughness.metallic_factor });
        set_property(material_handle, HE_STRING_LITERAL("occlusion_texture"), { .u64 = occlusion_texture.uuid });
        set_property(material_handle, HE_STRING_LITERAL("alpha_cutoff"), { .f32 = alpha_cutoff });
        set_property(material_handle, HE_STRING_LITERAL("reflectance"), { .f32 = reflectance });

        return { .success = true, .index = material_handle.index, .generation = material_handle.generation };
    }

    if (embeded_static_mesh)
    {
        U32 static_mesh_index = u64_to_u32(params->data_id);
        cgltf_mesh *static_mesh = &model_data->meshes[static_mesh_index];

        String static_mesh_path = get_embedded_asset_path(model_data, static_mesh, asset_handle, scratch_memory.arena);
        String static_mesh_name = get_name(static_mesh_path);

        U64 total_vertex_count = 0;
        U64 total_index_count = 0;

        Dynamic_Array< Sub_Mesh > sub_meshes;
        init(&sub_meshes, u64_to_u32(static_mesh->primitives_count), u64_to_u32(static_mesh->primitives_count));

        for (U32 sub_mesh_index = 0; sub_mesh_index < (U32)static_mesh->primitives_count; sub_mesh_index++)
        {
            cgltf_primitive *primitive = &static_mesh->primitives[sub_mesh_index];
            HE_ASSERT(primitive->type == cgltf_primitive_type_triangles);

            HE_ASSERT(primitive->indices->type == cgltf_type_scalar);
            HE_ASSERT(primitive->indices->component_type == cgltf_component_type_r_16u);
            HE_ASSERT(primitive->indices->stride == sizeof(U16));

            sub_meshes[sub_mesh_index].vertex_offset = u64_to_u32(total_vertex_count);
            sub_meshes[sub_mesh_index].index_offset = u64_to_u32(total_index_count);

            total_index_count += primitive->indices->count;
            sub_meshes[sub_mesh_index].index_count = u64_to_u32(primitive->indices->count);

            if (primitive->material)
            {
                cgltf_material *material = primitive->material;
                String material_path = get_embedded_asset_path(model_data, material, asset_handle, scratch_memory.arena);
                sub_meshes[sub_mesh_index].material_asset = get_asset_handle(material_path).uuid;
            }

            for (U32 attribute_index = 0; attribute_index < primitive->attributes_count; attribute_index++)
            {
                cgltf_attribute *attribute = &primitive->attributes[attribute_index];
                switch (attribute->type)
                {
                    case cgltf_attribute_type_position:
                    {
                        HE_ASSERT(attribute->data->type == cgltf_type_vec3);
                        HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);
                        U64 stride = attribute->data->stride;
                        HE_ASSERT(stride == sizeof(glm::vec3));
                        total_vertex_count += attribute->data->count;
                        sub_meshes[sub_mesh_index].vertex_count = u64_to_u32(attribute->data->count);
                    } break;

                    case cgltf_attribute_type_normal:
                    {
                        HE_ASSERT(attribute->data->type == cgltf_type_vec3);
                        HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                        U64 stride = attribute->data->stride;
                        HE_ASSERT(stride == sizeof(glm::vec3));
                    } break;

                    case cgltf_attribute_type_texcoord:
                    {
                        HE_ASSERT(attribute->data->type == cgltf_type_vec2);
                        HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                        U64 stride = attribute->data->stride;
                        HE_ASSERT(stride == sizeof(glm::vec2));
                    } break;

                    case cgltf_attribute_type_tangent:
                    {
                        HE_ASSERT(attribute->data->type == cgltf_type_vec4);
                        HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                        U64 stride = attribute->data->stride;
                        HE_ASSERT(stride == sizeof(glm::vec4));
                    } break;
                }
            }
        }

        U64 index_size = sizeof(U16) * total_index_count;
        U64 vertex_size = (sizeof(glm::vec3) + sizeof(glm::vec3) + sizeof(glm::vec2) + sizeof(glm::vec4)) * total_vertex_count;
        U64 total_size = index_size + vertex_size;
        U8 *static_mesh_data = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, U8, total_size);

        U16 *indices = (U16 *)static_mesh_data;

        U8 *vertex_data = static_mesh_data + index_size;
        glm::vec3 *positions = (glm::vec3 *)vertex_data;
        glm::vec3 *normals = (glm::vec3 *)(vertex_data + sizeof(glm::vec3) * total_vertex_count);
        glm::vec2 *uvs = (glm::vec2 *)(vertex_data + (sizeof(glm::vec3) + sizeof(glm::vec3)) * total_vertex_count);
        glm::vec4 *tangents = (glm::vec4 *)(vertex_data + (sizeof(glm::vec3) + sizeof(glm::vec2) + sizeof(glm::vec3)) * total_vertex_count);

        for (U32 sub_mesh_index = 0; sub_mesh_index < (U32)static_mesh->primitives_count; sub_mesh_index++)
        {
            cgltf_primitive *primitive = &static_mesh->primitives[sub_mesh_index];

            const auto *accessor = primitive->indices;
            const auto *view = accessor->buffer_view;
            U8 *data = (U8 *)view->buffer->data + view->offset + accessor->offset;
            copy_memory(indices + sub_meshes[sub_mesh_index].index_offset, data, primitive->indices->count * sizeof(U16));

            for (U32 attribute_index = 0; attribute_index < primitive->attributes_count; attribute_index++)
            {
                cgltf_attribute *attribute = &primitive->attributes[attribute_index];
                switch (attribute->type)
                {
                    case cgltf_attribute_type_position:
                    {
                        const auto *accessor = attribute->data;
                        const auto *view = accessor->buffer_view;
                        U8 *data_ptr = (U8 *)view->buffer->data;
                        U64 element_size = attribute->data->stride;
                        U64 element_count = attribute->data->count;
                        U8 *data = data_ptr + view->offset + accessor->offset;
                        copy_memory(positions + sub_meshes[sub_mesh_index].vertex_offset, data, element_size * element_count);
                    } break;

                    case cgltf_attribute_type_normal:
                    {
                        const auto *accessor = attribute->data;
                        const auto *view = accessor->buffer_view;
                        U8 *data_ptr = (U8 *)view->buffer->data;
                        U64 element_size = attribute->data->stride;
                        U64 element_count = attribute->data->count;
                        U8 *data = data_ptr + view->offset + accessor->offset;
                        copy_memory(normals + sub_meshes[sub_mesh_index].vertex_offset, data, element_size * element_count);
                    } break;


                    case cgltf_attribute_type_texcoord:
                    {
                        const auto *accessor = attribute->data;
                        const auto *view = accessor->buffer_view;
                        U8 *data_ptr = (U8 *)view->buffer->data;
                        U64 element_size = attribute->data->stride;
                        U64 element_count = attribute->data->count;
                        U8 *data = data_ptr + view->offset + accessor->offset;
                        copy_memory(uvs + sub_meshes[sub_mesh_index].vertex_offset, data, element_size * element_count);
                    } break;

                    case cgltf_attribute_type_tangent:
                    {
                        const auto *accessor = attribute->data;
                        const auto *view = accessor->buffer_view;
                        U8 *data_ptr = (U8 *)view->buffer->data;
                        U64 element_size = attribute->data->stride;
                        U64 element_count = attribute->data->count;
                        U8 *data = data_ptr + view->offset + accessor->offset;
                        copy_memory(tangents + sub_meshes[sub_mesh_index].vertex_offset, data, element_size * element_count);
                    } break;
                }
            }
        }

        void *data_array[] = { static_mesh_data };

        Static_Mesh_Descriptor static_mesh_descriptor =
        {
            .name = copy_string(static_mesh_name, to_allocator(allocator)),
            .data_array = to_array_view(data_array),

            .indices = indices,
            .index_count = u64_to_u32(total_index_count),

            .vertex_count = u64_to_u32(total_vertex_count),
            .positions = positions,
            .normals = normals,
            .uvs = uvs,
            .tangents = tangents,

            .sub_meshes = sub_meshes
        };

        Static_Mesh_Handle static_mesh_handle = renderer_create_static_mesh(static_mesh_descriptor);
        return { .success = true, .index = static_mesh_handle.index, .generation = static_mesh_handle.generation };
    }

    cgltf_scene *scene = &model_data->scenes[0];

    Model *model = HE_ALLOCATE(allocator, Model);
    model->name = copy_string(get_name(path), to_allocator(allocator));

    Scene_Node *nodes = HE_ALLOCATE_ARRAY(allocator, Scene_Node, scene->nodes_count);

    model->node_count = u64_to_u32(scene->nodes_count);
    model->nodes = nodes;

    for (U32 node_index = 0; node_index < scene->nodes_count; node_index++)
    {
        cgltf_node *node = scene->nodes[node_index];

        String node_name = {};

        if (node->name)
        {
            node_name = HE_STRING(node->name);
        }
        else
        {
            node_name = format_string(scratch_memory.arena, "node_%u", node_index);
        }

        S32 parent_index = node->parent ? (S32)(node->parent - model_data->nodes) : -1;

        glm::quat rotation = { node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2] };

        Transform transform =
        {
            .position = *(glm::vec3*)&node->translation,
            .rotation = rotation,
            .euler_angles = glm::degrees(glm::eulerAngles(rotation)),
            .scale = *(glm::vec3*)&node->scale
        };

        S64 mesh_index = (S64)(node->mesh - model_data->meshes);
        HE_ASSERT(mesh_index >= 0 && (S64)model_data->meshes_count);

        Scene_Node *scene_node = &nodes[node_index];
        scene_node->name = copy_string(node_name, to_allocator(allocator));
        scene_node->transform = transform;
        scene_node->parent_index = parent_index;

        if (node->mesh)
        {
            cgltf_mesh *static_mesh = node->mesh;
            String static_mesh_path = get_embedded_asset_path(model_data, static_mesh, asset_handle, scratch_memory.arena);
            scene_node->has_mesh = true;
            Static_Mesh_Component *mesh_comp = &scene_node->mesh;
            mesh_comp->static_mesh_asset = get_asset_handle(static_mesh_path).uuid;
            U32 material_count = u64_to_u32(static_mesh->primitives_count);
            init(&mesh_comp->materials, material_count, material_count);

            for (U32 i = 0; i < material_count; i++)
            {
                cgltf_primitive *primitive = &static_mesh->primitives[i];
                if (primitive->material)
                {
                    cgltf_material *material = primitive->material;
                    String material_path = get_embedded_asset_path(model_data, material, asset_handle, scratch_memory.arena);
                    mesh_comp->materials[i] = get_asset_handle(material_path).uuid;
                }
                else
                {
                    mesh_comp->materials[i] = 0;
                }
            }
        }
    }

    return { .success = true, .data = model, .size = sizeof(Model) };
}

void unload_model(Load_Asset_Result load_result)
{
    HE_ASSERT(sizeof(Model) == load_result.size);

    Free_List_Allocator *allocator = get_general_purpose_allocator();
    Model *model = (Model *)load_result.data;
    deallocate(allocator, (void *)model->name.data);

    for (U32 i = 0; i < model->node_count; i++)
    {
        Scene_Node *node = &model->nodes[i];
        deallocate(allocator, (void *)node->name.data);
    }

    deallocate(allocator, (void *)model->nodes);
    deallocate(allocator, model);
}