#pragma warning(push, 0)
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#pragma warning(pop)

#include "rendering/renderer.h"
#include "core/platform.h"

#ifdef HE_RHI_VULKAN
#include "rendering/vulkan/vulkan_renderer.h"
#endif

// todo(amer): to be removed...
#include <queue>
#include <filesystem>
namespace fs = std::filesystem;

bool request_renderer(RenderingAPI rendering_api,
                      Renderer *renderer)
{
    bool result = true;

    switch (rendering_api)
    {
#ifdef HE_RHI_VULKAN
        case RenderingAPI_Vulkan:
        {
            renderer->init = &vulkan_renderer_init;
            renderer->deinit = &vulkan_renderer_deinit;
            renderer->wait_for_gpu_to_finish_all_work = &vulkan_renderer_wait_for_gpu_to_finish_all_work;
            renderer->on_resize = &vulkan_renderer_on_resize;
            renderer->create_texture = &vulkan_renderer_create_texture;
            renderer->destroy_texture = &vulkan_renderer_destroy_texture;
            renderer->create_static_mesh = &vulkan_renderer_create_static_mesh;
            renderer->destroy_static_mesh = &vulkan_renderer_destroy_static_mesh;
            renderer->create_material = &vulkan_renderer_create_material;
            renderer->destroy_material = &vulkan_renderer_destroy_material;
            renderer->begin_frame = &vulkan_renderer_begin_frame;
            renderer->submit_static_mesh = &vulkan_renderer_submit_static_mesh;
            renderer->end_frame = &vulkan_renderer_end_frame;
        } break;
#endif

        default:
        {
            result = false;
        } break;
    }

    return result;
}

bool init_renderer_state(Renderer_State *renderer_state, struct Memory_Arena *arena)
{
    // note(amer): right now we are getting sizes should we even care about alignment ?
    Assert(renderer_state->texture_bundle_size);
    Assert(renderer_state->material_bundle_size);
    Assert(renderer_state->static_mesh_bundle_size);
    renderer_state->textures      = AllocateArray(arena, U8, renderer_state->texture_bundle_size * MAX_TEXTURE_COUNT);
    renderer_state->materials     = AllocateArray(arena, U8, renderer_state->material_bundle_size * MAX_MATERIAL_COUNT);
    renderer_state->static_meshes = AllocateArray(arena, U8, renderer_state->static_mesh_bundle_size * MAX_STATIC_MESH_COUNT);

    renderer_state->scene_nodes   = AllocateArray(arena, Scene_Node, MAX_SCENE_NODE_COUNT);
    return true;
}

Scene_Node*
add_child_scene_node(Renderer_State *renderer_state,
                     Scene_Node *parent)
{
    Assert(renderer_state->scene_node_count < MAX_SCENE_NODE_COUNT);
    Assert(parent);

    Scene_Node *node = &renderer_state->scene_nodes[renderer_state->scene_node_count++];
    node->parent = parent;

    if (parent->last_child)
    {
        parent->last_child->next_sibling = node;
        parent->last_child = node;
    }
    else
    {
        parent->first_child = parent->last_child = node;
    }

    return node;
}

// note(amer): https://github.com/deccer/CMake-Glfw-OpenGL-Template/blob/main/src/Project/ProjectApplication.cpp
// thanks to this giga chad for the example
Scene_Node *load_model(const char *path, Renderer *renderer,
                       Renderer_State *renderer_state, Memory_Arena *arena)
{
    Scoped_Temprary_Memory_Arena temp_arena(arena);

    Read_Entire_File_Result result =
        platform_begin_read_entire_file(path);

    if (!result.success)
    {
        return nullptr;
    }

    U8 *buffer = AllocateArray(&temp_arena, U8, result.size);
    platform_end_read_entire_file(&result, buffer);

    U64 path_length = strlen(path);
    U64 path_without_file_name_length = 0;

    for (U64 char_index = path_length - 1; char_index >= 0; char_index--)
    {
        if (path[char_index] == '\\' || path[char_index] == '/')
        {
            path_without_file_name_length = char_index;
            break;
        }
    }

    cgltf_options options = {};
    cgltf_data *model_data = nullptr;

    if (cgltf_parse(&options, buffer, result.size, &model_data) != cgltf_result_success)
    {
        return nullptr;
    }

    if (cgltf_load_buffers(&options, model_data, path) != cgltf_result_success)
    {
        return nullptr;
    }

    for (U32 material_index = 0; material_index < model_data->materials_count; material_index++)
    {
        cgltf_material *material = &model_data->materials[material_index];
        U64 material_hash = (U64)material;

        if (material->has_pbr_metallic_roughness && material->pbr_metallic_roughness.base_color_texture.texture)
        {
            Assert(renderer_state->material_count < MAX_MATERIAL_COUNT);
            Material *renderer_material = allocate_material(renderer_state);
            if (material->name)
            {
                renderer_material->name_length = u64_to_u32(strlen(material->name));
                Assert(renderer_material->name_length <= (MAX_MATERIAL_NAME - 1));
                strncpy(renderer_material->name, material->name, renderer_material->name_length);
            }
            renderer_material->hash = material_hash;

            const cgltf_image *image = material->pbr_metallic_roughness.base_color_texture.texture->image;

            Texture *albedo = nullptr;
            char albdeo_texture_path[MAX_TEXTURE_NAME];

            if (material->pbr_metallic_roughness.base_color_texture.texture->image->uri)
            {
                char *uri = material->pbr_metallic_roughness.base_color_texture.texture->image->uri;
                sprintf(albdeo_texture_path, "%.*s/%s",
                        u64_to_u32(path_without_file_name_length),
                        path, uri);
            }
            else
            {
                const char *extension_to_append = "";

                char *name = material->pbr_metallic_roughness.base_color_texture.texture->image->name;
                U32 name_length = u64_to_u32(strlen(name));
                Assert(name_length <= (MAX_TEXTURE_NAME - 1));

                S32 last_dot_index = -1;
                for (U32 char_index = 0; char_index < name_length; char_index++)
                {
                    if (name[name_length - char_index - 1] == '.')
                    {
                        last_dot_index = char_index;
                        break;
                    }
                }

                // todo(amer): string utils
                char *extension = name + last_dot_index;
                if (strcmp(extension, ".png") != 0 &&
                    strcmp(extension, ".jpg") != 0)
                {
                    if (strcmp(image->mime_type, "image/png") == 0)
                    {
                        extension_to_append = ".png";
                    }
                    else if (strcmp(image->mime_type, "image/jpg") == 0)
                    {
                        extension_to_append = ".jpg";
                    }
                }
                sprintf(albdeo_texture_path, "%.*s/%.*s%s",
                        u64_to_u32(path_without_file_name_length), path,
                        name_length, name, extension_to_append);
            }

            U32 albdeo_texture_path_length = u64_to_u32(strlen(albdeo_texture_path));

            S32 albedo_texture_index = find_texture(renderer_state,
                                                    albdeo_texture_path,
                                                    albdeo_texture_path_length);
            if (albedo_texture_index == -1)
            {
                Assert(renderer_state->texture_count < MAX_TEXTURE_COUNT);
                albedo = allocate_texture(renderer_state);
                strcpy(albedo->name, albdeo_texture_path);
                albedo->name_length = albdeo_texture_path_length;

                S32 albedo_texture_width;
                S32 albedo_texture_height;
                S32 albedo_texture_channels;

                stbi_uc *albedo_pixels = nullptr;

                if (!fs::exists(fs::path(albdeo_texture_path)))
                {
                    const auto *view = image->buffer_view;
                    U8 *data_ptr = (U8*)view->buffer->data;
                    U8 *image_data = data_ptr + view->offset;

                    albedo_pixels = stbi_load_from_memory(image_data, u64_to_u32(view->size),
                                                          &albedo_texture_width, &albedo_texture_height,
                                                          &albedo_texture_channels, STBI_rgb_alpha);
                }
                else
                {
                    albedo_pixels = stbi_load(albdeo_texture_path,
                                              &albedo_texture_width, &albedo_texture_height,
                                              &albedo_texture_channels, STBI_rgb_alpha);
                }

                Assert(albedo_pixels);
                bool mipmapping = true;
                bool created = renderer->create_texture(albedo,
                                                        albedo_texture_width,
                                                        albedo_texture_height,
                                                        albedo_pixels, TextureFormat_RGBA, mipmapping);
                Assert(created);

                stbi_image_free(albedo_pixels);
            }
            else
            {
                albedo = get_texture(renderer_state, albedo_texture_index);
            }

            renderer->create_material(renderer_material, albedo);
        }
    }

    U32 position_count = 0;
    glm::vec3 *positions = nullptr;

    U32 normal_count = 0;
    glm::vec3 *normals = nullptr;

    U32 uv_count = 0;
    glm::vec2 *uvs = nullptr;

    U32 index_count = 0;
    U16 *indices = nullptr;

    Scene_Node *root_scene_node = &renderer_state->scene_nodes[renderer_state->scene_node_count++];
    root_scene_node->parent = nullptr;
    root_scene_node->transform = glm::mat4(1.0f);

    struct Scene_Node_Bundle
    {
        cgltf_node *cgltf_node;
        Scene_Node *node;
    };

    for (U32 node_index = 0; node_index < model_data->nodes_count; node_index++)
    {
        std::queue<Scene_Node_Bundle> nodes;
        nodes.push({ &model_data->nodes[node_index], add_child_scene_node(renderer_state, root_scene_node) });
        while (!nodes.empty())
        {
            Scene_Node_Bundle &node_bundle = nodes.front();
            cgltf_node *node = node_bundle.cgltf_node;
            Scene_Node *scene_node = node_bundle.node;
            nodes.pop();

            cgltf_node_transform_world(node, glm::value_ptr(scene_node->transform));

            if (node->mesh)
            {
                scene_node->start_mesh_index = renderer_state->static_mesh_count;
                scene_node->static_mesh_count += u64_to_u32(node->mesh->primitives_count);

                for (U32 primitive_index = 0; primitive_index < node->mesh->primitives_count; primitive_index++)
                {
                    cgltf_primitive *primitive = &node->mesh->primitives[primitive_index];
                    Assert(primitive->material);
                    cgltf_material *material = primitive->material;

                    U64 material_hash = (U64)material;
                    S32 material_index = find_material(renderer_state, material_hash);
                    Assert(material_index != -1);

                    Static_Mesh *static_mesh = allocate_static_mesh(renderer_state);
                    static_mesh->material = get_material(renderer_state, material_index);

                    Assert(primitive->type == cgltf_primitive_type_triangles);

                    for (U32 attribute_index = 0; attribute_index < primitive->attributes_count; attribute_index++)
                    {
                        cgltf_attribute *attribute = &primitive->attributes[attribute_index];
                        Assert(attribute->type != cgltf_attribute_type_invalid);

                        const auto *accessor = attribute->data;
                        const auto *view = accessor->buffer_view;
                        U8 *data_ptr = (U8*)view->buffer->data;

                        switch (attribute->type)
                        {
                            case cgltf_attribute_type_position:
                            {
                                Assert(attribute->data->type == cgltf_type_vec3);
                                Assert(attribute->data->component_type == cgltf_component_type_r_32f);

                                position_count = u64_to_u32(attribute->data->count);
                                U64 stride = attribute->data->stride;
                                Assert(stride == sizeof(glm::vec3));
                                positions = (glm::vec3*)(data_ptr + view->offset + accessor->offset);
                            } break;

                            case cgltf_attribute_type_normal:
                            {
                                Assert(attribute->data->type == cgltf_type_vec3);
                                Assert(attribute->data->component_type == cgltf_component_type_r_32f);

                                normal_count = u64_to_u32(attribute->data->count);
                                U64 stride = attribute->data->stride;
                                Assert(stride == sizeof(glm::vec3));
                                normals = (glm::vec3*)(data_ptr + view->offset + accessor->offset);
                            } break;

                            case cgltf_attribute_type_texcoord:
                            {
                                Assert(attribute->data->type == cgltf_type_vec2);
                                Assert(attribute->data->component_type == cgltf_component_type_r_32f);

                                uv_count = u64_to_u32(attribute->data->count);
                                U64 stride = attribute->data->stride;
                                Assert(stride == sizeof(glm::vec2));
                                uvs = (glm::vec2*)(data_ptr + view->offset + accessor->offset);
                            } break;
                        }
                    }

                    // note(amer): we only support u16 indices for now.
                    Assert(primitive->indices->type == cgltf_type_scalar);
                    Assert(primitive->indices->component_type == cgltf_component_type_r_16u);
                    Assert(primitive->indices->stride == sizeof(U16));

                    index_count = u64_to_u32(primitive->indices->count);
                    const auto *accessor = primitive->indices;
                    const auto *view = accessor->buffer_view;
                    U8 *data_ptr = (U8*)view->buffer->data;
                    indices = (U16*)(data_ptr + view->offset + accessor->offset);
                    Assert(position_count == normal_count);
                    Assert(position_count == uv_count);

                    U32 vertex_count = position_count;
                    Vertex* vertices = AllocateArray(&temp_arena, Vertex, vertex_count);

                    for (U32 vertex_index = 0; vertex_index < vertex_count; vertex_index++)
                    {
                        Vertex* vertex = &vertices[vertex_index];
                        vertex->position = positions[vertex_index];
                        vertex->normal = normals[vertex_index];
                        vertex->uv = uvs[vertex_index];
                    }

                    bool created = renderer->create_static_mesh(static_mesh, vertices, vertex_count,
                                                                indices, index_count);
                    Assert(created);
                }
            }

            for (U32 child_node_index = 0;
                 child_node_index < node->children_count;
                 child_node_index++)
            {
                nodes.push({ node->children[child_node_index], add_child_scene_node(renderer_state, scene_node) });
            }
        }
    }

    cgltf_free(model_data);
    return root_scene_node;
}

void render_scene_node(Renderer *renderer, Renderer_State *renderer_state, Scene_Node *scene_node, glm::mat4 parent_transform)
{
    glm::mat4 transform = parent_transform * scene_node->transform;

    for (U32 static_mesh_index = 0;
         static_mesh_index < scene_node->static_mesh_count;
         static_mesh_index++)
    {
        Static_Mesh *static_mesh = get_static_mesh(renderer_state, scene_node->start_mesh_index + static_mesh_index);
        renderer->submit_static_mesh(renderer_state, static_mesh, transform);
    }

    for (Scene_Node *node = scene_node->first_child; node; node = node->next_sibling)
    {
        render_scene_node(renderer, renderer_state, node, transform);
    }
}


Texture *allocate_texture(Renderer_State *renderer_state)
{
    Assert(renderer_state->texture_count < MAX_TEXTURE_COUNT);
    U32 texture_index = renderer_state->texture_count++;
    return get_texture(renderer_state, texture_index);
}

Material *allocate_material(Renderer_State *renderer_state)
{
    Assert(renderer_state->material_count < MAX_MATERIAL_COUNT);
    U32 material_index = renderer_state->material_count++;
    return get_material(renderer_state, material_index);
}

Static_Mesh *allocate_static_mesh(Renderer_State *renderer_state)
{
    Assert(renderer_state->static_mesh_count < MAX_STATIC_MESH_COUNT);
    U32 static_mesh_index = renderer_state->static_mesh_count++;
    return get_static_mesh(renderer_state, static_mesh_index);
}

U32 index_of(Renderer_State *renderer_state, Texture *texture)
{
    U64 offset = (U8 *)texture - renderer_state->textures;
    Assert(offset >= 0);
    U32 index = u64_to_u32(offset / renderer_state->texture_bundle_size);
    Assert(index < MAX_TEXTURE_COUNT);
    return index;
}

U32 index_of(Renderer_State *renderer_state, Material *material)
{
    U64 offset = (U8 *)material - renderer_state->materials;
    Assert(offset >= 0);
    U32 index = u64_to_u32(offset / renderer_state->material_bundle_size);
    Assert(index < MAX_MATERIAL_COUNT);
    return index;
}

U32 index_of(Renderer_State *renderer_state, Static_Mesh *static_mesh)
{
    U64 offset = (U8 *)static_mesh - renderer_state->static_meshes;
    Assert(offset >= 0);
    U32 index = u64_to_u32(offset / renderer_state->static_mesh_bundle_size);
    Assert(index < MAX_STATIC_MESH_COUNT);
    return index;
}

Texture *get_texture(Renderer_State *renderer_state, U32 index)
{
    Assert(index < MAX_TEXTURE_COUNT);
    return (Texture*)(renderer_state->textures + index * renderer_state->texture_bundle_size);
}

Material *get_material(Renderer_State *renderer_state, U32 index)
{
    Assert(index < MAX_MATERIAL_COUNT);
    return (Material*)(renderer_state->materials + index * renderer_state->material_bundle_size);
}

Static_Mesh *get_static_mesh(Renderer_State *renderer_state, U32 index)
{
    Assert(index < MAX_STATIC_MESH_COUNT);
    return (Static_Mesh*)(renderer_state->static_meshes + index * renderer_state->static_mesh_bundle_size);
}

S32 find_texture(Renderer_State *renderer_state, char *name, U32 length)
{
    for (U32 texture_index = 0; texture_index < renderer_state->texture_count; texture_index++)
    {
        Texture* texture = get_texture(renderer_state, texture_index);
        if (texture->name_length == length && strncmp(texture->name, name, length) == 0)
        {
            return texture_index;
        }
    }
    return -1;
}

S32 find_material(Renderer_State *renderer_state, U64 hash)
{
    for (U32 material_index = 0; material_index < renderer_state->material_count; material_index++)
    {
        Material *material = get_material(renderer_state, material_index);
        if (material->hash == hash)
        {
            return material_index;
        }
    }
    return -1;
}