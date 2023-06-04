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

// note(amer): https://github.com/deccer/CMake-Glfw-OpenGL-Template/blob/main/src/Project/ProjectApplication.cpp
// thanks to this giga chad for the example

bool load_model(Model *model, const char *path, Renderer *renderer,
                Renderer_State *renderer_state, Memory_Arena *arena)
{
    Scoped_Temprary_Memory_Arena temp_arena(arena);

    Read_Entire_File_Result result =
        platform_begin_read_entire_file(path);

    U32 position_count = 0;
    glm::vec3 *positions = nullptr;

    U32 normal_count = 0;
    glm::vec3 *normals = nullptr;

    U32 uv_count = 0;
    glm::vec2 *uvs = nullptr;

    U32 index_count = 0;
    U16 *indices = nullptr;

    if (!result.success)
    {
        return false;
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
        return false;
    }

    if (cgltf_load_buffers(&options, model_data, path) != cgltf_result_success)
    {
        return false;
    }

    Assert(model_data->materials_count >= 1);

    for (U32 material_index = 0; material_index < model_data->materials_count; material_index++)
    {
        cgltf_material *material = &model_data->materials[material_index];
        U32 material_name_length = u64_to_u32(strlen(material->name));

        if (find_material(renderer_state, material->name, material_name_length) != -1)
        {
            continue;
        }

        Material *renderer_material = &renderer_state->materials[renderer_state->material_count++];
        strcpy(renderer_material->name, material->name);
        renderer_material->name_length = material_name_length;

        if (material->has_pbr_metallic_roughness && material->pbr_metallic_roughness.base_color_texture.texture)
        {
            const cgltf_image *image = material->pbr_metallic_roughness.base_color_texture.texture->image;

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

            S32 albedo_texture_index = find_texture(renderer_state, albdeo_texture_path, albdeo_texture_path_length);
            if (albedo_texture_index == -1)
            {
                Texture *albedo = &renderer_state->textures[renderer_state->texture_count++];
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

                    albedo_pixels = stbi_load_from_memory(image_data, view->size,
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
                renderer_material->albedo = albedo;
                renderer->create_material(renderer_material, albedo);
            }
            else
            {
                renderer_material->albedo = &renderer_state->textures[albedo_texture_index];
            }
        }
    }

    U32 static_mesh_count = 0;
    Static_Mesh *static_meshes = &renderer_state->static_meshes[renderer_state->static_mesh_count];
    glm::mat4 *parent_transforms = &renderer_state->parent_transforms[renderer_state->static_mesh_count];

    for (U32 node_index = 0; node_index < model_data->nodes_count; node_index++)
    {
        std::queue<cgltf_node*> nodes;
        nodes.push(&model_data->nodes[node_index]);
        while (!nodes.empty())
        {
            cgltf_node *node = nodes.front();
            nodes.pop();

            if (node->mesh)
            {
                for (U32 primitive_index = 0; primitive_index < node->mesh->primitives_count; primitive_index++)
                {
                    cgltf_primitive *primitive = &node->mesh->primitives[primitive_index];
                    Assert(primitive->material);
                    cgltf_material* material = primitive->material;

                    S32 material_index = find_material(renderer_state, material->name, u64_to_u32(strlen(material->name)));
                    Assert(material_index != -1);

                    U32 static_mesh_index = static_mesh_count++;
                    Static_Mesh *static_mesh = &static_meshes[static_mesh_index];
                    glm::mat4 *parent_transform = &parent_transforms[static_mesh_index];

                    cgltf_node_transform_world(node, glm::value_ptr(*parent_transform));

                    static_mesh->material = &renderer_state->materials[material_index];

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

                    // we only support u16 indices for now.
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

            for (U32 child_node_index = 0; child_node_index < node->children_count; child_node_index++)
            {
                nodes.push(node->children[child_node_index]);
            }
        }
    }

    Assert(static_mesh_count);
    model->static_meshes = static_meshes;
    model->parent_transforms = parent_transforms;
    model->static_mesh_count = static_mesh_count;
    renderer_state->static_mesh_count += static_mesh_count;

    cgltf_free(model_data);
    return true;
}

S32 find_texture(Renderer_State *renderer_state, char *name, U32 length)
{
    for (U32 texture_index = 0; texture_index < renderer_state->texture_count; texture_index++)
    {
        Texture *texture = &renderer_state->textures[texture_index];
        if (texture->name_length == length && strncmp(texture->name, name, length) == 0)
        {
            return texture_index;
        }
    }
    return -1;
}

S32 find_material(Renderer_State *renderer_state, char *name, U32 length)
{
    for (U32 material_index = 0; material_index < renderer_state->material_count; material_index++)
    {
        Material *material = &renderer_state->materials[material_index];
        if (material->name_length == length && strncmp(material->name, name, length) == 0)
        {
            return material_index;
        }
    }
    return -1;
}