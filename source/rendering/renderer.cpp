#pragma warning(push, 0)
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#pragma warning(pop)

#include "rendering/renderer.h"
#include "core/platform.h"

#ifdef HE_RHI_VULKAN
#include "rendering/vulkan/vulkan.h"
#endif

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

bool load_static_mesh(Static_Mesh *static_mesh, const char *path, Renderer *renderer, Memory_Arena *arena)
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

    cgltf_options options = {};
    cgltf_data *data = nullptr;
    if (cgltf_parse(&options, buffer, result.size, &data) == cgltf_result_success)
    {
        cgltf_load_buffers(&options, data, path);

        Assert(data->meshes_count >= 1);
        cgltf_mesh *mesh = &data->meshes[0];

        Assert(mesh->primitives_count >= 1);
        cgltf_primitive *primitive = &mesh->primitives[0];

        cgltf_material *material = primitive->material;
        Assert(material->has_pbr_metallic_roughness);

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

        char albdeo_texture_path[256];
        sprintf(albdeo_texture_path, "%.*s/%s", u64_to_u32(path_without_file_name_length),
                path, material->pbr_metallic_roughness.base_color_texture.texture->image->uri);

        S32 albedo_texture_width;
        S32 albedo_texture_height;
        S32 albedo_texture_channels;
        stbi_uc *albedo_pixels = stbi_load(albdeo_texture_path,
                                            &albedo_texture_width, &albedo_texture_height,
                                            &albedo_texture_channels, STBI_rgb_alpha);

        Assert(albedo_pixels);
        bool mipmapping = true;
        bool created = renderer->create_texture(&static_mesh->albedo,
                                                albedo_texture_width,
                                                albedo_texture_height,
                                                albedo_pixels, TextureFormat_RGBA, mipmapping);
        Assert(created);

        stbi_image_free(albedo_pixels);

        Assert(primitive->type == cgltf_primitive_type_triangles);

        for (U32 i = 0; i < primitive->attributes_count; i++)
        {
            cgltf_attribute *attribute = &primitive->attributes[i];
            Assert(attribute->type != cgltf_attribute_type_invalid);
            switch (attribute->type)
            {
                case cgltf_attribute_type_position:
                {
                    Assert(attribute->data->type == cgltf_type_vec3);
                    Assert(attribute->data->component_type == cgltf_component_type_r_32f);
                    // Assert(attribute->data->buffer_view->type == cgltf_buffer_view_type_vertices);

                    position_count = u64_to_u32(attribute->data->count);
                    U64 stride = attribute->data->stride;
                    Assert(stride == sizeof(glm::vec3));

                    const auto *accessor = attribute->data;
                    const auto *view = accessor->buffer_view;
                    auto *data_ptr = (U8*)view->buffer->data;

                    U8* position_buffer = data_ptr + view->offset + accessor->offset;
                    positions = (glm::vec3 *)position_buffer;
                } break;

                case cgltf_attribute_type_normal:
                {
                    Assert(attribute->data->type == cgltf_type_vec3);
                    Assert(attribute->data->component_type == cgltf_component_type_r_32f);
                    // Assert(attribute->data->buffer_view->type == cgltf_buffer_view_type_vertices);

                    normal_count = u64_to_u32(attribute->data->count);
                    U64 stride = attribute->data->stride;
                    Assert(stride == sizeof(glm::vec3));

                    const auto *accessor = attribute->data;
                    const auto *view = accessor->buffer_view;
                    auto *data_ptr = (U8*)view->buffer->data;

                    U8 *normal_buffer = data_ptr + view->offset + accessor->offset;
                    normals = (glm::vec3*)normal_buffer;
                } break;

                case cgltf_attribute_type_texcoord:
                {
                    Assert(attribute->data->type == cgltf_type_vec2);
                    Assert(attribute->data->component_type == cgltf_component_type_r_32f);
                    // Assert(attribute->data->buffer_view->type == cgltf_buffer_view_type_vertices);

                    uv_count = u64_to_u32(attribute->data->count);
                    U64 stride = attribute->data->stride;
                    Assert(stride == sizeof(glm::vec2));

                    const auto* accessor = attribute->data;
                    const auto* view = accessor->buffer_view;
                    auto *data_ptr = (U8*)view->buffer->data;
                    U8 *uv_buffer = data_ptr + view->offset + accessor->offset;
                    uvs = (glm::vec2*)uv_buffer;
                } break;
            }
        }

        Assert(primitive->indices->type == cgltf_type_scalar);
        Assert(primitive->indices->component_type == cgltf_component_type_r_16u);
        Assert(primitive->indices->stride == sizeof(U16));
        index_count = u64_to_u32(primitive->indices->count);
        const auto *accessor = primitive->indices;
        const auto *view = accessor->buffer_view;
        U8 *data_ptr = (U8*)view->buffer->data;
        indices = (U16*)(data_ptr + view->offset + accessor->offset);
    }

    Assert(position_count == normal_count);
    Assert(position_count == uv_count);

    U32 vertex_count = position_count;
    Vertex *vertices = AllocateArray(&temp_arena, Vertex, vertex_count);

    for (U32 vertex_index = 0; vertex_index < vertex_count; vertex_index++)
    {
        Vertex *vertex = &vertices[vertex_index];
        vertex->position = positions[vertex_index];
        vertex->normal = normals[vertex_index];
        vertex->uv = uvs[vertex_index];
    }

    bool created = renderer->create_static_mesh(static_mesh, vertices, vertex_count,
                                                indices, index_count);
    Assert(created);
    cgltf_free(data);

    return true;
}