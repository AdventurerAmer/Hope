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

    // todo(amer): hardcoding texture...
    S32 texture_width;
    S32 texture_height;
    S32 texture_channels;
    stbi_uc *pixels = stbi_load("models/Default_albedo.jpg",
                                &texture_width, &texture_height,
                                &texture_channels, STBI_rgb_alpha);

    Assert(pixels);

    bool created = renderer->create_texture(&static_mesh->albedo,
                                            texture_width, texture_height, pixels, TextureFormat_RGBA);
    Assert(created);

    stbi_image_free(pixels);

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

    if (result.success)
    {
        U8 *buffer = AllocateArray(&temp_arena, U8, result.size);
        platform_end_read_entire_file(&result, buffer);

        cgltf_options options = {};
        cgltf_data *data = nullptr;
        if (cgltf_parse(&options, buffer, result.size, &data) == cgltf_result_success)
        {
            Assert(data->meshes_count >= 1);
            cgltf_mesh *mesh = &data->meshes[0];
            Assert(mesh->primitives_count >= 1);
            cgltf_primitive *primitive = &mesh->primitives[0];
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
                        Assert(attribute->data->buffer_view->type == cgltf_buffer_view_type_vertices);

                        position_count = u64_to_u32(attribute->data->count);
                        U64 stride = attribute->data->stride;
                        Assert(stride == sizeof(glm::vec3));

                        U64 buffer_offset = attribute->data->buffer_view->buffer->extras.start_offset;
                        U8 *position_buffer = ((U8*)data->bin + buffer_offset) + attribute->data->buffer_view->offset;
                        positions = (glm::vec3 *)position_buffer;
                    } break;

                    case cgltf_attribute_type_normal:
                    {
                        Assert(attribute->data->type == cgltf_type_vec3);
                        Assert(attribute->data->component_type == cgltf_component_type_r_32f);
                        Assert(attribute->data->buffer_view->type == cgltf_buffer_view_type_vertices);

                        normal_count = u64_to_u32(attribute->data->count);
                        U64 stride = attribute->data->stride;
                        Assert(stride == sizeof(glm::vec3));

                        U64 buffer_offset = attribute->data->buffer_view->buffer->extras.start_offset;
                        U8* normal_buffer = ((U8*)data->bin + buffer_offset) + attribute->data->buffer_view->offset;
                        normals = (glm::vec3*)normal_buffer;
                    } break;

                    case cgltf_attribute_type_texcoord:
                    {
                        Assert(attribute->data->type == cgltf_type_vec2);
                        Assert(attribute->data->component_type == cgltf_component_type_r_32f);
                        Assert(attribute->data->buffer_view->type == cgltf_buffer_view_type_vertices);

                        uv_count = u64_to_u32(attribute->data->count);
                        U64 stride = attribute->data->stride;
                        Assert(stride == sizeof(glm::vec2));

                        U64 buffer_offset = attribute->data->buffer_view->buffer->extras.start_offset;
                        U8* uv_buffer = ((U8*)data->bin + buffer_offset) + attribute->data->buffer_view->offset;
                        uvs = (glm::vec2*)uv_buffer;
                    } break;
                }
            }

            Assert(primitive->indices->type == cgltf_type_scalar);
            Assert(primitive->indices->component_type == cgltf_component_type_r_16u);
            Assert(primitive->indices->stride == sizeof(U16));
            index_count = u64_to_u32(primitive->indices->count);
            U64 buffer_offset = primitive->indices->buffer_view->buffer->extras.start_offset;
            U8 *index_buffer =
                ((U8*)data->bin + buffer_offset) + primitive->indices->buffer_view->offset;
            indices = (U16*)index_buffer;
            cgltf_free(data);
        }
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

    created = renderer->create_static_mesh(static_mesh, vertices, vertex_count,
                                           indices, index_count);
    Assert(created);

    return true;
}