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

#if HE_OS_WINDOWS
#define HE_RHI_VULKAN
#endif

#ifdef HE_RHI_VULKAN
#include "rendering/vulkan/vulkan_renderer.h"
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
            renderer->create_buffer = &vulkan_renderer_create_buffer;
            renderer->destroy_buffer = &vulkan_renderer_destroy_buffer;
            renderer->create_texture = &vulkan_renderer_create_texture;
            renderer->destroy_texture = &vulkan_renderer_destroy_texture;
            renderer->create_sampler = &vulkan_renderer_create_sampler;
            renderer->destroy_sampler = &vulkan_renderer_destroy_sampler;
            renderer->create_static_mesh = &vulkan_renderer_create_static_mesh;
            renderer->destroy_static_mesh = &vulkan_renderer_destroy_static_mesh;
            renderer->create_shader = &vulkan_renderer_create_shader;
            renderer->destroy_shader = &vulkan_renderer_destroy_shader;
            renderer->create_pipeline_state = &vulkan_renderer_create_pipeline_state;
            renderer->destroy_pipeline_state = &vulkan_renderer_destroy_pipeline_state;
            renderer->create_shader_group = &vulkan_renderer_create_shader_group;
            renderer->destroy_shader_group = &vulkan_renderer_destroy_shader_group;
            renderer->create_bind_group_layout = &vulkan_renderer_create_bind_group_layout;
            renderer->destroy_bind_group_layout = &vulkan_renderer_destroy_bind_group_layout;
            renderer->create_bind_group = &vulkan_renderer_create_bind_group;
            renderer->update_bind_group = &vulkan_renderer_update_bind_group;
            renderer->destroy_bind_group = &vulkan_renderer_destroy_bind_group;
            renderer->begin_frame = &vulkan_renderer_begin_frame;
            renderer->set_vertex_buffers = &vulkan_renderer_set_vertex_buffers;
            renderer->set_index_buffer = &vulkan_renderer_set_index_buffer;
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
    Memory_Arena *arena = &engine->memory.transient_arena;

    init(&renderer_state->buffers, arena, HE_MAX_BUFFER_COUNT);
    init(&renderer_state->textures, arena, HE_MAX_TEXTURE_COUNT);
    init(&renderer_state->samplers, arena, HE_MAX_SAMPLER_COUNT);
    init(&renderer_state->shaders, arena, HE_MAX_SHADER_COUNT);
    init(&renderer_state->shader_groups, arena, HE_MAX_SHADER_GROUP_COUNT);
    init(&renderer_state->pipeline_states, arena, HE_MAX_PIPELINE_STATE_COUNT);
    init(&renderer_state->bind_group_layouts, arena, HE_MAX_BIND_GROUP_LAYOUT_COUNT);
    init(&renderer_state->bind_groups, arena, HE_MAX_BIND_GROUP_COUNT);
    init(&renderer_state->materials,  arena, HE_MAX_MATERIAL_COUNT);
    init(&renderer_state->static_meshes, arena, HE_MAX_STATIC_MESH_COUNT);

    renderer_state->scene_nodes = HE_ALLOCATE_ARRAY(arena, Scene_Node, HE_MAX_SCENE_NODE_COUNT);

    bool render_commands_mutex_created = platform_create_mutex(&renderer_state->render_commands_mutex);
    HE_ASSERT(render_commands_mutex_created);

    U32 &back_buffer_width = renderer_state->back_buffer_width;
    U32 &back_buffer_height = renderer_state->back_buffer_height;
    back_buffer_width = 1280;
    back_buffer_height = 720;

    HE_DECLARE_CVAR("renderer", back_buffer_width, CVarFlag_None);
    HE_DECLARE_CVAR("renderer", back_buffer_height, CVarFlag_None);
    return true;
}

bool init_renderer_state(Renderer_State *renderer_state, Engine *engine)
{
    Renderer *renderer = &engine->renderer;

    Buffer_Descriptor transfer_buffer_descriptor = {};
    transfer_buffer_descriptor.size = HE_GIGA(2);
    transfer_buffer_descriptor.usage = Buffer_Usage::TRANSFER;
    transfer_buffer_descriptor.is_device_local = false;
    renderer_state->transfer_buffer = aquire_handle(&renderer_state->buffers);
    renderer->create_buffer(renderer_state->transfer_buffer, transfer_buffer_descriptor);

    Buffer *transfer_buffer = get(&renderer_state->buffers, renderer_state->transfer_buffer);
    init_free_list_allocator(&renderer_state->transfer_allocator, transfer_buffer->data, transfer_buffer->size);
    
    renderer_state->white_pixel_texture = aquire_handle(&renderer_state->textures);

    U32 *white_pixel_data = HE_ALLOCATE(&renderer_state->transfer_allocator, U32);
    *white_pixel_data = 0xFFFFFFFF;

    Texture_Descriptor white_pixel_descriptor = {};
    white_pixel_descriptor.width = 1;
    white_pixel_descriptor.height = 1;
    white_pixel_descriptor.data = white_pixel_data;
    white_pixel_descriptor.format = Texture_Format::RGBA;
    white_pixel_descriptor.mipmapping = false;
    renderer->create_texture(renderer_state->white_pixel_texture, white_pixel_descriptor);

    U32 *normal_pixel_data = HE_ALLOCATE(&renderer_state->transfer_allocator, U32);
    *normal_pixel_data = 0xFFFF8080; // todo(amer): endianness
    HE_ASSERT(HE_ARCH_X64);

    Texture_Descriptor normal_pixel_descriptor = {};
    normal_pixel_descriptor.width = 1;
    normal_pixel_descriptor.height = 1;
    normal_pixel_descriptor.data = normal_pixel_data;
    normal_pixel_descriptor.format = Texture_Format::RGBA;
    normal_pixel_descriptor.mipmapping = false;

    renderer_state->normal_pixel_texture = aquire_handle(&renderer_state->textures);
    renderer->create_texture(renderer_state->normal_pixel_texture, normal_pixel_descriptor);

    Sampler_Descriptor default_sampler_descriptor = {};
    default_sampler_descriptor.min_filter = Filter::LINEAR;
    default_sampler_descriptor.mag_filter = Filter::NEAREST;
    default_sampler_descriptor.mip_filter = Filter::LINEAR;
    default_sampler_descriptor.address_mode_u = Address_Mode::REPEAT;
    default_sampler_descriptor.address_mode_v = Address_Mode::REPEAT;
    default_sampler_descriptor.address_mode_w = Address_Mode::REPEAT;
    default_sampler_descriptor.anisotropic_filtering = true;

    renderer_state->default_sampler = aquire_handle(&renderer_state->samplers);
    renderer->create_sampler(renderer_state->default_sampler, default_sampler_descriptor);

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        Buffer_Descriptor globals_uniform_buffer_descriptor = {};
        globals_uniform_buffer_descriptor.size = sizeof(Globals);
        globals_uniform_buffer_descriptor.usage = Buffer_Usage::UNIFORM;
        globals_uniform_buffer_descriptor.is_device_local = false;

        renderer_state->globals_uniform_buffers[frame_index] = aquire_handle(&renderer_state->buffers);
        renderer->create_buffer(renderer_state->globals_uniform_buffers[frame_index], globals_uniform_buffer_descriptor);

        Buffer_Descriptor object_data_storage_buffer_descriptor = {};
        object_data_storage_buffer_descriptor.size = sizeof(Object_Data) * HE_MAX_OBJECT_DATA_COUNT;
        object_data_storage_buffer_descriptor.usage = Buffer_Usage::STORAGE;
        object_data_storage_buffer_descriptor.is_device_local = false;

        renderer_state->object_data_storage_buffers[frame_index] = aquire_handle(&renderer_state->buffers);
        renderer->create_buffer(renderer_state->object_data_storage_buffers[frame_index], object_data_storage_buffer_descriptor);
    }

    U32 max_vertex_count = 1'000'000; // todo(amer): @Hardcode
    renderer_state->max_vertex_count = max_vertex_count;

    Buffer_Descriptor position_buffer_descriptor = {};
    position_buffer_descriptor.size = max_vertex_count * sizeof(glm::vec3);
    position_buffer_descriptor.usage = Buffer_Usage::VERTEX;
    position_buffer_descriptor.is_device_local = true;
    renderer_state->position_buffer = aquire_handle(&renderer_state->buffers);
    renderer->create_buffer(renderer_state->position_buffer, position_buffer_descriptor);

    Buffer_Descriptor normal_buffer_descriptor = {};
    normal_buffer_descriptor.size = max_vertex_count * sizeof(glm::vec3);
    normal_buffer_descriptor.usage = Buffer_Usage::VERTEX;
    normal_buffer_descriptor.is_device_local = true;
    renderer_state->normal_buffer = aquire_handle(&renderer_state->buffers);
    renderer->create_buffer(renderer_state->normal_buffer, normal_buffer_descriptor);

    Buffer_Descriptor uv_buffer_descriptor = {};
    uv_buffer_descriptor.size = max_vertex_count * sizeof(glm::vec2);
    uv_buffer_descriptor.usage = Buffer_Usage::VERTEX;
    uv_buffer_descriptor.is_device_local = true;
    renderer_state->uv_buffer = aquire_handle(&renderer_state->buffers);
    renderer->create_buffer(renderer_state->uv_buffer, uv_buffer_descriptor);

    Buffer_Descriptor tangent_buffer_descriptor = {};
    tangent_buffer_descriptor.size = max_vertex_count * sizeof(glm::vec4);
    tangent_buffer_descriptor.usage = Buffer_Usage::VERTEX;
    tangent_buffer_descriptor.is_device_local = true;
    renderer_state->tangent_buffer = aquire_handle(&renderer_state->buffers);
    renderer->create_buffer(renderer_state->tangent_buffer, tangent_buffer_descriptor);

    Buffer_Descriptor index_buffer_descriptor = {};
    index_buffer_descriptor.size = HE_MEGA(128);
    index_buffer_descriptor.usage = Buffer_Usage::INDEX;
    index_buffer_descriptor.is_device_local = true;
    renderer_state->index_buffer = aquire_handle(&renderer_state->buffers);
    renderer->create_buffer(renderer_state->index_buffer, index_buffer_descriptor);

    _transfer_allocator = &renderer_state->transfer_allocator;
    _stbi_allocator = &engine->memory.free_list_allocator;
    return true;
}

void deinit_renderer_state(struct Renderer *renderer, Renderer_State *renderer_state)
{
    // todo(amer): clean this...
    for (S32 buffer_index = 0; buffer_index < (S32)renderer_state->buffers.capacity; buffer_index++)
    {
        if (!renderer_state->buffers.is_allocated[buffer_index])
        {
            continue;
        }
        renderer->destroy_buffer({ buffer_index, renderer_state->buffers.generations[buffer_index] });
    }

    for (S32 texture_index = 0; texture_index < (S32)renderer_state->textures.capacity; texture_index++)
    {
        if (!renderer_state->textures.is_allocated[texture_index])
        {
            continue;
        }
        renderer->destroy_texture({ texture_index, renderer_state->textures.generations[texture_index] });
    }

    for (S32 sampler_index = 0; sampler_index < (S32)renderer_state->samplers.capacity; sampler_index++)
    {
        if (!renderer_state->samplers.is_allocated[sampler_index])
        {
            continue;
        }
        renderer->destroy_sampler({ sampler_index, renderer_state->samplers.generations[sampler_index] });
    }

    for (S32 static_mesh_index = 0; static_mesh_index < (S32)renderer_state->static_meshes.capacity; static_mesh_index++)
    {
        if (!renderer_state->static_meshes.is_allocated[static_mesh_index])
        {
            continue;
        }
        renderer->destroy_static_mesh({ static_mesh_index, renderer_state->static_meshes.generations[static_mesh_index] });
    }

    for (S32 shader_index = 0; shader_index < (S32)renderer_state->shaders.capacity; shader_index++)
    {
        if (!renderer_state->shaders.is_allocated[shader_index])
        {
            continue;
        }
        renderer->destroy_shader({ shader_index, renderer_state->shaders.generations[shader_index] });
    }

    for (S32 shader_group_index = 0; shader_group_index < (S32)renderer_state->shader_groups.capacity; shader_group_index++)
    {
        if (!renderer_state->shader_groups.is_allocated[shader_group_index])
        {
            continue;
        }
        renderer->destroy_shader_group({ shader_group_index, renderer_state->shader_groups.generations[shader_group_index] });
    }

    for (S32 bind_group_layout_index = 0; bind_group_layout_index < (S32)renderer_state->bind_group_layouts.capacity; bind_group_layout_index++)
    {
        if (!renderer_state->bind_group_layouts.is_allocated[bind_group_layout_index])
        {
            continue;
        }
        renderer->destroy_bind_group_layout({ bind_group_layout_index, renderer_state->bind_group_layouts.generations[bind_group_layout_index] });
    }

    for (S32 pipeline_state_index = 0; pipeline_state_index < (S32)renderer_state->pipeline_states.count; pipeline_state_index++)
    {
        if (!renderer_state->pipeline_states.is_allocated[pipeline_state_index])
        {
            continue;
        }
        renderer->destroy_pipeline_state({ pipeline_state_index, renderer_state->pipeline_states.generations[pipeline_state_index] });
    }
}

Scene_Node* add_child_scene_node(Renderer_State *renderer_state, Scene_Node *parent)
{
    HE_ASSERT(renderer_state->scene_node_count < HE_MAX_SCENE_NODE_COUNT);
    HE_ASSERT(parent);

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
    Texture_Handle texture_handle;
};

static bool create_texture(Texture_Handle texture_handle, void *pixels, U32 texture_width, U32 texture_height, Renderer *renderer, Renderer_State *renderer_state)
{
    U64 data_size = texture_width * texture_height * sizeof(U32);
    U32 *data = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, U32, data_size);
    memcpy(data, pixels, data_size);

    Texture_Descriptor descriptor = {};
    descriptor.width = texture_width;
    descriptor.height = texture_height;
    descriptor.data = data;
    descriptor.format = Texture_Format::RGBA;
    descriptor.mipmapping = true;

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    bool texture_created = renderer->create_texture(texture_handle, descriptor);
    HE_ASSERT(texture_created);
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

    stbi_uc *pixels = stbi_load(path.data, &texture_width, &texture_height, &texture_channels, STBI_rgb_alpha);
    HE_ASSERT(pixels);

    bool texture_created = create_texture(job_data->texture_handle, pixels, texture_width, texture_height, renderer, renderer_state);
    HE_ASSERT(texture_created);
    stbi_image_free(pixels);

    return Job_Result::SUCCEEDED;
}

static Texture_Handle cgltf_load_texture(cgltf_texture_view *texture_view, const String &model_path, Renderer *renderer, Renderer_State *renderer_state, Memory_Arena *arena)
{
    Temprary_Memory_Arena temprary_arena;
    begin_temprary_memory_arena(&temprary_arena, arena);
    HE_DEFER
    {
        end_temprary_memory_arena(&temprary_arena);
    };

    const cgltf_image *image = texture_view->texture->image;

    String texture_path = {};
    Texture_Handle texture_handle = {};

    if (texture_view->texture->image->uri)
    {
        char *uri = texture_view->texture->image->uri;
        texture_path = format_string(temprary_arena.arena, "%.*s/%s", HE_EXPAND_STRING(model_path), uri);
    }
    else
    {
        String texture_name = HE_STRING(texture_view->texture->image->name);
        S64 dot_index = find_first_char_from_right(texture_name, ".");
        HE_ASSERT(dot_index != -1);

        String extension_to_append = HE_STRING_LITERAL("");
        String extension = sub_string(texture_name, dot_index);

        if (extension != ".png" &&
            extension != ".jpg")
        {
            String mime_type = HE_STRING(image->mime_type);

            if (mime_type == "image/png")
            {
                extension_to_append = HE_STRING_LITERAL(".png");
            }
            else if (mime_type == "image/jpg")
            {
                extension_to_append = HE_STRING_LITERAL(".jpg");
            }
        }

        texture_path = format_string(temprary_arena.arena, "%.*s/%.*s%s",
                                     HE_EXPAND_STRING(model_path),
                                     HE_EXPAND_STRING(texture_name),
                                     HE_EXPAND_STRING(extension_to_append));
    }

    Texture_Handle found_texture_handle = find_texture(renderer_state, texture_path);
    if (!is_valid_handle(&renderer_state->textures, found_texture_handle))
    {
        texture_handle = aquire_handle(&renderer_state->textures);
        Texture *texture = get(&renderer_state->textures, texture_handle);
        texture->name = copy_string(texture_path.data, texture_path.count, &renderer_state->engine->memory.free_list_allocator);

        if (platform_file_exists(texture_path.data))
        {
            Load_Texture_Job_Data load_texture_job_data = {};
            load_texture_job_data.path = texture->name;
            load_texture_job_data.renderer = renderer;
            load_texture_job_data.renderer_state = renderer_state;
            load_texture_job_data.texture_handle = texture_handle;

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

            HE_ASSERT(pixels);
            bool texture_created = create_texture(texture_handle, pixels, texture_width, texture_height, renderer, renderer_state);
            HE_ASSERT(texture_created);
            stbi_image_free(pixels);
        }
    }
    else
    {
        texture_handle = found_texture_handle;
    }

    return texture_handle;
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
    Read_Entire_File_Result result = read_entire_file(path.data, &renderer_state->transfer_allocator); // @Leak

    if (!result.success)
    {
        return nullptr;
    }

    U8 *buffer = result.data;

    S64 last_slash = find_first_char_from_right(path, "\\/");
    HE_ASSERT(last_slash != -1);
    String model_path = sub_string(path, 0, last_slash);

    cgltf_options options = {};
    options.memory.alloc_func = _cgltf_alloc;
    options.memory.free_func = _cgltf_free;

    cgltf_data *model_data = nullptr;

    if (cgltf_parse(&options, buffer, result.size, &model_data) != cgltf_result_success)
    {
        return nullptr;
    }

    if (cgltf_load_buffers(&options, model_data, path.data) != cgltf_result_success) // @Leak
    {
        return nullptr;
    }

    for (U32 material_index = 0; material_index < model_data->materials_count; material_index++)
    {
        cgltf_material *material = &model_data->materials[material_index];
        U64 material_hash = (U64)material;

        HE_ASSERT(renderer_state->materials.count < HE_MAX_MATERIAL_COUNT);

        Material_Descriptor material_descriptor = {};
        material_descriptor.pipeline_state_handle = renderer_state->mesh_pipeline;

        platform_lock_mutex(&renderer_state->render_commands_mutex);
        Material_Handle material_handle = create_material(renderer_state, renderer, material_descriptor);
        platform_unlock_mutex(&renderer_state->render_commands_mutex);

        Material *renderer_material = get(&renderer_state->materials, material_handle);

        if (material->name)
        {
            renderer_material->name = copy_string(material->name,
                                                  string_length(material->name),
                                                  &renderer_state->engine->memory.free_list_allocator);
        }
        renderer_material->hash = material_hash;

        Texture_Handle albedo = { -1 };
        Texture_Handle normal = { -1 };
        Texture_Handle metallic_roughness = { -1 };

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

        U32 *albedo_texture_index = (U32 *)get_property(renderer_material, HE_STRING_LITERAL("albedo_texture_index"), Shader_Data_Type::U32);
        U32 *normal_texture_index = (U32 *)get_property(renderer_material, HE_STRING_LITERAL("normal_texture_index"), Shader_Data_Type::U32);
        U32 *orm_texture_index = (U32 *)get_property(renderer_material, HE_STRING_LITERAL("occlusion_roughness_metallic_texture_index"), Shader_Data_Type::U32);
        glm::vec3 *albedo_color = (glm::vec3 *)get_property(renderer_material, HE_STRING_LITERAL("albedo_color"), Shader_Data_Type::VECTOR3F);
        F32 *roughness_factor = (F32 *)get_property(renderer_material, HE_STRING_LITERAL("roughness_factor"), Shader_Data_Type::F32);
        F32 *metallic_factor = (F32 *)get_property(renderer_material, HE_STRING_LITERAL("metallic_factor"), Shader_Data_Type::F32);
        F32 *reflectance = (F32 *)get_property(renderer_material, HE_STRING_LITERAL("reflectance"), Shader_Data_Type::F32);
        *albedo_color = *(glm::vec3 *)material->pbr_metallic_roughness.base_color_factor;
        *roughness_factor = material->pbr_metallic_roughness.roughness_factor;
        *metallic_factor = material->pbr_metallic_roughness.metallic_factor;
        *reflectance = 0.04f;
        *albedo_texture_index = albedo.index;
        *normal_texture_index = normal.index;
        *orm_texture_index = metallic_roughness.index;
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

    HE_DEFER
    {
        end_temprary_memory_arena(&temprary_arena);
    };

    Ring_Queue< Scene_Node_Bundle > nodes;
    init(&nodes, 4096, &temprary_arena);

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
            scene_node->start_mesh_index = renderer_state->static_meshes.count;
            scene_node->static_mesh_count += u64_to_u32(node->mesh->primitives_count);

            for (U32 primitive_index = 0; primitive_index < node->mesh->primitives_count; primitive_index++)
            {
                cgltf_primitive *primitive = &node->mesh->primitives[primitive_index];
                HE_ASSERT(primitive->material);
                cgltf_material *material = primitive->material;

                U64 material_hash = (U64)material;
                Material_Handle material_handle = find_material(renderer_state, material_hash);

                Static_Mesh_Handle static_mesh_handle = aquire_handle(&renderer_state->static_meshes);
                Static_Mesh *static_mesh = get(&renderer_state->static_meshes, static_mesh_handle);
                static_mesh->material_handle = material_handle;

                HE_ASSERT(primitive->type == cgltf_primitive_type_triangles);

                for (U32 attribute_index = 0; attribute_index < primitive->attributes_count; attribute_index++)
                {
                    cgltf_attribute *attribute = &primitive->attributes[attribute_index];
                    HE_ASSERT(attribute->type != cgltf_attribute_type_invalid);

                    const auto *accessor = attribute->data;
                    const auto *view = accessor->buffer_view;
                    U8 *data_ptr = (U8 *)view->buffer->data;

                    switch (attribute->type)
                    {
                        case cgltf_attribute_type_position:
                        {
                            HE_ASSERT(attribute->data->type == cgltf_type_vec3);
                            HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                            position_count = u64_to_u32(attribute->data->count);
                            U64 stride = attribute->data->stride;
                            HE_ASSERT(stride == sizeof(glm::vec3));
                            positions = (glm::vec3 *)(data_ptr + view->offset + accessor->offset);
                        } break;

                        case cgltf_attribute_type_normal:
                        {
                            HE_ASSERT(attribute->data->type == cgltf_type_vec3);
                            HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                            normal_count = u64_to_u32(attribute->data->count);
                            U64 stride = attribute->data->stride;
                            HE_ASSERT(stride == sizeof(glm::vec3));
                            normals = (glm::vec3 *)(data_ptr + view->offset + accessor->offset);
                        } break;

                        case cgltf_attribute_type_texcoord:
                        {
                            HE_ASSERT(attribute->data->type == cgltf_type_vec2);
                            HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                            uv_count = u64_to_u32(attribute->data->count);
                            U64 stride = attribute->data->stride;
                            HE_ASSERT(stride == sizeof(glm::vec2));
                            uvs = (glm::vec2 *)(data_ptr + view->offset + accessor->offset);
                        } break;

                        case cgltf_attribute_type_tangent:
                        {
                            HE_ASSERT(attribute->data->type == cgltf_type_vec4);
                            HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);
                            tangent_count = u64_to_u32(attribute->data->count);
                            U64 stride = attribute->data->stride;
                            HE_ASSERT(stride == sizeof(glm::vec4));
                            tangents = (glm::vec4 *)(data_ptr + view->offset + accessor->offset);
                        } break;
                    }
                }

                // note(amer): we only support u16 indices for now.
                HE_ASSERT(primitive->indices->type == cgltf_type_scalar);
                HE_ASSERT(primitive->indices->component_type == cgltf_component_type_r_16u);
                HE_ASSERT(primitive->indices->stride == sizeof(U16));

                index_count = u64_to_u32(primitive->indices->count);
                const auto *accessor = primitive->indices;
                const auto *view = accessor->buffer_view;
                U8 *data_ptr = (U8 *)view->buffer->data;
                indices = (U16 *)(data_ptr + view->offset + accessor->offset);

                HE_ASSERT(position_count == normal_count);
                HE_ASSERT(position_count == uv_count);
                // HE_ASSERT(position_count == tangent_count); // note(amer): this fails when we load the sponza model.
                U32 vertex_count = position_count;

                Static_Mesh_Descriptor descriptor = {};
                descriptor.vertex_count = vertex_count;
                descriptor.index_count = index_count;
                descriptor.positions = positions;
                descriptor.normals = normals;
                descriptor.uvs = uvs;
                descriptor.tangents = tangents;
                descriptor.indices = indices;

                platform_lock_mutex(&renderer_state->render_commands_mutex);
                bool created = renderer->create_static_mesh(static_mesh_handle, descriptor);
                platform_unlock_mutex(&renderer_state->render_commands_mutex);
                HE_ASSERT(created);
            }
        }

        for (U32 child_node_index = 0;
            child_node_index < node->children_count;
            child_node_index++)
        {
            push(&nodes, { node->children[child_node_index], add_child_scene_node(renderer_state, scene_node) });
        }
    }

    // cgltf_free(model_data);
    // deallocate(renderer_state->transfer_allocator, buffer);
    return true;
}

Scene_Node *load_model(const String &path, Renderer *renderer, Renderer_State *renderer_state, Memory_Arena *arena)
{
    Scene_Node *root_scene_node = &renderer_state->scene_nodes[renderer_state->scene_node_count++];
    bool model_loaded = load_model(root_scene_node, path, renderer, renderer_state, arena);
    HE_ASSERT(model_loaded);
    return root_scene_node;
}

void render_scene_node(Renderer *renderer, Renderer_State *renderer_state, Scene_Node *scene_node, const glm::mat4 &parent_transform)
{
    glm::mat4 transform = parent_transform * scene_node->transform;

    for (U32 static_mesh_index = 0;
         static_mesh_index < scene_node->static_mesh_count;
         static_mesh_index++)
    {
        U32 mesh_index = scene_node->start_mesh_index + static_mesh_index;
        renderer->submit_static_mesh({ (S32)mesh_index, renderer_state->static_meshes.generations[mesh_index] }, transform);
    }

    for (Scene_Node *node = scene_node->first_child; node; node = node->next_sibling)
    {
        render_scene_node(renderer, renderer_state, node, transform);
    }
}

Material_Handle create_material(Renderer_State *renderer_state, Renderer *renderer, const Material_Descriptor &descriptor)
{
    Material_Handle material_handle = aquire_handle(&renderer_state->materials);
    Material *material = get(&renderer_state->materials, material_handle);
    Pipeline_State *pipeline_state = get(&renderer_state->pipeline_states, descriptor.pipeline_state_handle);
    Shader_Group *shader_group = get(&renderer_state->shader_groups, pipeline_state->shader_group);

    Shader_Struct *properties = nullptr;

    for (U32 shader_index = 0; shader_index < shader_group->shader_count; shader_index++)
    {
        Shader *shader = get(&renderer_state->shaders, shader_group->shaders[shader_index]);
        for (U32 struct_index = 0; struct_index < shader->struct_count; struct_index++)
        {
            Shader_Struct *shader_struct = &shader->structs[struct_index];
            if (shader_struct->name == "Material_Properties")
            {
                properties = shader_struct;
                break;
            }
        }
    }

    HE_ASSERT(properties);

    Shader_Struct_Member *last_member = &properties->members[properties->member_count - 1];
    U32 last_member_size = get_size_of_shader_data_type(last_member->data_type);
    U32 size = last_member->offset + last_member_size;

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        material->buffers[frame_index] = aquire_handle(&renderer_state->buffers);

        Buffer_Descriptor buffer_descriptor = {};
        buffer_descriptor.usage = Buffer_Usage::UNIFORM;
        buffer_descriptor.size = size;
        buffer_descriptor.is_device_local = false;

        renderer->create_buffer(material->buffers[frame_index], buffer_descriptor);
    }

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        material->bind_groups[frame_index] = aquire_handle(&renderer_state->bind_groups);

        Bind_Group_Descriptor bind_group_descriptor = {};
        bind_group_descriptor.layout = shader_group->bind_group_layouts[2]; // todo(amer): Hardcoding
        renderer->create_bind_group(material->bind_groups[frame_index], bind_group_descriptor);

        Update_Binding_Descriptor update_binding_descriptor = {};
        update_binding_descriptor.binding_number = 0;
        update_binding_descriptor.element_index = 0;
        update_binding_descriptor.count = 1;
        update_binding_descriptor.buffers = &material->buffers[frame_index];

        renderer->update_bind_group(material->bind_groups[frame_index], &update_binding_descriptor, 1);
    }

    material->pipeline_state_handle = descriptor.pipeline_state_handle;
    material->data = HE_ALLOCATE_ARRAY(&renderer_state->engine->memory.free_list_allocator, U8, size);
    material->size = size;
    material->properties = properties;

    return material_handle;
}

void destroy_material(Renderer_State *renderer_state, Renderer *renderer, Material_Handle material_handle)
{
    Material *material = get(&renderer_state->materials, material_handle);

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        renderer->destroy_buffer(material->buffers[frame_index]);
        renderer->destroy_bind_group(material->bind_groups[frame_index]);
    }

    deallocate(&renderer_state->engine->memory.free_list_allocator, material->data);
    release_handle(&renderer_state->materials, material_handle);
}

U8 *get_property(Material *material, const String &name, Shader_Data_Type data_type)
{
    Shader_Struct *properties = material->properties;
    for (U32 member_index = 0; member_index < properties->member_count; member_index++)
    {
        Shader_Struct_Member *member = &properties->members[member_index];
        if (name == member->name && member->data_type == data_type)
        {
            return material->data + member->offset;
        }
    }
    return nullptr;
}

Texture_Handle find_texture(Renderer_State *renderer_state, const String &name)
{
    for (S32 texture_index = 0; texture_index < (S32)renderer_state->textures.capacity; texture_index++)
    {
        if (!renderer_state->textures.is_allocated[texture_index])
        {
            continue;
        }

        Texture *texture = &renderer_state->textures.data[texture_index];
        if (texture->name == name)
        {
            return { texture_index, renderer_state->textures.generations[texture_index] };
        }
    }

    return Resource_Pool< Texture >::invalid_handle;
}

Material_Handle find_material(Renderer_State *renderer_state, U64 hash)
{
    for (S32 material_index = 0; material_index < (S32)renderer_state->materials.capacity; material_index++)
    {
        if (!renderer_state->materials.is_allocated[material_index])
        {
            continue;
        }

        Material *material = &renderer_state->materials.data[material_index];
        if (material->hash == hash)
        {
            return { material_index, renderer_state->materials.generations[material_index] };
        }
    }
    return Resource_Pool< Material >::invalid_handle;
}

U32 get_size_of_shader_data_type(Shader_Data_Type data_type)
{
    switch (data_type)
    {
        case Shader_Data_Type::BOOL: return 1;

        case Shader_Data_Type::S8: return 1;
        case Shader_Data_Type::S16: return 2;
        case Shader_Data_Type::S32: return 4;
        case Shader_Data_Type::S64: return 8;

        case Shader_Data_Type::U8: return 1;
        case Shader_Data_Type::U16: return 2;
        case Shader_Data_Type::U32: return 4;
        case Shader_Data_Type::U64: return 8;

        case Shader_Data_Type::F16: return 2;
        case Shader_Data_Type::F32: return 4;
        case Shader_Data_Type::F64: return 8;

        case Shader_Data_Type::VECTOR2F: return 2 * 4;
        case Shader_Data_Type::VECTOR3F: return 3 * 4;
        case Shader_Data_Type::VECTOR4F: return 4 * 4;

        case Shader_Data_Type::MATRIX3F: return 9 * 4;
        case Shader_Data_Type::MATRIX4F: return 16 * 4;

        default:
        {
            HE_ASSERT(!"unsupported type");
        } break;
    }

    return 0;
}