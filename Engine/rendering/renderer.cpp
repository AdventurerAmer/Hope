#pragma warning(push, 0)

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "core/memory.h"
#include "core/engine.h"
#include "core/file_system.h"
#include "core/job_system.h"
#include "core/debugging.h"
#include "containers/queue.h"

static Free_List_Allocator *_transfer_allocator;
static Free_List_Allocator *_stbi_allocator;

#define STBI_MALLOC(sz) allocate(_stbi_allocator, sz, 0);
#define STBI_REALLOC(p, newsz) reallocate(_stbi_allocator, p, newsz, 0)
#define STBI_FREE(p) deallocate(_stbi_allocator, p)

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#pragma warning(pop)

#include "rendering/renderer.h"
#include "core/platform.h"
#include "core/cvars.h"

#if HOPE_OS_WINDOWS
#define HOPE_RHI_VULKAN
#endif

#ifdef HOPE_RHI_VULKAN
#include "rendering/vulkan/vulkan_renderer.h"
#endif

// todo(amer): this is going to be a percentage
HOPE_CVarInt(back_buffer_width, "back buffer width", -1, "renderer", CVarFlag_None);
HOPE_CVarInt(back_buffer_height, "back buffer height", -1, "renderer", CVarFlag_None);

bool request_renderer(RenderingAPI rendering_api,
                      Renderer *renderer)
{
    bool result = true;

    switch (rendering_api)
    {
#ifdef HOPE_RHI_VULKAN
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
            renderer->create_shader = &vulkan_renderer_create_shader;
            renderer->destroy_shader = &vulkan_renderer_destroy_shader;
            renderer->create_pipeline_state = &vulkan_renderer_create_pipeline_state;
            renderer->destroy_pipeline_state = &vulkan_renderer_destroy_pipeline_state;
            renderer->begin_frame = &vulkan_renderer_begin_frame;
            renderer->submit_static_mesh = &vulkan_renderer_submit_static_mesh;
            renderer->end_frame = &vulkan_renderer_end_frame;
            renderer->imgui_new_frame = &vulkan_renderer_imgui_new_frame;
        } break;
#endif

        default:
        {
            result = false;
        } break;
    }

    return result;
}

bool pre_init_renderer_state(Renderer_State *renderer_state, Engine *engine)
{
    renderer_state->engine = engine;
    renderer_state->textures = AllocateArray(&engine->memory.transient_arena, Texture, MAX_TEXTURE_COUNT);
    renderer_state->materials = AllocateArray(&engine->memory.transient_arena, Material, MAX_MATERIAL_COUNT);
    renderer_state->static_meshes = AllocateArray(&engine->memory.transient_arena, Static_Mesh, MAX_STATIC_MESH_COUNT);
    renderer_state->scene_nodes = AllocateArray(&engine->memory.transient_arena, Scene_Node, MAX_SCENE_NODE_COUNT);
    renderer_state->shaders = AllocateArray(&engine->memory.transient_arena, Shader, MAX_SHADER_COUNT);
    renderer_state->pipeline_states = AllocateArray(&engine->memory.transient_arena, Pipeline_State, MAX_PIPELINE_STATE_COUNT);

    bool render_commands_mutex_created = platform_create_mutex(&renderer_state->render_commands_mutex);
    HOPE_Assert(render_commands_mutex_created);

    HOPE_CVarGetInt(back_buffer_width, "renderer");
    HOPE_CVarGetInt(back_buffer_height, "renderer");

    if (*back_buffer_width == -1 || *back_buffer_height == -1)
    {
        // todo(amer): get video modes and pick highest one
        *back_buffer_width = 1280;
        *back_buffer_height = 720;
    }

    renderer_state->back_buffer_width = (U32)*back_buffer_width;
    renderer_state->back_buffer_height = (U32)*back_buffer_height;

    return true;
}

bool init_renderer_state(Renderer_State *renderer_state, Engine *engine)
{
    _transfer_allocator = renderer_state->transfer_allocator;
    _stbi_allocator = &engine->memory.free_list_allocator;

    Renderer *renderer = &engine->renderer;
    renderer_state->white_pixel_texture = allocate_texture(renderer_state);

    U32 *white_pixel_data = Allocate(renderer_state->transfer_allocator, U32);
    *white_pixel_data = 0xFFFFFFFF;

    Texture_Descriptor white_pixel_descriptor = {};
    white_pixel_descriptor.width = 1;
    white_pixel_descriptor.height = 1;
    white_pixel_descriptor.data = white_pixel_data;
    white_pixel_descriptor.format = TextureFormat_RGBA;
    white_pixel_descriptor.mipmapping = false;
    renderer->create_texture(renderer_state->white_pixel_texture, white_pixel_descriptor);

    U32 *normal_pixel_data = Allocate(renderer_state->transfer_allocator, U32);
    *normal_pixel_data = 0xFFFF8080; // todo(amer): endianness
    HOPE_Assert(HOPE_ARCH_X64);

    Texture_Descriptor normal_pixel_descriptor = {};
    normal_pixel_descriptor.width = 1;
    normal_pixel_descriptor.height = 1;
    normal_pixel_descriptor.data = normal_pixel_data;
    normal_pixel_descriptor.format = TextureFormat_RGBA;
    normal_pixel_descriptor.mipmapping = false;

    renderer_state->normal_pixel_texture = allocate_texture(renderer_state);
    renderer->create_texture(renderer_state->normal_pixel_texture, normal_pixel_descriptor);

    return true;
}

void deinit_renderer_state(struct Renderer *renderer, Renderer_State *renderer_state)
{
    HOPE_CVarGetInt(back_buffer_width, "renderer");
    HOPE_CVarGetInt(back_buffer_height, "renderer");

    *back_buffer_width = renderer_state->back_buffer_width;
    *back_buffer_height = renderer_state->back_buffer_height;

    for (U32 texture_index = 0; texture_index < renderer_state->texture_count; texture_index++)
    {
        renderer->destroy_texture(&renderer_state->textures[texture_index]);
    }

    for (U32 material_index = 0; material_index < renderer_state->material_count; material_index++)
    {
        renderer->destroy_material(&renderer_state->materials[material_index]);
    }

    for (U32 static_mesh_index = 0; static_mesh_index < renderer_state->static_mesh_count; static_mesh_index++)
    {
        renderer->destroy_static_mesh(&renderer_state->static_meshes[static_mesh_index]);
    }

    for (U32 shader_index = 0; shader_index < renderer_state->shader_count; shader_index++)
    {
        renderer->destroy_shader(&renderer_state->shaders[shader_index]);
    }

    for (U32 pipeline_state_index = 0; pipeline_state_index < renderer_state->pipeline_state_count; pipeline_state_index++)
    {
        renderer->destroy_pipeline_state(&renderer_state->pipeline_states[pipeline_state_index]);
    }
}

Scene_Node*
add_child_scene_node(Renderer_State *renderer_state,
                     Scene_Node *parent)
{
    HOPE_Assert(renderer_state->scene_node_count < MAX_SCENE_NODE_COUNT);
    HOPE_Assert(parent);

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

struct Load_Texture_Job_Data
{
    String path;
    Renderer *renderer;
    Renderer_State *renderer_state;
    Texture *texture;
};

static bool create_texture(Texture *texture, void *pixels, U32 texture_width, U32 texture_height, Renderer *renderer, Renderer_State *renderer_state)
{
    U64 data_size = texture_width * texture_height * sizeof(U32);
    U32 *data = AllocateArray(renderer_state->transfer_allocator, U32, data_size); // @Leak
    memcpy(data, pixels, data_size);

    Texture_Descriptor descriptor = {};
    descriptor.width = texture_width;
    descriptor.height = texture_height;
    descriptor.data = data;
    descriptor.format = TextureFormat_RGBA;
    descriptor.mipmapping = true;

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    bool texture_created = renderer->create_texture(texture, descriptor);
    HOPE_Assert(texture_created);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
    return true;
}

static Job_Result load_texture_job(const Job_Parameters &params)
{
    Load_Texture_Job_Data *job_data = (Load_Texture_Job_Data *)params.data;
    const String &path = job_data->path;

    Renderer *renderer = job_data->renderer;
    Renderer_State *renderer_state = job_data->renderer_state;

    S32 texture_width;
    S32 texture_height;
    S32 texture_channels;

    stbi_uc *pixels = stbi_load(path.data,
                                &texture_width, &texture_height,
                                &texture_channels, STBI_rgb_alpha);
    HOPE_Assert(pixels);

    bool texture_created = create_texture(job_data->texture, pixels, texture_width, texture_height, renderer, renderer_state);
    HOPE_Assert(texture_created);
    stbi_image_free(pixels);

    return Job_Result::SUCCEEDED;
}

static Texture*
cgltf_load_texture(cgltf_texture_view *texture_view, const String &model_path,
                   Renderer *renderer, Renderer_State *renderer_state, Memory_Arena *arena)
{
    Temprary_Memory_Arena temprary_arena;
    begin_temprary_memory_arena(&temprary_arena, arena);

    const cgltf_image *image = texture_view->texture->image;

    String texture_path = {};
    Texture *texture = nullptr;

    if (texture_view->texture->image->uri)
    {
        char *uri = texture_view->texture->image->uri;
        texture_path = format_string(temprary_arena.arena, "%.*s/%s", HOPE_ExpandString(&model_path), uri);
    }
    else
    {
        String texture_name = HOPE_String(texture_view->texture->image->name);
        S64 dot_index = find_first_char_from_right(&texture_name, ".");
        HOPE_Assert(dot_index != -1);

        String extension_to_append = HOPE_StringLiteral("");
        String extension = sub_string(&texture_name, dot_index);

        if (extension != ".png" &&
            extension != ".jpg")
        {
            String mime_type = HOPE_String(image->mime_type);

            if (mime_type == "image/png")
            {
                extension_to_append = HOPE_StringLiteral(".png");
            }
            else if (mime_type == "image/jpg")
            {
                extension_to_append = HOPE_StringLiteral(".jpg");
            }
        }

        texture_path = format_string(temprary_arena.arena, "%.*s/%.*s%s",
                                     HOPE_ExpandString(&model_path),
                                     HOPE_ExpandString(&texture_name),
                                     HOPE_ExpandString(&extension_to_append));
    }

    S32 texture_index = find_texture(renderer_state,
                                     texture_path);

    if (texture_index == -1)
    {
        HOPE_Assert(renderer_state->texture_count < MAX_TEXTURE_COUNT);
        texture = allocate_texture(renderer_state);
        texture->name = copy_string(texture_path.data, texture_path.count, &renderer_state->engine->memory.free_list_allocator);

        if (platform_file_exists(texture_path.data))
        {
            Load_Texture_Job_Data load_texture_job_data = {};
            load_texture_job_data.path = texture->name;
            load_texture_job_data.renderer = renderer;
            load_texture_job_data.renderer_state = renderer_state;
            load_texture_job_data.texture = texture;

            Job job = {};
            job.parameters.data = &load_texture_job_data;
            job.parameters.size = sizeof(load_texture_job_data);
            job.proc = load_texture_job;
            execute_job(job);
        }
        else
        {
            const auto *view = image->buffer_view;
            U8 *data_ptr = (U8*)view->buffer->data;
            U8 *image_data = data_ptr + view->offset;

            S32 texture_width;
            S32 texture_height;
            S32 texture_channels;

            stbi_uc *pixels = nullptr;

            pixels = stbi_load_from_memory(image_data, u64_to_u32(view->size),
                                           &texture_width, &texture_height,
                                           &texture_channels, STBI_rgb_alpha);

            HOPE_Assert(pixels);
            bool texture_created = create_texture(texture, pixels, texture_width, texture_height, renderer, renderer_state);
            HOPE_Assert(texture_created);
            stbi_image_free(pixels);
        }
    }
    else
    {
        texture = &renderer_state->textures[texture_index];
    }

    end_temprary_memory_arena(&temprary_arena);
    return texture;
}

static void* _cgltf_alloc(void* user, cgltf_size size)
{
    return allocate(_transfer_allocator, size, 8);
}

static void _cgltf_free(void* user, void *ptr)
{
    deallocate(_transfer_allocator, ptr);
}

struct Load_Model_Job_Data
{
    String path;
    Renderer *renderer;
    Renderer_State *renderer_state;
    Scene_Node *scene_node;
};

Job_Result load_model_job(const Job_Parameters &params)
{
    Temprary_Memory_Arena *tempray_memory_arena = params.temprary_memory_arena;
    Load_Model_Job_Data *data = (Load_Model_Job_Data *)params.data;
    bool model_loaded = load_model(data->scene_node, data->path, data->renderer, data->renderer_state, tempray_memory_arena->arena);
    if (!model_loaded)
    {
        return Job_Result::FAILED;
    }
    return Job_Result::SUCCEEDED;
}

Scene_Node* load_model_threaded(const String &path, Renderer *renderer, Renderer_State *renderer_state)
{
    Scene_Node *root_scene_node = &renderer_state->scene_nodes[renderer_state->scene_node_count++];

    Load_Model_Job_Data data = {};
    data.path = path;
    data.renderer = renderer;
    data.renderer_state = renderer_state;
    data.scene_node = root_scene_node;

    Job job = {};
    job.proc = load_model_job;
    job.parameters.data = &data;
    job.parameters.size = sizeof(Load_Model_Job_Data);
    execute_job(job);

    return root_scene_node;
}

// note(amer): https://github.com/deccer/CMake-Glfw-OpenGL-Template/blob/main/src/Project/ProjectApplication.cpp
// thanks to this giga chad for the example
bool load_model(Scene_Node *root_scene_node, const String &path, Renderer *renderer, Renderer_State *renderer_state, Memory_Arena *arena)
{
    Read_Entire_File_Result result = read_entire_file(path.data, renderer_state->transfer_allocator);

    if (!result.success)
    {
        return nullptr;
    }

    U8 *buffer = result.data;

    S64 last_slash = find_first_char_from_right(&path, "\\/");
    HOPE_Assert(last_slash != -1);
    String model_path = sub_string(&path, 0, last_slash);

    cgltf_options options = {};
    options.memory.alloc_func = _cgltf_alloc;
    options.memory.free_func = _cgltf_free;

    cgltf_data *model_data = nullptr;

    if (cgltf_parse(&options, buffer, result.size, &model_data) != cgltf_result_success)
    {
        return nullptr;
    }

    if (cgltf_load_buffers(&options, model_data, path.data) != cgltf_result_success)
    {
        return nullptr;
    }

    for (U32 material_index = 0; material_index < model_data->materials_count; material_index++)
    {
        cgltf_material *material = &model_data->materials[material_index];
        U64 material_hash = (U64)material;

        HOPE_Assert(renderer_state->material_count < MAX_MATERIAL_COUNT);
        Material *renderer_material = allocate_material(renderer_state);

        if (material->name)
        {
            renderer_material->name = copy_string(material->name,
                                                  string_length(material->name),
                                                  &renderer_state->engine->memory.free_list_allocator);
        }
        renderer_material->hash = material_hash;

        Texture *albedo = nullptr;
        Texture *normal = nullptr;
        Texture *metallic_roughness = nullptr;

        if (material->has_pbr_metallic_roughness)
        {
            if (material->pbr_metallic_roughness.base_color_texture.texture)
            {
                albedo = cgltf_load_texture(&material->pbr_metallic_roughness.base_color_texture,
                                            model_path,
                                            renderer,
                                            renderer_state, arena);
            }
            else
            {
                albedo = renderer_state->white_pixel_texture;
            }

            if (material->pbr_metallic_roughness.metallic_roughness_texture.texture)
            {
                metallic_roughness = cgltf_load_texture(&material->pbr_metallic_roughness.base_color_texture,
                                                        model_path,
                                                        renderer,
                                                        renderer_state, arena);
            }
            else
            {
                metallic_roughness = renderer_state->white_pixel_texture;
            }
        }

        if (material->normal_texture.texture)
        {
            normal = cgltf_load_texture(&material->normal_texture,
                                        model_path,
                                        renderer,
                                        renderer_state, arena);
        }
        else
        {
            normal = renderer_state->normal_pixel_texture;
        }

        Material_Descriptor desc = {};
        desc.pipeline_state = renderer_state->mesh_pipeline;

        platform_lock_mutex(&renderer_state->render_commands_mutex);
        renderer->create_material(renderer_material, desc);
        platform_unlock_mutex(&renderer_state->render_commands_mutex);

        U32 *albedo_texture_index = (U32 *)get_property(renderer_material, HOPE_StringLiteral("albedo_texture_index"), ShaderDataType_U32);
        U32 *normal_texture_index = (U32 *)get_property(renderer_material, HOPE_StringLiteral("normal_texture_index"), ShaderDataType_U32);
        U32 *occlusion_roughness_metallic_texture_index = (U32 *)get_property(renderer_material,
                                                                              HOPE_StringLiteral("occlusion_roughness_metallic_texture_index"),
                                                                              ShaderDataType_U32);

        glm::vec3 *albedo_color = (glm::vec3 *)get_property(renderer_material,
                                                            HOPE_StringLiteral("albedo_color"),
                                                            ShaderDataType_Vector3f);

        F32 *roughness_factor = (F32 *)get_property(renderer_material,
                                                    HOPE_StringLiteral("roughness_factor"),
                                                    ShaderDataType_F32);

        F32 *metallic_factor = (F32 *)get_property(renderer_material,
                                                    HOPE_StringLiteral("metallic_factor"),
                                                    ShaderDataType_F32);

        F32 *reflectance = (F32 *)get_property(renderer_material,
                                               HOPE_StringLiteral("reflectance"),
                                               ShaderDataType_F32);
        
        *albedo_color = *(glm::vec3 *)material->pbr_metallic_roughness.base_color_factor;
        *roughness_factor = material->pbr_metallic_roughness.roughness_factor;
        *metallic_factor = material->pbr_metallic_roughness.metallic_factor;
        *reflectance = 0.04f;
        *albedo_texture_index = index_of(renderer_state, albedo);
        *normal_texture_index = index_of(renderer_state, normal);
        *occlusion_roughness_metallic_texture_index = index_of(renderer_state, metallic_roughness);
    }

    U32 position_count = 0;
    glm::vec3 *positions = nullptr;

    U32 normal_count = 0;
    glm::vec3 *normals = nullptr;

    U32 uv_count = 0;
    glm::vec2 *uvs = nullptr;

    U32 tangent_count = 0;
    glm::vec4 *tangents = nullptr;

    U32 index_count = 0;
    U16 *indices = nullptr;

    root_scene_node->parent = nullptr;
    root_scene_node->transform = glm::mat4(1.0f);

    struct Scene_Node_Bundle
    {
        cgltf_node *cgltf_node;
        Scene_Node *node;
    };

    Temprary_Memory_Arena temprary_arena = {};
    begin_temprary_memory_arena(&temprary_arena, arena);

    Ring_Queue< Scene_Node_Bundle > nodes;
    init_ring_queue(&nodes, 4096, &temprary_arena);

    for (U32 node_index = 0; node_index < model_data->nodes_count; node_index++)
    {
        push(&nodes, { &model_data->nodes[node_index], add_child_scene_node(renderer_state, root_scene_node) });
    }

    while (true)
    {
        Scene_Node_Bundle node_bundle = {};
        bool peeked = peek_front(&nodes, &node_bundle);
        if (!peeked)
        {
            break;
        }
        pop_front(&nodes);

        Scene_Node *scene_node = node_bundle.node;
        cgltf_node *node = node_bundle.cgltf_node;

        cgltf_node_transform_world(node, glm::value_ptr(scene_node->transform));

        if (node->mesh)
        {
            scene_node->start_mesh_index = renderer_state->static_mesh_count;
            scene_node->static_mesh_count += u64_to_u32(node->mesh->primitives_count);

            for (U32 primitive_index = 0; primitive_index < node->mesh->primitives_count; primitive_index++)
            {
                cgltf_primitive* primitive = &node->mesh->primitives[primitive_index];
                HOPE_Assert(primitive->material);
                cgltf_material* material = primitive->material;

                U64 material_hash = (U64)material;
                S32 material_index = find_material(renderer_state, material_hash);
                HOPE_Assert(material_index != -1);

                Static_Mesh* static_mesh = allocate_static_mesh(renderer_state);
                static_mesh->material = &renderer_state->materials[material_index];

                HOPE_Assert(primitive->type == cgltf_primitive_type_triangles);

                for (U32 attribute_index = 0; attribute_index < primitive->attributes_count; attribute_index++)
                {
                    cgltf_attribute* attribute = &primitive->attributes[attribute_index];
                    HOPE_Assert(attribute->type != cgltf_attribute_type_invalid);

                    const auto *accessor = attribute->data;
                    const auto *view = accessor->buffer_view;
                    U8 *data_ptr = (U8*)view->buffer->data;

                    switch (attribute->type)
                    {
                        case cgltf_attribute_type_position:
                        {
                            HOPE_Assert(attribute->data->type == cgltf_type_vec3);
                            HOPE_Assert(attribute->data->component_type == cgltf_component_type_r_32f);

                            position_count = u64_to_u32(attribute->data->count);
                            U64 stride = attribute->data->stride;
                            HOPE_Assert(stride == sizeof(glm::vec3));
                            positions = (glm::vec3*)(data_ptr + view->offset + accessor->offset);
                        } break;

                        case cgltf_attribute_type_normal:
                        {
                            HOPE_Assert(attribute->data->type == cgltf_type_vec3);
                            HOPE_Assert(attribute->data->component_type == cgltf_component_type_r_32f);

                            normal_count = u64_to_u32(attribute->data->count);
                            U64 stride = attribute->data->stride;
                            HOPE_Assert(stride == sizeof(glm::vec3));
                            normals = (glm::vec3*)(data_ptr + view->offset + accessor->offset);
                        } break;

                        case cgltf_attribute_type_texcoord:
                        {
                            HOPE_Assert(attribute->data->type == cgltf_type_vec2);
                            HOPE_Assert(attribute->data->component_type == cgltf_component_type_r_32f);

                            uv_count = u64_to_u32(attribute->data->count);
                            U64 stride = attribute->data->stride;
                            HOPE_Assert(stride == sizeof(glm::vec2));
                            uvs = (glm::vec2*)(data_ptr + view->offset + accessor->offset);
                        } break;

                        case cgltf_attribute_type_tangent:
                        {
                            HOPE_Assert(attribute->data->type == cgltf_type_vec4);
                            HOPE_Assert(attribute->data->component_type == cgltf_component_type_r_32f);
                            tangent_count = u64_to_u32(attribute->data->count);
                            U64 stride = attribute->data->stride;
                            HOPE_Assert(stride == sizeof(glm::vec4));
                            tangents = (glm::vec4*)(data_ptr + view->offset + accessor->offset);
                        } break;
                    }
                }

                // note(amer): we only support u16 indices for now.
                HOPE_Assert(primitive->indices->type == cgltf_type_scalar);
                HOPE_Assert(primitive->indices->component_type == cgltf_component_type_r_16u);
                HOPE_Assert(primitive->indices->stride == sizeof(U16));

                index_count = u64_to_u32(primitive->indices->count);
                const auto *accessor = primitive->indices;
                const auto *view = accessor->buffer_view;
                U8 *data_ptr = (U8*)view->buffer->data;
                indices = (U16*)(data_ptr + view->offset + accessor->offset);

                HOPE_Assert(position_count == normal_count);
                HOPE_Assert(position_count == uv_count);
                // HOPE_Assert(position_count == tangent_count);

                U32 vertex_count = position_count;

                Static_Mesh_Descriptor descriptor = {};
                descriptor.vertex_count = vertex_count;
                descriptor.positions = positions;
                descriptor.normals = normals;
                descriptor.uvs = uvs;
                descriptor.tangents = tangents;
                descriptor.indices = indices;
                descriptor.index_count = index_count;

                platform_lock_mutex(&renderer_state->render_commands_mutex);
                bool created = renderer->create_static_mesh(static_mesh, descriptor);
                platform_unlock_mutex(&renderer_state->render_commands_mutex);
                HOPE_Assert(created);
            }
        }

        for (U32 child_node_index = 0;
            child_node_index < node->children_count;
            child_node_index++)
        {
            push(&nodes, { node->children[child_node_index], add_child_scene_node(renderer_state, scene_node) });
        }
    }

    end_temprary_memory_arena(&temprary_arena);

    // cgltf_free(model_data);
    // deallocate(renderer_state->transfer_allocator, buffer);

    return true;
}

Scene_Node *load_model(const String &path, Renderer *renderer, Renderer_State *renderer_state, Memory_Arena *arena)
{
    Scene_Node *root_scene_node = &renderer_state->scene_nodes[renderer_state->scene_node_count++];
    bool model_loaded = load_model(root_scene_node, path, renderer, renderer_state, arena);
    HOPE_Assert(model_loaded);
    return root_scene_node;
}

void render_scene_node(Renderer *renderer, Renderer_State *renderer_state, Scene_Node *scene_node, const glm::mat4 &parent_transform)
{
    glm::mat4 transform = parent_transform * scene_node->transform;

    for (U32 static_mesh_index = 0;
         static_mesh_index < scene_node->static_mesh_count;
         static_mesh_index++)
    {
        Static_Mesh *static_mesh = renderer_state->static_meshes + scene_node->start_mesh_index + static_mesh_index;
        renderer->submit_static_mesh(renderer_state, static_mesh, transform);
    }

    for (Scene_Node *node = scene_node->first_child; node; node = node->next_sibling)
    {
        render_scene_node(renderer, renderer_state, node, transform);
    }
}

Texture *allocate_texture(Renderer_State *renderer_state)
{
    HOPE_Assert(renderer_state->texture_count < MAX_TEXTURE_COUNT);
    U32 texture_index = renderer_state->texture_count++;
    Texture *result = &renderer_state->textures[texture_index];
    return result;
}

Material *allocate_material(Renderer_State *renderer_state)
{
    HOPE_Assert(renderer_state->material_count < MAX_MATERIAL_COUNT);

    U32 material_index = renderer_state->material_count++;
    Material *result = &renderer_state->materials[material_index];

    return result;
}

Static_Mesh *allocate_static_mesh(Renderer_State *renderer_state)
{
    HOPE_Assert(renderer_state->static_mesh_count < MAX_STATIC_MESH_COUNT);
    U32 static_mesh_index = renderer_state->static_mesh_count++;
    Static_Mesh *result = &renderer_state->static_meshes[static_mesh_index];
    return result;
}

Shader *allocate_shader(Renderer_State *renderer_state)
{
    HOPE_Assert(renderer_state->shader_count < MAX_STATIC_MESH_COUNT);
    U32 shader_index = renderer_state->shader_count++;
    Shader *result = &renderer_state->shaders[shader_index];
    return result;
}

Pipeline_State *allocate_pipeline_state(Renderer_State *renderer_state)
{
    HOPE_Assert(renderer_state->pipeline_state_count < MAX_PIPELINE_STATE_COUNT);
    U32 pipline_state_index = renderer_state->pipeline_state_count++;
    Pipeline_State *result = &renderer_state->pipeline_states[pipline_state_index];
    return result;
}

U8 *get_property(Material *material, const String &name, ShaderDataType shader_datatype)
{
    Shader_Struct *properties = material->properties;
    for (U32 member_index = 0; member_index < properties->member_count; member_index++)
    {
        Shader_Struct_Member *member = &properties->members[member_index];
        if (name == member->name && member->data_type == shader_datatype)
        {
            return material->data + member->offset;
        }
    }
    return nullptr;
}

U32 index_of(Renderer_State *renderer_state, const Texture *texture)
{
    U64 index = texture - renderer_state->textures;
    HOPE_Assert(index < MAX_TEXTURE_COUNT);
    return u64_to_u32(index);
}

U32 index_of(Renderer_State *renderer_state, const Material *material)
{
    U64 index = material - renderer_state->materials;
    HOPE_Assert(index < MAX_MATERIAL_COUNT);
    return u64_to_u32(index);
}

U32 index_of(Renderer_State *renderer_state, const Static_Mesh *static_mesh)
{
    U64 index = static_mesh - renderer_state->static_meshes;
    HOPE_Assert(index < MAX_STATIC_MESH_COUNT);
    return u64_to_u32(index);
}

U32 index_of(Renderer_State *renderer_state, const Shader *shader)
{
    U64 index = shader - renderer_state->shaders;
    HOPE_Assert(index < MAX_SHADER_COUNT);
    return u64_to_u32(index);
}

U32 index_of(Renderer_State *renderer_state, const Pipeline_State *pipeline_state)
{
    U64 index = pipeline_state - renderer_state->pipeline_states;
    HOPE_Assert(index < MAX_SHADER_COUNT);
    return u64_to_u32(index);
}

U32 index_of(Renderer_State *renderer_state, Texture *texture)
{
    U64 index = texture - renderer_state->textures;
    HOPE_Assert(index < MAX_TEXTURE_COUNT);
    return u64_to_u32(index);
}

U32 index_of(Renderer_State *renderer_state, Material *material)
{
    U64 index = material - renderer_state->materials;
    HOPE_Assert(index < MAX_MATERIAL_COUNT);
    return u64_to_u32(index);
}

U32 index_of(Renderer_State *renderer_state, Static_Mesh *static_mesh)
{
    U64 index = static_mesh - renderer_state->static_meshes;
    HOPE_Assert(index < MAX_STATIC_MESH_COUNT);
    return u64_to_u32(index);
}

U32 index_of(Renderer_State *renderer_state, Shader *shader)
{
    U64 index = shader - renderer_state->shaders;
    HOPE_Assert(index < MAX_SHADER_COUNT);
    return u64_to_u32(index);
}

U32 index_of(Renderer_State *renderer_state, Pipeline_State *pipeline_state)
{
    U64 index = pipeline_state - renderer_state->pipeline_states;
    HOPE_Assert(index < MAX_SHADER_COUNT);
    return u64_to_u32(index);
}

S32 find_texture(Renderer_State *renderer_state, const String &name)
{
    for (U32 texture_index = 0; texture_index < renderer_state->texture_count; texture_index++)
    {
        Texture *texture = &renderer_state->textures[texture_index];
        if (texture->name == name)
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
        Material *material = &renderer_state->materials[material_index];
        if (material->hash == hash)
        {
            return material_index;
        }
    }
    return -1;
}


U32 get_size_of_shader_data_type(ShaderDataType shader_data_type)
{
    switch (shader_data_type)
    {
        case ShaderDataType_Bool: return 1;

        case ShaderDataType_S8: return 1;
        case ShaderDataType_S16: return 2;
        case ShaderDataType_S32: return 4;
        case ShaderDataType_S64: return 8;

        case ShaderDataType_U8: return 1;
        case ShaderDataType_U16: return 2;
        case ShaderDataType_U32: return 4;
        case ShaderDataType_U64: return 8;

        case ShaderDataType_F16: return 2;
        case ShaderDataType_F32: return 4;
        case ShaderDataType_F64: return 8;

        case ShaderDataType_Vector2f: return 2 * 4;
        case ShaderDataType_Vector3f: return 3 * 4;
        case ShaderDataType_Vector4f: return 4 * 4;

        case ShaderDataType_Matrix3f: return 9 * 4;
        case ShaderDataType_Matrix4f: return 16 * 4;

        default:
        {
            HOPE_Assert(!"unsupported type");
        } break;
    }

    return 0;
}