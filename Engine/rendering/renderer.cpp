#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"

#include "core/platform.h"
#include "core/cvars.h"
#include "core/memory.h"
#include "core/engine.h"
#include "core/file_system.h"
#include "core/job_system.h"
#include "core/logging.h"

#include "containers/string.h"
#include "containers/queue.h"

#include "assets/asset_manager.h"

#include <algorithm> // todo(amer): to be removed

#include <shaderc/shaderc.h>
#include <spirv_cross/spirv_cross_c.h>

#if HE_OS_WINDOWS
#define HE_RHI_VULKAN
#endif

#ifdef HE_RHI_VULKAN
#include "rendering/vulkan/vulkan_renderer.h"
#endif

#pragma warning(push, 0)

#include <imgui.h>

#pragma warning(pop)

#include "renderer.h"

static Renderer_State *renderer_state;
static Renderer *renderer;

bool request_renderer(RenderingAPI rendering_api, Renderer *renderer)
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
            renderer->create_shader = &vulkan_renderer_create_shader;
            renderer->destroy_shader = &vulkan_renderer_destroy_shader;
            renderer->create_pipeline_state = &vulkan_renderer_create_pipeline_state;
            renderer->destroy_pipeline_state = &vulkan_renderer_destroy_pipeline_state;
            renderer->set_bind_groups = &vulkan_renderer_set_bind_groups;
            renderer->update_bind_group = &vulkan_renderer_update_bind_group;
            renderer->create_render_pass = &vulkan_renderer_create_render_pass;
            renderer->begin_render_pass = &vulkan_renderer_begin_render_pass;
            renderer->end_render_pass = &vulkan_renderer_end_render_pass;
            renderer->destroy_render_pass = &vulkan_renderer_destroy_render_pass;
            renderer->create_frame_buffer = &vulkan_renderer_create_frame_buffer;
            renderer->destroy_frame_buffer = &vulkan_renderer_destroy_frame_buffer;
            renderer->create_semaphore = &vulkan_renderer_create_semaphore;
            renderer->get_semaphore_value = &vulkan_renderer_get_semaphore_value;
            renderer->destroy_semaphore = &vulkan_renderer_destroy_semaphore;
            renderer->destroy_upload_request = &vulkan_renderer_destroy_upload_request;
            renderer->begin_frame = &vulkan_renderer_begin_frame;
            renderer->set_viewport = &vulkan_renderer_set_viewport;
            renderer->set_vertex_buffers = &vulkan_renderer_set_vertex_buffers;
            renderer->set_index_buffer = &vulkan_renderer_set_index_buffer;
            renderer->set_pipeline_state = &vulkan_renderer_set_pipeline_state;
            renderer->draw_sub_mesh = &vulkan_renderer_draw_sub_mesh;
            renderer->end_frame = &vulkan_renderer_end_frame;
            renderer->set_vsync = &vulkan_renderer_set_vsync;
            renderer->get_texture_memory_requirements = &vulkan_renderer_get_texture_memory_requirements;
            renderer->init_imgui = &vulkan_renderer_init_imgui;
            renderer->imgui_new_frame = &vulkan_renderer_imgui_new_frame;
            renderer->imgui_add_texture = &vulkan_renderer_imgui_add_texture;
            renderer->imgui_get_texture_id = &vulkan_renderer_imgui_get_texture_id;
            renderer->imgui_render = &vulkan_renderer_imgui_render;
        } break;
#endif

        default:
        {
            result = false;
        } break;
    }

    return result;
}

bool init_renderer_state(Engine *engine)
{
    Memory_Arena *arena = get_permenent_arena();
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
    Free_List_Allocator *allocator = get_general_purpose_allocator();

    renderer_state = HE_ALLOCATE(arena, Renderer_State);
    renderer_state->engine = engine;

    bool renderer_requested = request_renderer(RenderingAPI_Vulkan, &renderer_state->renderer);
    if (!renderer_requested)
    {
        HE_LOG(Rendering, Fetal, "failed to request vulkan renderer\n");
        return false;
    }

    renderer = &renderer_state->renderer;

    bool render_commands_mutex_created = platform_create_mutex(&renderer_state->render_commands_mutex);
    HE_ASSERT(render_commands_mutex_created);


    {
        Allocator allocator = to_allocator(arena);
        init(&renderer_state->buffers, HE_MAX_BUFFER_COUNT, allocator);
        init(&renderer_state->textures, HE_MAX_TEXTURE_COUNT, allocator);
        init(&renderer_state->samplers, HE_MAX_SAMPLER_COUNT, allocator);
        init(&renderer_state->shaders, HE_MAX_SHADER_COUNT, allocator);
        init(&renderer_state->pipeline_states, HE_MAX_PIPELINE_STATE_COUNT, allocator);
        init(&renderer_state->bind_groups, HE_MAX_BIND_GROUP_COUNT, allocator);
        init(&renderer_state->render_passes, HE_MAX_RENDER_PASS_COUNT, allocator);
        init(&renderer_state->frame_buffers, HE_MAX_FRAME_BUFFER_COUNT, allocator);
        init(&renderer_state->semaphores, HE_MAX_SEMAPHORE_COUNT, allocator);
        init(&renderer_state->materials, HE_MAX_MATERIAL_COUNT, allocator);
        init(&renderer_state->static_meshes, HE_MAX_STATIC_MESH_COUNT, allocator);
        init(&renderer_state->scenes, HE_MAX_SCENE_COUNT, allocator);
        init(&renderer_state->upload_requests, HE_MAX_UPLOAD_REQUEST_COUNT, allocator);
    }

    platform_create_mutex(&renderer_state->pending_upload_requests_mutex); 
    reset(&renderer_state->pending_upload_requests);
    
    U32 &back_buffer_width = renderer_state->back_buffer_width;
    U32 &back_buffer_height = renderer_state->back_buffer_height;
    bool &triple_buffering = renderer_state->triple_buffering;
    bool &vsync = renderer_state->vsync;
    U8 &msaa_setting = (U8&)renderer_state->msaa_setting;
    U8 &anisotropic_filtering_setting = (U8&)renderer_state->anisotropic_filtering_setting;
    F32 &gamma = renderer_state->gamma;

    // default settings
    back_buffer_width = 1280;
    back_buffer_height = 720;
    msaa_setting = (U8)MSAA_Setting::X4;
    anisotropic_filtering_setting = (U8)Anisotropic_Filtering_Setting::X16;
    triple_buffering = true;
    vsync = false;
    gamma = 2.2f;

    HE_DECLARE_CVAR("renderer", back_buffer_width, CVarFlag_None);
    HE_DECLARE_CVAR("renderer", back_buffer_height, CVarFlag_None);
    HE_DECLARE_CVAR("renderer", triple_buffering, CVarFlag_None);
    HE_DECLARE_CVAR("renderer", gamma, CVarFlag_None);
    HE_DECLARE_CVAR("renderer", msaa_setting, CVarFlag_None);
    HE_DECLARE_CVAR("renderer", anisotropic_filtering_setting, CVarFlag_None);
    HE_DECLARE_CVAR("renderer", vsync, CVarFlag_None);

    renderer_state->current_frame_in_flight_index = 0;
    HE_ASSERT(renderer_state->frames_in_flight <= HE_MAX_FRAMES_IN_FLIGHT);

    if (renderer_state->triple_buffering)
    {
        renderer_state->frames_in_flight = 3;
    }
    else
    {
        renderer_state->frames_in_flight = 2;
    }

    bool renderer_inited = renderer->init(engine, renderer_state);
    if (!renderer_inited)
    {
        HE_LOG(Rendering, Fetal, "failed to initialize renderer\n");
        return false;
    }

    Sampler_Descriptor default_texture_sampler_descriptor =
    {
        .address_mode_u = Address_Mode::REPEAT,
        .address_mode_v = Address_Mode::REPEAT,
        .address_mode_w = Address_Mode::REPEAT,
        .min_filter = Filter::LINEAR,
        .mag_filter = Filter::NEAREST,
        .mip_filter = Filter::LINEAR,
        .anisotropy = get_anisotropic_filtering_value(renderer_state->anisotropic_filtering_setting)
    };
    renderer_state->default_texture_sampler = renderer_create_sampler(default_texture_sampler_descriptor);

    bool imgui_inited = init_imgui(engine);
    HE_ASSERT(imgui_inited);

    Buffer_Descriptor transfer_buffer_descriptor =
    {
        .size = HE_MEGA_BYTES(512),
        .usage = Buffer_Usage::TRANSFER
    };
    renderer_state->transfer_buffer = renderer_create_buffer(transfer_buffer_descriptor);

    Buffer *transfer_buffer = get(&renderer_state->buffers, renderer_state->transfer_buffer);
    init_free_list_allocator(&renderer_state->transfer_allocator, transfer_buffer->data, transfer_buffer->size, transfer_buffer->size);

    // default resources
    Renderer_Semaphore_Descriptor semaphore_descriptor =
    {
        .initial_value = 0
    };

    {
        U32* white_pixel_data = HE_ALLOCATE(&renderer_state->transfer_allocator, U32);
        *white_pixel_data = 0xFFFFFFFF;
        
        void *data_array[] =
        {
            white_pixel_data 
        };

        Texture_Descriptor white_pixel_descriptor =
        {
            .name = HE_STRING_LITERAL("white pixel"),
            .width = 1,
            .height = 1,
            .format = Texture_Format::R8G8B8A8_UNORM,
            .data_array = to_array_view(data_array),
            .mipmapping = false
        };

        renderer_state->white_pixel_texture = renderer_create_texture(white_pixel_descriptor);
    }

    {
        U32 *normal_pixel_data = HE_ALLOCATE(&renderer_state->transfer_allocator, U32);
        *normal_pixel_data = 0xFFFF8080; // todo(amer): endianness
        HE_ASSERT(HE_ARCH_X64);

        void *data_array[] =
        {
            normal_pixel_data
        };
        
        Texture_Descriptor normal_pixel_descriptor =
        {
            .name = HE_STRING_LITERAL("normal pixel"),
            .width = 1,
            .height = 1,
            .format = Texture_Format::R8G8B8A8_UNORM,
            .data_array = to_array_view(data_array),
            .mipmapping = false,
        };

        renderer_state->normal_pixel_texture = renderer_create_texture(normal_pixel_descriptor);
    }

    {
        U16 _indices[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35 };
        U32 index_count = HE_ARRAYCOUNT(_indices);

        glm::vec3 _positions[] = { { 1.000000f, -1.000000f, 1.000000f }, { -1.000000f, -1.000000f, -1.000000f }, { 1.000000f, -1.000000f, -1.000000f }, { -1.000000f, 1.000000f, -1.000000f }, { 0.999999f, 1.000000f, 1.000001f }, { 1.000000f, 1.000000f, -0.999999f },{ 1.000000f, 1.000000f, -0.999999f },{ 1.000000f, -1.000000f, 1.000000f },{ 1.000000f, -1.000000f, -1.000000f }, { 0.999999f, 1.000000f, 1.000001f },{ -1.000000f, -1.000000f, 1.000000f },{ 1.000000f, -1.000000f, 1.000000f },{ -1.000000f, -1.000000f, 1.000000f },{ -1.000000f, 1.000000f, -1.000000f },{ -1.000000f, -1.000000f, -1.000000f },{ 1.000000f, -1.000000f, -1.000000f },{ -1.000000f, 1.000000f, -1.000000f },{ 1.000000f, 1.000000f, -0.999999f },{ 1.000000f, -1.000000f, 1.000000f },{ -1.000000f, -1.000000f, 1.000000f },{ -1.000000f, -1.000000f, -1.000000f },{ -1.000000f, 1.000000f, -1.000000f },{ -1.000000f, 1.000000f, 1.000000f },{ 0.999999f, 1.000000f, 1.000001f },{ 1.000000f, 1.000000f, -0.999999f },{ 0.999999f, 1.000000f, 1.000001f },{ 1.000000f, -1.000000f, 1.000000f },{ 0.999999f, 1.000000f, 1.000001f },{ -1.000000f, 1.000000f, 1.000000f },{ -1.000000f, -1.000000f, 1.000000f },{ -1.000000f, -1.000000f, 1.000000f },{ -1.000000f, 1.000000f, 1.000000f },{ -1.000000f, 1.000000f, -1.000000f },{ 1.000000f, -1.000000f, -1.000000f },{ -1.000000f, -1.000000f, -1.000000f },{ -1.000000f, 1.000000f, -1.000000f } };

        U32 vertex_count = HE_ARRAYCOUNT(_positions);

        glm::vec3 _normals[] = { { -0.000000f, -1.000000f, 0.000000f },{ -0.000000f, -1.000000f, 0.000000f },{ -0.000000f, -1.000000f, 0.000000f },{ 0.000000f, 1.000000f, -0.000000f },{ 0.000000f, 1.000000f, -0.000000f },{ 0.000000f, 1.000000f, -0.000000f },{ 1.000000f, -0.000000f, -0.000000f },{ 1.000000f, -0.000000f, -0.000000f },{ 1.000000f, -0.000000f, -0.000000f },{ -0.000000f, -0.000000f, 1.000000f },{ -0.000000f, -0.000000f, 1.000000f },{ -0.000000f, -0.000000f, 1.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ 0.000000f, 0.000000f, -1.000000f },{ 0.000000f, 0.000000f, -1.000000f },{ 0.000000f, 0.000000f, -1.000000f },{ 0.000000f, -1.000000f, 0.000000f },{ 0.000000f, -1.000000f, 0.000000f },{ 0.000000f, -1.000000f, 0.000000f },{ 0.000000f, 1.000000f, 0.000000f },{ 0.000000f, 1.000000f, 0.000000f },{ 0.000000f, 1.000000f, 0.000000f },{ 1.000000f, 0.000000f, 0.000001f },{ 1.000000f, 0.000000f, 0.000001f },{ 1.000000f, 0.000000f, 0.000001f },{ -0.000000f, 0.000000f, 1.000000f },{ -0.000000f, 0.000000f, 1.000000f },{ -0.000000f, 0.000000f, 1.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ 0.000000f, 0.000000f, -1.000000f },{ 0.000000f, 0.000000f, -1.000000f },{ 0.000000f, 0.000000f, -1.000000f } };

        glm::vec2 _uvs[] = { { 0.000000f, 0.000000f },{ -1.000000f, 1.000000f },{ 0.000000f, 1.000000f },{ 0.000000f, 0.000000f },{ 1.000000f, -1.000000f },{ 1.000000f, -0.000000f },{ 1.000000f, 0.000000f },{ 0.000000f, -1.000000f },{ 1.000000f, -1.000000f },{ 1.000000f, 0.000000f },{ -0.000000f, -1.000000f },{ 1.000000f, -1.000000f },{ 0.000000f, 0.000000f },{ 1.000000f, 1.000000f },{ 1.000000f, 0.000000f },{ 0.000000f, 0.000000f },{ -1.000000f, 1.000000f },{ 0.000000f, 1.000000f },{ 0.000000f, 0.000000f },{ -1.000000f, 0.000000f },{ -1.000000f, 1.000000f },{ 0.000000f, 0.000000f },{ -0.000000f, -1.000000f },{ 1.000000f, -1.000000f },{ 1.000000f, 0.000000f },{ -0.000000f, 0.000000f },{ 0.000000f, -1.000000f },{ 1.000000f, 0.000000f },{ -0.000000f, 0.000000f },{ -0.000000f, -1.000000f },{ 0.000000f, 0.000000f },{ 0.000000f, 1.000000f },{ 1.000000f, 1.000000f },{ 0.000000f, 0.000000f },{ -1.000000f, 0.000000f },{ -1.000000f, 1.000000f } };        

        glm::vec4 _tangents[] = { { 1.000000f, -0.000000f, -0.000000f, -1.000000f },{ 1.000000f, -0.000000f, -0.000000f, -1.000000f },{ 1.000000f, -0.000000f, -0.000000f, -1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, -0.000000f, 0.000000f, 1.000000f },{ 0.000000f, 0.000000f, -1.000000f, 1.000000f },{ 0.000000f, 0.000000f, -1.000000f, 1.000000f },{ 0.000000f, 0.000000f, -1.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f },{ 1.000000f, -0.000000f, -0.000000f, -1.000000f },{ 1.000000f, 0.000000f, -0.000000f, -1.000000f },{ 1.000000f, -0.000000f, -0.000000f, -1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 0.000001f, 0.000000f, -1.000000f, 1.000000f },{ 0.000001f, 0.000000f, -1.000000f, 1.000000f },{ 0.000001f, 0.000000f, -1.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f } };

        U64 size = sizeof(U16) * (U64)index_count + (sizeof(glm::vec3) + sizeof(glm::vec3) + sizeof(glm::vec2) + sizeof(glm::vec4)) * (U64)vertex_count;
        U8 *data = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, U8, size);
        
        U16 *indices = (U16 *)data;
        copy_memory(indices, _indices, sizeof(U16) * HE_ARRAYCOUNT(_indices));

        U8 *vertex_data = data + sizeof(U16) * index_count;

        glm::vec3 *positions = (glm::vec3 *)vertex_data;
        copy_memory(positions, _positions, sizeof(glm::vec3) * HE_ARRAYCOUNT(_positions));
        
        glm::vec3 *normals = (glm::vec3 *)(vertex_data + sizeof(glm::vec3) * vertex_count);
        copy_memory(normals, _normals, sizeof(glm::vec3) * HE_ARRAYCOUNT(_normals));
        
        glm::vec2 *uvs = (glm::vec2 *)(vertex_data + (sizeof(glm::vec3) + sizeof(glm::vec3)) * vertex_count);
        copy_memory(uvs, _uvs, sizeof(glm::vec2) * HE_ARRAYCOUNT(_uvs));
        
        glm::vec4 *tangents = (glm::vec4 *)(vertex_data + (sizeof(glm::vec3) + sizeof(glm::vec2) + sizeof(glm::vec3)) * vertex_count);
        copy_memory(tangents, _tangents, sizeof(glm::vec4) * HE_ARRAYCOUNT(_tangents));

        Dynamic_Array< Sub_Mesh > sub_meshes;
        init(&sub_meshes, 1, 1);

        Sub_Mesh &sub_mesh = sub_meshes[0];

        sub_mesh.vertex_offset = 0;
        sub_mesh.index_offset = 0;

        sub_mesh.index_count = index_count;
        sub_mesh.vertex_count = vertex_count;

        sub_mesh.material_asset = 0;

        void *data_array[] = { data };

        Static_Mesh_Descriptor cube_static_mesh =
        {
            .name = HE_STRING_LITERAL("cube"),
            .data_array = to_array_view(data_array),

            .indices = indices,
            .index_count = index_count,
            
            .vertex_count = vertex_count,
            .positions = positions,
            .normals = normals,
            .uvs = uvs,
            .tangents = tangents,
            
            .sub_meshes = sub_meshes
        };

        renderer_state->default_static_mesh = renderer_create_static_mesh(cube_static_mesh);
    }

    init(&renderer_state->render_graph);

    {
        auto render = [](Renderer *renderer, Renderer_State *renderer_state)
        {
            Asset_Handle skybox_material_asset = { .uuid = renderer_state->scene_data.skybox_material_asset };

            if (is_asset_handle_valid(skybox_material_asset) && is_asset_loaded(skybox_material_asset))
            {
                Material_Handle skybox_material = get_asset_handle_as<Material>(skybox_material_asset);
                renderer_use_material(skybox_material);

                Static_Mesh_Handle static_mesh_handle = renderer_state->default_static_mesh;
                Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

                Buffer_Handle vertex_buffers[] =
                {
                    static_mesh->positions_buffer,
                    static_mesh->normals_buffer,
                    static_mesh->uvs_buffer,
                    static_mesh->tangents_buffer
                };

                U64 offsets[] = { 0, 0, 0, 0 };

                renderer->set_vertex_buffers(to_array_view(vertex_buffers), to_array_view(offsets));
                renderer->set_index_buffer(static_mesh->indices_buffer, 0);

                U32 object_data_index = renderer_state->object_data_count++;
                Shader_Object_Data *object_data = &renderer_state->object_data_base[object_data_index];
                object_data->model = get_world_matrix(get_identity_transform());
                renderer->draw_sub_mesh(static_mesh_handle, object_data_index, 0);
            }

            Render_Pass_Handle opaque_pass = get_render_pass(&renderer_state->render_graph, HE_STRING_LITERAL("opaque"));
            Asset_Handle model_asset_handle = { .uuid = renderer_state->scene_data.model_asset };
            if (is_asset_handle_valid(model_asset_handle) && is_asset_loaded(model_asset_handle))
            {
                Model *model = get_asset_as<Model>(model_asset_handle);
                for (U32 node_index = 0; node_index < model->node_count; node_index++)
                {
                    Scene_Node *node = &model->nodes[node_index];

                    if (!node->has_mesh)
                    {
                        continue;
                    }

                    Asset_Handle static_mesh_asset = { .uuid = node->mesh.static_mesh_asset };
                    if (is_asset_handle_valid(static_mesh_asset))
                    {
                        Static_Mesh_Handle static_mesh_handle = renderer_state->default_static_mesh;

                        if (!is_asset_loaded(static_mesh_asset))
                        {
                            aquire_asset(static_mesh_asset);
                        }
                        else
                        {
                            static_mesh_handle = get_asset_handle_as<Static_Mesh>(static_mesh_asset);
                        }

                        Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);
                        if (!static_mesh->is_uploaded_to_gpu)
                        {
                            continue;
                        }

                        HE_ASSERT(renderer_state->object_data_count < HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT);
                        U32 object_data_index = renderer_state->object_data_count++;
                        Shader_Object_Data *object_data = &renderer_state->object_data_base[object_data_index];
                        object_data->model = get_world_matrix(node->transform);

                        Buffer_Handle vertex_buffers[] =
                        {
                            static_mesh->positions_buffer,
                            static_mesh->normals_buffer,
                            static_mesh->uvs_buffer,
                            static_mesh->tangents_buffer
                        };

                        U64 offsets[] = { 0, 0, 0, 0 };

                        renderer->set_vertex_buffers(to_array_view(vertex_buffers), to_array_view(offsets));
                        renderer->set_index_buffer(static_mesh->indices_buffer, 0);

                        Dynamic_Array< Sub_Mesh > &sub_meshes = static_mesh->sub_meshes;
                        for (U32 sub_mesh_index = 0; sub_mesh_index < sub_meshes.count; sub_mesh_index++)
                        {
                            Sub_Mesh *sub_mesh = &sub_meshes[sub_mesh_index];

                            Asset_Handle material_asset = { .uuid = sub_mesh->material_asset };
                            Material_Handle material_handle = renderer_state->default_material;
                            if (is_asset_handle_valid(material_asset))
                            {
                                if (!is_asset_loaded(material_asset))
                                {
                                    aquire_asset(material_asset);
                                }
                                else
                                {
                                    material_handle = get_asset_handle_as<Material>(material_asset);
                                }
                            }

                            Material *material = renderer_get_material(material_handle);
                            Pipeline_State *pipeline_state = renderer_get_pipeline_state(material->pipeline_state_handle);

                            if (pipeline_state->descriptor.render_pass == opaque_pass)
                            {
                                renderer_use_material(material_handle);
                                renderer->draw_sub_mesh(static_mesh_handle, object_data_index, sub_mesh_index);
                            }
                        }

                    }
                }
            }

            render_scene(renderer_state->scene_data.scene_handle);
        };

        Render_Target_Info render_targets[] =
        {
            {
                .name = "multisample_main",
                .operation = Attachment_Operation::CLEAR,
                .info =
                {
                    .format = Texture_Format::R8G8B8A8_UNORM,
                    .resizable_sample = true,
                    .resizable = true,
                    .scale_x = 1.0f,
                    .scale_y = 1.0f,
                }
            },
            {
                .name = "depth",
                .operation = Attachment_Operation::CLEAR,
                .info =
                {
                    .format = Texture_Format::DEPTH_F32_STENCIL_U8,
                    .resizable_sample = true,
                    .resizable = true,
                    .scale_x = 1.0f,
                    .scale_y = 1.0f,
                }
            }
        };

        Render_Graph_Node &node = add_node(&renderer_state->render_graph, "opaque", to_array_view(render_targets), render);
        add_resolve_color_attachment(&renderer_state->render_graph, &node, "multisample_main", "main");
        node.clear_values[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
        node.clear_values[1].depth = 1.0f;
    }

    {
        auto render = [](Renderer *renderer, Renderer_State *renderer_state)
        {
            renderer->imgui_render();
        };

        Render_Target_Info render_targets[] =
        {
            {
                .name = "main",
                .operation = Attachment_Operation::LOAD
            }
        };

        add_node(&renderer_state->render_graph, "ui", to_array_view(render_targets), render);
    }

    set_presentable_attachment(&renderer_state->render_graph, "main");
    
    bool compiled = compile(&renderer_state->render_graph, renderer, renderer_state);
    if (!compiled)
    {
        HE_LOG(Rendering, Fetal, "failed to compile render graph\n");
        return false;
    }

    invalidate(&renderer_state->render_graph, renderer, renderer_state);

    Sampler_Descriptor default_cubemap_sampler_descriptor =
    {
        .address_mode_u = Address_Mode::CLAMP,
        .address_mode_v = Address_Mode::CLAMP,
        .address_mode_w = Address_Mode::CLAMP,

        .min_filter = Filter::LINEAR,
        .mag_filter = Filter::LINEAR,
        .mip_filter = Filter::LINEAR,

        .anisotropy = 1
    };

    renderer_state->default_cubemap_sampler = renderer_create_sampler(default_cubemap_sampler_descriptor); 

    {
        Allocator allocator = to_allocator(scratch_memory.arena);
        Read_Entire_File_Result result = read_entire_file(HE_STRING_LITERAL("shaders/default.glsl"), allocator);
        String default_shader_source = { .count = result.size, .data = (const char *)result.data };
        
        Shader_Compilation_Result default_shader_compilation_result = renderer_compile_shader(default_shader_source, HE_STRING_LITERAL("shaders")); // todo(amer): @Leak
        HE_ASSERT(default_shader_compilation_result.success);

        Shader_Descriptor default_shader_descriptor =
        {
            .name = HE_STRING_LITERAL("default"),
            .compilation_result = &default_shader_compilation_result
        };

        renderer_state->default_shader = renderer_create_shader(default_shader_descriptor);
        HE_ASSERT(is_valid_handle(&renderer_state->shaders, renderer_state->default_shader));
        renderer_destroy_shader_compilation_result(&default_shader_compilation_result);
        
        Pipeline_State_Descriptor default_pipeline_state_descriptor =
        {
            .settings =
            {
                .cull_mode = Cull_Mode::BACK,
                .front_face = Front_Face::COUNTER_CLOCKWISE,
                .fill_mode = Fill_Mode::SOLID,
                .sample_shading = true,
            },
            .shader = renderer_state->default_shader,
            .render_pass = get_render_pass(&renderer_state->render_graph, HE_STRING_LITERAL("opaque")), // todo(amer): we should not depend on the render graph here...
        };

        renderer_state->default_pipeline = renderer_create_pipeline_state(default_pipeline_state_descriptor);
        HE_ASSERT(is_valid_handle(&renderer_state->pipeline_states, renderer_state->default_pipeline));

        Material_Descriptor default_material_descriptor =
        {
            .pipeline_state_handle = renderer_state->default_pipeline,
        };

        renderer_state->default_material = renderer_create_material(default_material_descriptor);
        HE_ASSERT(is_valid_handle(&renderer_state->materials, renderer_state->default_material));
        
        set_property(renderer_state->default_material, HE_STRING_LITERAL("debug_texture_index"), { .u32 = (U32)renderer_state->white_pixel_texture.index });
        set_property(renderer_state->default_material, HE_STRING_LITERAL("debug_color"), { .v3 = { 1.0f, 0.0f, 1.0f }});

        Shader *default_shader = get(&renderer_state->shaders, renderer_state->default_shader);

        Shader_Struct *globals_struct = renderer_find_shader_struct(renderer_state->default_shader, HE_STRING_LITERAL("Globals"));
        HE_ASSERT(globals_struct);

        Bind_Group_Descriptor per_frame_bind_group_descriptor =
        {
            .shader = renderer_state->default_shader,
            .group_index = HE_PER_FRAME_BIND_GROUP_INDEX
        };

        Bind_Group_Descriptor per_render_pass_bind_group_descriptor =
        {
            .shader = renderer_state->default_shader,
            .group_index = HE_PER_PASS_BIND_GROUP_INDEX
        };

        for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
        {
            Buffer_Descriptor globals_uniform_buffer_descriptor =
            {
                .size = globals_struct->size,
                .usage = Buffer_Usage::UNIFORM,
            };
            renderer_state->globals_uniform_buffers[frame_index] = renderer_create_buffer(globals_uniform_buffer_descriptor);

            Buffer_Descriptor object_data_storage_buffer_descriptor =
            {
                .size = sizeof(Shader_Object_Data) * HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT,
                .usage = Buffer_Usage::STORAGE_CPU_SIDE,
            };
            renderer_state->object_data_storage_buffers[frame_index] = renderer_create_buffer(object_data_storage_buffer_descriptor);

            renderer_state->per_frame_bind_groups[frame_index] = renderer_create_bind_group(per_frame_bind_group_descriptor);
            renderer_state->per_render_pass_bind_groups[frame_index] = renderer_create_bind_group(per_render_pass_bind_group_descriptor);
        }
    }

    return true;
}

void deinit_renderer_state()
{
    renderer->wait_for_gpu_to_finish_all_work();

    for (auto it = iterator(&renderer_state->buffers); next(&renderer_state->buffers, it);)
    {
        renderer->destroy_buffer(it);
    }

    for (auto it = iterator(&renderer_state->textures); next(&renderer_state->textures, it);)
    {
        renderer->destroy_texture(it);
    }

    for (auto it = iterator(&renderer_state->samplers); next(&renderer_state->samplers, it);)
    {
        renderer->destroy_sampler(it);
    }

    for (auto it = iterator(&renderer_state->shaders); next(&renderer_state->shaders, it);)
    {
        renderer->destroy_shader(it);
    }

    for (auto it = iterator(&renderer_state->frame_buffers); next(&renderer_state->frame_buffers, it);)
    {
        renderer->destroy_frame_buffer(it);
    }

    for (auto it = iterator(&renderer_state->render_passes); next(&renderer_state->render_passes, it);)
    {
        renderer->destroy_render_pass(it);
    }

    for (auto it = iterator(&renderer_state->pipeline_states); next(&renderer_state->pipeline_states, it);)
    {
        renderer->destroy_pipeline_state(it);
    }

    for (auto it = iterator(&renderer_state->semaphores); next(&renderer_state->semaphores, it);)
    {
        renderer->destroy_semaphore(it);
    }

    renderer->deinit();

    platform_shutdown_imgui();
    ImGui::DestroyContext();
}


void renderer_on_resize(U32 width, U32 height)
{
    if (width == 0 || height == 0)
    {
        return;
    }

    if (renderer_state)
    {
        renderer_state->back_buffer_width = width;
        renderer_state->back_buffer_height = height;

        if (renderer)
        {
            renderer->on_resize(width, height);
        }

        renderer->wait_for_gpu_to_finish_all_work();
        invalidate(&renderer_state->render_graph, renderer, renderer_state);
    }
}

void renderer_wait_for_gpu_to_finish_all_work()
{
    renderer->wait_for_gpu_to_finish_all_work();
}

//
// Buffers
//

Buffer_Handle renderer_create_buffer(const Buffer_Descriptor &descriptor)
{
    Buffer_Handle buffer_handle = aquire_handle(&renderer_state->buffers);
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_buffer(buffer_handle, descriptor);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);

    Buffer *buffer = &renderer_state->buffers.data[buffer_handle.index];
    buffer->usage = descriptor.usage;
    buffer->size = descriptor.size;

    return buffer_handle;
}

Buffer* renderer_get_buffer(Buffer_Handle buffer_handle)
{
    return get(&renderer_state->buffers, buffer_handle);
}

void renderer_destroy_buffer(Buffer_Handle &buffer_handle)
{
    renderer->destroy_buffer(buffer_handle);
    release_handle(&renderer_state->buffers, buffer_handle);
    buffer_handle = Resource_Pool< Buffer >::invalid_handle;
}

//
// Textures
//
Texture_Handle renderer_create_texture(const Texture_Descriptor &descriptor)
{
    HE_ASSERT(descriptor.data_array.count <= HE_MAX_UPLOAD_REQUEST_ALLOCATION_COUNT);

    Texture_Handle texture_handle = aquire_handle(&renderer_state->textures);
    Texture *texture = renderer_get_texture(texture_handle);

    if (descriptor.name.data)
    {
        texture->name = copy_string(descriptor.name, to_allocator(get_general_purpose_allocator()));
    }

    Upload_Request_Handle upload_request_handle = Resource_Pool< Upload_Request >::invalid_handle;
    texture->is_uploaded_to_gpu = true;

    if (descriptor.data_array.count)
    {
        texture->is_uploaded_to_gpu = false;

        Upload_Request_Descriptor upload_request_descriptor =
        {
            .name = texture->name,
            .is_uploaded = &texture->is_uploaded_to_gpu
        };

        upload_request_handle = renderer_create_upload_request(upload_request_descriptor);
        Upload_Request *upload_request = renderer_get_upload_request(upload_request_handle);
        upload_request->texture = texture_handle;
        copy(&upload_request->allocations_in_transfer_buffer, descriptor.data_array);
    }

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_texture(texture_handle, descriptor, upload_request_handle);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);

    if (descriptor.data_array.count)
    {
        renderer_add_pending_upload_request(upload_request_handle);
    }

    texture->width = descriptor.width;
    texture->height = descriptor.height;
    texture->is_attachment = descriptor.is_attachment;
    texture->is_cubemap = descriptor.is_cubemap;
    texture->format = descriptor.format;
    texture->sample_count = descriptor.sample_count;
    texture->alias = descriptor.alias;
    return texture_handle;
}

Texture* renderer_get_texture(Texture_Handle texture_handle)
{
    return get(&renderer_state->textures, texture_handle);
}

void renderer_destroy_texture(Texture_Handle &texture_handle)
{
    HE_ASSERT(texture_handle != renderer_state->white_pixel_texture);
    HE_ASSERT(texture_handle != renderer_state->normal_pixel_texture);

    Texture *texture = renderer_get_texture(texture_handle);

    if (texture->name.data)
    {
        deallocate(get_general_purpose_allocator(), (void *)texture->name.data);
    }

    texture->name = HE_STRING_LITERAL("");
    texture->width = 0;
    texture->height = 0;
    texture->sample_count = 1;
    texture->size = 0;
    texture->alignment = 0;
    texture->is_attachment = false;
    texture->is_cubemap = false;
    texture->alias = Resource_Pool<Texture>::invalid_handle;

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->destroy_texture(texture_handle);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);

    release_handle(&renderer_state->textures, texture_handle);
    texture_handle = Resource_Pool< Texture >::invalid_handle;
}

//
// Samplers
//

Sampler_Handle renderer_create_sampler(const Sampler_Descriptor &descriptor)
{
    Sampler_Handle sampler_handle = aquire_handle(&renderer_state->samplers);

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_sampler(sampler_handle, descriptor);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);

    Sampler *sampler = &renderer_state->samplers.data[sampler_handle.index];
    sampler->descriptor = descriptor;
    return sampler_handle;
}

Sampler* renderer_get_sampler(Sampler_Handle sampler_handle)
{
    return get(&renderer_state->samplers, sampler_handle);
}

void renderer_destroy_sampler(Sampler_Handle &sampler_handle)
{
    renderer->destroy_sampler(sampler_handle);
    release_handle(&renderer_state->samplers, sampler_handle);
    sampler_handle = Resource_Pool< Sampler >::invalid_handle;
}

//
// Shaders
//

static shaderc_shader_kind shader_stage_to_shaderc_kind(Shader_Stage stage)
{
    switch (stage)
    {
        case Shader_Stage::VERTEX: return shaderc_vertex_shader;
        case Shader_Stage::FRAGMENT: return shaderc_fragment_shader;

        default:
        {
            HE_ASSERT(!"unsupported stage");
        } break;
    }

    return shaderc_vertex_shader;
}

struct Shaderc_UserData
{
    Allocator allocator;
    String include_path;
};

shaderc_include_result *shaderc_include_resolve(void *user_data, const char *requested_source, int type, const char *requesting_source, size_t include_depth)
{
    Shaderc_UserData *ud = (Shaderc_UserData *)user_data;
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
    String source = HE_STRING(requested_source);
    String path = format_string(scratch_memory.arena, "%.*s/%.*s", HE_EXPAND_STRING(ud->include_path), HE_EXPAND_STRING(source));
    Read_Entire_File_Result file_result = read_entire_file(path, ud->allocator);
    HE_ASSERT(file_result.success);
    shaderc_include_result *result = HE_ALLOCATOR_ALLOCATE(&ud->allocator, shaderc_include_result);
    result->source_name = requested_source;
    result->source_name_length = string_length(requested_source);
    result->user_data = ud;
    result->content = (const char *)file_result.data;
    result->content_length = file_result.size;
    return result;
}

void shaderc_include_result_release(void *user_data, shaderc_include_result *include_result)
{
    Shaderc_UserData *ud = (Shaderc_UserData *)user_data;
    HE_ALLOCATOR_DEALLOCATE(&ud->allocator, (void *)include_result->content);
    HE_ALLOCATOR_DEALLOCATE(&ud->allocator, include_result);
}

Shader_Compilation_Result renderer_compile_shader(String source, String include_path)
{
    static shaderc_compiler_t compiler = shaderc_compiler_initialize();
    static shaderc_compile_options_t options = shaderc_compile_options_initialize();

    Free_List_Allocator *allocator = get_general_purpose_allocator();
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    Shaderc_UserData *shaderc_userdata = HE_ALLOCATE(allocator, Shaderc_UserData);
    shaderc_userdata->allocator = to_allocator(allocator);
    shaderc_userdata->include_path = include_path;

    HE_DEFER
    {
        deallocate(allocator, shaderc_userdata);
    };

    shaderc_compile_options_set_include_callbacks(options, shaderc_include_resolve, shaderc_include_result_release, shaderc_userdata);
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    shaderc_compile_options_set_generate_debug_info(options);
    shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);
    shaderc_compile_options_set_auto_map_locations(options, true);

    static String shader_stage_signature[(U32)Shader_Stage::COUNT];
    shader_stage_signature[(U32)Shader_Stage::VERTEX] = HE_STRING_LITERAL("vertex");
    shader_stage_signature[(U32)Shader_Stage::FRAGMENT] = HE_STRING_LITERAL("fragment");

    String sources[(U32)Shader_Stage::COUNT] = {};

    Shader_Compilation_Result compilation_result = {};
    
    String str = source;
    S32 last_shader_stage_index = -1;
    
    String type_literal = HE_STRING_LITERAL("type");
    U64 search_offset = 0;

    while (true)
    {
        S64 hash_symbol_index = find_first_char_from_left(str, HE_STRING_LITERAL("#"), search_offset);
        if (hash_symbol_index == -1)
        {
            break;
        }
        
        String str_after_index = sub_string(str, hash_symbol_index + 1);
        if (!starts_with(str_after_index, type_literal))
        {
            search_offset = hash_symbol_index + 1;
            continue;
        }

        search_offset = 0;

        if (last_shader_stage_index != -1)
        {
            sources[last_shader_stage_index].count = hash_symbol_index;
        }

        str = sub_string(str, hash_symbol_index + type_literal.count + 1);
        String whitespace = HE_STRING_LITERAL(" \n\r\f\t\v");
        str = eat_chars(str, whitespace);

        for (U32 i = 0; i < (U32)Shader_Stage::COUNT; i++)
        {
            if (!starts_with(str, shader_stage_signature[i]))
            {
                continue;
            }
            HE_ASSERT(sources[i].count == 0);

            str = advance(str, shader_stage_signature[i].count);
            sources[i] = str;
            last_shader_stage_index = (S32)i;
            break;                
        }    
    }
    
    for (U32 stage_index = 0; stage_index < (U32)Shader_Stage::COUNT; stage_index++)
    {
        if (!sources[stage_index].count)
        {
            continue;
        }

        // shaderc requires a string to be null-terminated
        String source = format_string(scratch_memory.arena, "%.*s", HE_EXPAND_STRING(sources[stage_index]));

        shaderc_shader_kind kind = shader_stage_to_shaderc_kind((Shader_Stage)stage_index);
        shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, source.data, source.count, kind, include_path.data, "main", options);
        
        HE_DEFER
        {
            shaderc_result_release(result);
        };

        shaderc_compilation_status status = shaderc_result_get_compilation_status(result);
        if (status != shaderc_compilation_status_success)
        {
            HE_LOG(Resource, Fetal, "%s\n", shaderc_result_get_error_message(result));
            
            for (U32 i = 0; i < stage_index; i++)
            {
                if (compilation_result.stages[stage_index].count)
                {
                    deallocate(allocator, (void *)compilation_result.stages[stage_index].data);
                }
            }

            return { .success = false };
        }

        const char *data = shaderc_result_get_bytes(result);
        U64 size = shaderc_result_get_length(result);
        String blob = { .count = size, .data = data };
        compilation_result.stages[stage_index] = copy_string(blob, to_allocator(allocator));
    }

    compilation_result.success = true;
    return compilation_result;
}

void renderer_destroy_shader_compilation_result(Shader_Compilation_Result *result)
{
    Free_List_Allocator *allocator = get_general_purpose_allocator();

    for (U32 stage_index = 0; stage_index < (U32)Shader_Stage::COUNT; stage_index++)
    {
        if (!result->stages[stage_index].count)
        {
            continue;
        }

        deallocate(allocator, (void *)result->stages[stage_index].data);
        result->stages[stage_index] = {};
    }
}

Shader_Handle renderer_create_shader(const Shader_Descriptor &descriptor)
{
    HE_ASSERT(descriptor.compilation_result->success);
    Shader_Handle shader_handle = aquire_handle(&renderer_state->shaders);
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_shader(shader_handle, descriptor);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
    return shader_handle;
}

Shader* renderer_get_shader(Shader_Handle shader_handle)
{
    return get(&renderer_state->shaders, shader_handle);
}

Shader_Struct *renderer_find_shader_struct(Shader_Handle shader_handle, String name)
{
    Shader *shader = renderer_get_shader(shader_handle);
    for (U32 struct_index = 0; struct_index < shader->struct_count; struct_index++)
    {
        Shader_Struct *shader_struct = &shader->structs[struct_index];
        if (shader_struct->name == name)
        {
            return shader_struct;
        }
    }
    return nullptr;
}

void renderer_destroy_shader(Shader_Handle &shader_handle)
{
    HE_ASSERT(shader_handle != renderer_state->default_shader);

    // todo(amer): can we make the shader reflection data a one memory block to free it in one step...
    Free_List_Allocator *allocator = get_general_purpose_allocator();
    Shader *shader = renderer_get_shader(shader_handle);

    for (U32 struct_index = 0; struct_index < shader->struct_count; struct_index++)
    {
        Shader_Struct *shader_struct = &shader->structs[struct_index];
        deallocate(allocator, shader_struct->members);
    }

    deallocate(allocator, shader->structs);

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->destroy_shader(shader_handle);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);

    release_handle(&renderer_state->shaders, shader_handle);
    shader_handle = Resource_Pool< Shader >::invalid_handle;
}

//
// Bind Groups
//

Bind_Group_Handle renderer_create_bind_group(const Bind_Group_Descriptor &descriptor)
{
    Bind_Group_Handle bind_group_handle = aquire_handle(&renderer_state->bind_groups);
    Bind_Group *bind_group = &renderer_state->bind_groups.data[bind_group_handle.index];
    bind_group->shader = descriptor.shader;
    bind_group->group_index = descriptor.group_index;
    return bind_group_handle;
}

Bind_Group *renderer_get_bind_group(Bind_Group_Handle bind_group_handle)
{
    return get(&renderer_state->bind_groups, bind_group_handle);
}

void renderer_update_bind_group(Bind_Group_Handle bind_group_handle, const Array_View< Update_Binding_Descriptor > &update_binding_descriptors)
{
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->update_bind_group(bind_group_handle, update_binding_descriptors);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
}

void renderer_destroy_bind_group(Bind_Group_Handle &bind_group_handle)
{
    release_handle(&renderer_state->bind_groups, bind_group_handle);
    bind_group_handle = Resource_Pool< Bind_Group >::invalid_handle;
}

//
// Pipeline States
//

Pipeline_State_Handle renderer_create_pipeline_state(const Pipeline_State_Descriptor &descriptor)
{
    Pipeline_State_Handle pipeline_state_handle = aquire_handle(&renderer_state->pipeline_states);
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_pipeline_state(pipeline_state_handle, descriptor);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);

    Pipeline_State *pipeline_state = &renderer_state->pipeline_states.data[pipeline_state_handle.index];
    pipeline_state->descriptor = descriptor;

    return pipeline_state_handle;
}

Pipeline_State* renderer_get_pipeline_state(Pipeline_State_Handle pipeline_state_handle)
{
    return get(&renderer_state->pipeline_states, pipeline_state_handle);
}

void renderer_destroy_pipeline_state(Pipeline_State_Handle &pipeline_state_handle)
{
    renderer->destroy_pipeline_state(pipeline_state_handle);
    release_handle(&renderer_state->pipeline_states, pipeline_state_handle);
    pipeline_state_handle = Resource_Pool< Pipeline_State >::invalid_handle;
}

//
// Render Passes
//

Render_Pass_Handle renderer_create_render_pass(const Render_Pass_Descriptor &descriptor)
{
    Render_Pass_Handle render_pass_handle = aquire_handle(&renderer_state->render_passes);
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_render_pass(render_pass_handle, descriptor);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
    return render_pass_handle;
}

Render_Pass *renderer_get_render_pass(Render_Pass_Handle render_pass_handle)
{
    return get(&renderer_state->render_passes, render_pass_handle);
}

void renderer_destroy_render_pass(Render_Pass_Handle &render_pass_handle)
{
    renderer->destroy_render_pass(render_pass_handle);
    release_handle(&renderer_state->render_passes, render_pass_handle);
    render_pass_handle = Resource_Pool< Render_Pass >::invalid_handle;
}

//
// Frame Buffers
//

Frame_Buffer_Handle renderer_create_frame_buffer(const Frame_Buffer_Descriptor &descriptor)
{
    Frame_Buffer_Handle frame_buffer_handle = aquire_handle(&renderer_state->frame_buffers);
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_frame_buffer(frame_buffer_handle, descriptor);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
    return frame_buffer_handle;
}

Frame_Buffer *renderer_get_frame_buffer(Frame_Buffer_Handle frame_buffer_handle)
{
    return get(&renderer_state->frame_buffers, frame_buffer_handle);
}

void renderer_destroy_frame_buffer(Frame_Buffer_Handle &frame_buffer_handle)
{
    renderer->destroy_frame_buffer(frame_buffer_handle);
    release_handle(&renderer_state->frame_buffers, frame_buffer_handle);
    frame_buffer_handle = Resource_Pool< Frame_Buffer >::invalid_handle;
}

//
// Semaphores
//
Semaphore_Handle renderer_create_semaphore(const Renderer_Semaphore_Descriptor &descriptor)
{
    Semaphore_Handle semaphore_handle = aquire_handle(&renderer_state->semaphores);
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_semaphore(semaphore_handle, descriptor);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
    return semaphore_handle;
}

Renderer_Semaphore* renderer_get_semaphore(Semaphore_Handle semaphore_handle)
{
    return get(&renderer_state->semaphores, semaphore_handle);
}

U64 renderer_get_semaphore_value(Semaphore_Handle semaphore_handle)
{
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    U64 value = renderer->get_semaphore_value(semaphore_handle);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
    return value;
}

void renderer_destroy_semaphore(Semaphore_Handle &semaphore_handle)
{
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->destroy_semaphore(semaphore_handle);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
    release_handle(&renderer_state->semaphores, semaphore_handle);
    semaphore_handle = Resource_Pool< Renderer_Semaphore >::invalid_handle;
}

//
// Static Meshes
//

Static_Mesh_Handle renderer_create_static_mesh(const Static_Mesh_Descriptor &descriptor)
{
    Static_Mesh_Handle static_mesh_handle = aquire_handle(&renderer_state->static_meshes);
    Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

    if (descriptor.name.data)
    {
        static_mesh->name = copy_string(descriptor.name, to_allocator(get_general_purpose_allocator()));
    }

    Buffer_Descriptor position_buffer_descriptor =
    {
        .size = descriptor.vertex_count * sizeof(glm::vec3),
        .usage = Buffer_Usage::VERTEX
    };

    static_mesh->positions_buffer = renderer_create_buffer(position_buffer_descriptor);

    Buffer_Descriptor normal_buffer_descriptor =
    {
        .size = descriptor.vertex_count * sizeof(glm::vec3),
        .usage = Buffer_Usage::VERTEX
    };

    static_mesh->normals_buffer = renderer_create_buffer(normal_buffer_descriptor);

    Buffer_Descriptor uv_buffer_descriptor =
    {
        .size = descriptor.vertex_count * sizeof(glm::vec2),
        .usage = Buffer_Usage::VERTEX
    };

    static_mesh->uvs_buffer = renderer_create_buffer(uv_buffer_descriptor);

    Buffer_Descriptor tangent_buffer_descriptor =
    {
        .size = descriptor.vertex_count * sizeof(glm::vec4),
        .usage = Buffer_Usage::VERTEX
    };

    static_mesh->tangents_buffer = renderer_create_buffer(tangent_buffer_descriptor);

    Buffer_Descriptor index_buffer_descriptor =
    {
        .size = descriptor.index_count * sizeof(U16),
        .usage = Buffer_Usage::INDEX
    };

    static_mesh->indices_buffer = renderer_create_buffer(index_buffer_descriptor);

    static_mesh->vertex_count = descriptor.vertex_count;
    static_mesh->index_count = descriptor.index_count;
    static_mesh->sub_meshes = descriptor.sub_meshes;
    static_mesh->is_uploaded_to_gpu = false;

    Upload_Request_Descriptor upload_request_descriptor =
    {
        .name = static_mesh->name,
        .is_uploaded = &static_mesh->is_uploaded_to_gpu
    };

    Upload_Request_Handle upload_request_handle = renderer_create_upload_request(upload_request_descriptor);
    Upload_Request *upload_request = renderer_get_upload_request(upload_request_handle);

    for (U32 i = 0; i < descriptor.data_array.count; i++)
    {
        append(&upload_request->allocations_in_transfer_buffer, descriptor.data_array[i]);
    }

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_static_mesh(static_mesh_handle, descriptor, upload_request_handle);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
    
    renderer_add_pending_upload_request(upload_request_handle);
    return static_mesh_handle;
}

Static_Mesh *renderer_get_static_mesh(Static_Mesh_Handle static_mesh_handle)
{
    return get(&renderer_state->static_meshes, static_mesh_handle);
}

void renderer_destroy_static_mesh(Static_Mesh_Handle &static_mesh_handle)
{
    Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

    if (static_mesh->name.data)
    {
        deallocate(get_general_purpose_allocator(), (void *)static_mesh->name.data);
    }

    renderer_destroy_buffer(static_mesh->positions_buffer);
    renderer_destroy_buffer(static_mesh->normals_buffer);
    renderer_destroy_buffer(static_mesh->uvs_buffer);
    renderer_destroy_buffer(static_mesh->tangents_buffer);
    renderer_destroy_buffer(static_mesh->indices_buffer);

    deinit(&static_mesh->sub_meshes);

    static_mesh->vertex_count = 0;
    static_mesh->index_count = 0;

    release_handle(&renderer_state->static_meshes, static_mesh_handle);
    static_mesh_handle = Resource_Pool< Static_Mesh >::invalid_handle;
}

//
// Materials
//

Material_Handle renderer_create_material(const Material_Descriptor &descriptor)
{
    Material_Handle material_handle = aquire_handle(&renderer_state->materials);
    Material *material = get(&renderer_state->materials, material_handle);

    if (descriptor.name.count)
    {
        material->name = copy_string(descriptor.name, to_allocator(get_general_purpose_allocator()));
    }

    Pipeline_State *pipeline_state = get(&renderer_state->pipeline_states, descriptor.pipeline_state_handle);
    Shader *shader = get(&renderer_state->shaders, pipeline_state->descriptor.shader);
    Shader_Struct *properties = renderer_find_shader_struct(pipeline_state->descriptor.shader, HE_STRING_LITERAL("Material"));
    HE_ASSERT(properties);

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        Buffer_Descriptor material_buffer_descriptor =
        {
            .size = properties->size,
            .usage = Buffer_Usage::UNIFORM
        };
        
        Buffer_Handle buffer_handle = renderer_create_buffer(material_buffer_descriptor);
        material->buffers[frame_index] = buffer_handle;

        Bind_Group_Descriptor bind_group_descriptor =
        {
            .shader = pipeline_state->descriptor.shader,
            .group_index = HE_PER_OBJECT_BIND_GROUP_INDEX
        };

        material->bind_groups[frame_index] = renderer_create_bind_group(bind_group_descriptor);
    }

    init(&material->properties, properties->member_count);

    static constexpr const char* texture_postfixes[] =
    {
        "texture",
        "cubemap"
    };

    for (U32 property_index = 0; property_index < properties->member_count; property_index++)
    {
        Shader_Struct_Member *member = &properties->members[property_index];

        Material_Property *property = &material->properties[property_index];
        property->name = member->name;
        property->data_type = member->data_type;
        property->offset_in_buffer = member->offset;

        bool has_texture_postfix = false;

        for (U32 i = 0; i < HE_ARRAYCOUNT(texture_postfixes); i++)
        {
            if (ends_with(property->name, HE_STRING(texture_postfixes[i])))
            {
                has_texture_postfix = true;
                break;
            }
        }

        property->is_texture_asset = has_texture_postfix && member->data_type == Shader_Data_Type::U32;
        property->is_color = ends_with(property->name, HE_STRING_LITERAL("color")) && (member->data_type == Shader_Data_Type::VECTOR3F || member->data_type == Shader_Data_Type::VECTOR4F);
    }

    material->pipeline_state_handle = descriptor.pipeline_state_handle;
    material->data = HE_ALLOCATE_ARRAY(get_general_purpose_allocator(), U8, properties->size);
    material->size = properties->size;
    material->dirty_count = HE_MAX_FRAMES_IN_FLIGHT;

    return material_handle;
}

Material *renderer_get_material(Material_Handle material_handle)
{
    return get(&renderer_state->materials, material_handle);
}

void renderer_destroy_material(Material_Handle &material_handle)
{
    Material *material = get(&renderer_state->materials, material_handle);

    if (material->name.data)
    {
        deallocate(get_general_purpose_allocator(), (void *)material->name.data);
    }

    Pipeline_State *pipeline_state = get(&renderer_state->pipeline_states, material->pipeline_state_handle); 
    
    renderer_destroy_pipeline_state(material->pipeline_state_handle);

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        renderer_destroy_buffer(material->buffers[frame_index]);
        renderer_destroy_bind_group(material->bind_groups[frame_index]);
    }

    deallocate(get_general_purpose_allocator(), material->data);
    release_handle(&renderer_state->materials, material_handle);

    material_handle = Resource_Pool< Material >::invalid_handle;
}

S32 find_property(Material_Handle material_handle, String name)
{
    Material *material = get(&renderer_state->materials, material_handle);
    for (U32 property_index = 0; property_index < material->properties.count; property_index++)
    {
        Material_Property *property = &material->properties[property_index];
        if (property->name == name)
        {
            return (S32)property_index;
        }
    }

    return -1;
}

bool set_property(Material_Handle material_handle, String name, Material_Property_Data data)
{
    S32 property_id = find_property(material_handle, name);
    if (property_id == -1)
    {
        HE_LOG(Rendering, Trace, "can't find material property: %s\n", name);
        return false;
    }
    return set_property(material_handle, property_id, data);
}

bool set_property(Material_Handle material_handle, S32 property_id, Material_Property_Data data)
{
    if (property_id == -1)
    {
        HE_LOG(Rendering, Trace, "invalid property id: %d\n", property_id);
        return false;
    }

    Material *material = get(&renderer_state->materials, material_handle);
    Material_Property *property = &material->properties[property_id];
    property->data = data;

    if (property->is_texture_asset)
    {
        U32 *texture_index = (U32 *)&material->data[property->offset_in_buffer];
        *texture_index = (U32)renderer_state->white_pixel_texture.index;
    }
    else
    {
        memcpy(material->data + property->offset_in_buffer, &property->data, get_size_of_shader_data_type(property->data_type));
    }

    material->dirty_count = HE_MAX_FRAMES_IN_FLIGHT;
    return true;
}

void renderer_use_material(Material_Handle material_handle)
{
    Material *material = get(&renderer_state->materials, material_handle);

    if (material->dirty_count)
    {
        material->dirty_count--;

        for (U32 property_index = 0; property_index < material->properties.count; property_index++)
        {
            Material_Property *property = &material->properties[property_index];
            if (property->is_texture_asset)
            {
                U32 *texture_index = (U32 *)&material->data[property->offset_in_buffer];
                Asset_Handle texture_asset = { .uuid = property->data.u64 };
                if (is_asset_handle_valid(texture_asset))
                {
                    if (is_asset_loaded(texture_asset))
                    {
                        Texture_Handle texture = get_asset_handle_as<Texture>(texture_asset);
                        *texture_index = texture.index;
                    }
                    else
                    {
                        *texture_index = renderer_state->white_pixel_texture.index;
                        material->dirty_count = HE_MAX_FRAMES_IN_FLIGHT;
                        aquire_asset(texture_asset);
                    }
                }
                else
                {
                    *texture_index = renderer_state->white_pixel_texture.index;
                }
            }
        }

        Buffer *material_buffer = get(&renderer_state->buffers, material->buffers[renderer_state->current_frame_in_flight_index]);
        copy_memory(material_buffer->data, material->data, material->size);
    }

    Pipeline_State *pipeline_state = renderer_get_pipeline_state(material->pipeline_state_handle);
    
    Update_Binding_Descriptor update_binding_descriptor =
    {
        .binding_number = 0,
        .element_index = 0,
        .count = 1,
        .buffers = &material->buffers[renderer_state->current_frame_in_flight_index],
    };
    renderer_update_bind_group(material->bind_groups[renderer_state->current_frame_in_flight_index], { .count = 1, .data = &update_binding_descriptor });    

    Bind_Group_Handle material_bind_groups[] =
    {
        material->bind_groups[renderer_state->current_frame_in_flight_index]
    };

    renderer->set_bind_groups(HE_PER_OBJECT_BIND_GROUP_INDEX, to_array_view(material_bind_groups));

    if (renderer_state->current_pipeline_state_handle.index != material->pipeline_state_handle.index)
    {
        renderer->set_pipeline_state(material->pipeline_state_handle);
        renderer_state->current_pipeline_state_handle = material->pipeline_state_handle;
    }
}

//
// Scenes
//

Transform get_identity_transform()
{
    Transform result =
    {
        .position = { 0.0f, 0.0f, 0.0f },
        .rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
        .euler_angles = { 0.0f, 0.0f, 0.0f },
        .scale = { 1.0f, 1.0f, 1.0f }
    };
    return result;
}

Transform combine(const Transform &a, const Transform &b)
{
    Transform result =
    {
        .position = a.position + b.position,
        .rotation = a.rotation * b.rotation,
        .scale = a.scale * b.scale
    };
    result.euler_angles = glm::degrees(glm::eulerAngles(result.rotation));
    return result;
}

glm::mat4 get_world_matrix(const Transform &transform)
{
    return glm::translate(glm::mat4(1.0f), transform.position) * glm::toMat4(transform.rotation) * glm::scale(glm::mat4(1.0f), transform.scale);
}

Scene_Handle renderer_create_scene(U32 node_capacity)
{
    Scene_Handle scene_handle = aquire_handle(&renderer_state->scenes);
    Scene *scene = get(&renderer_state->scenes, scene_handle);
    init(&scene->nodes, 0, node_capacity);
    scene->first_free_node_index = -1;
    allocate_node(scene, HE_STRING_LITERAL("Root"));
    return scene_handle;
}

Scene *renderer_get_scene(Scene_Handle scene_handle)
{
    Scene *scene = get(&renderer_state->scenes, scene_handle);
    return scene;
}

void renderer_destroy_scene(Scene_Handle &scene_handle)
{
    Scene *scene = get(&renderer_state->scenes, scene_handle);
    deinit(&scene->nodes);
    release_handle(&renderer_state->scenes, scene_handle);
    scene_handle = Resource_Pool< Scene >::invalid_handle;
}

static String light_type_to_str(Light_Type type)
{
    switch (type)
    {
        case Light_Type::DIRECTIONAL: return HE_STRING_LITERAL("directional");
        case Light_Type::POINT: return HE_STRING_LITERAL("point");
        case Light_Type::SPOT: return HE_STRING_LITERAL("spot");
        default:
        {
            HE_ASSERT(!"unsupported light type");
        } break;
    }

    return {};
}

static Light_Type str_to_light_type(String str)
{
    if (str == "directional")
    {
        return Light_Type::DIRECTIONAL;
    }
    else if (str == "point")
    {
        return Light_Type::POINT;
    }
    else if (str == "spot")
    {
        return Light_Type::SPOT;
    }

    HE_ASSERT(!"unsupported light type");
    return (Light_Type)0;
}

void serialize_scene_node(Scene_Node *node, S32 parent_index, String_Builder *builder)
{
    append(builder, "node_name %llu %.*s\n", node->name.count, HE_EXPAND_STRING(node->name));
    append(builder, "parent %d\n", parent_index);

    U32 component_count = 1;

    if (node->has_mesh)
    {
        component_count++;
    }

    if (node->has_light)
    {
        component_count++;
    }

    append(builder, "component_count %u\n", component_count);

    {
        Transform *t = &node->transform;
        glm::vec3 *p = &t->position;
        glm::quat *r = &t->rotation;
        glm::vec3 *s = &t->scale;

        append(builder, "component transform\n");
        append(builder, "position %f %f %f\n", p->x, p->y, p->z);
        append(builder, "rotation %f %f %f %f\n", r->x, r->y, r->z, r->w);
        append(builder, "scale %f %f %f\n", s->x, s->y, s->z);
    }

    if (node->has_mesh)
    {
        Static_Mesh_Component *static_mesh_comp = &node->mesh;
        append(builder, "component mesh\n");
        append(builder, "static_mesh_asset %llu\n", static_mesh_comp->static_mesh_asset);
    }

    if (node->has_light)
    {
        Light_Component *l = &node->light;
        glm::vec3 *c = &l->color;
        append(builder, "component light\n");
        append(builder, "type %.*s\n", HE_EXPAND_STRING(light_type_to_str(l->type)));
        append(builder, "color %f %f %f\n", c->r, c->g, c->b);
        append(builder, "intensity %f\n", l->intensity);
        append(builder, "radius %f\n", l->radius);
        append(builder, "inner_angle %f\n", l->inner_angle);
        append(builder, "outer_angle %f\n", l->outer_angle);
    }
}

bool serialize_scene(Scene_Handle scene_handle, String path)
{
    struct Serialized_Scene_Node
    {
        U32 node_index;
        S32 serialized_parent_index;
    };

    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    Scene *scene = get(&renderer_state->scenes, scene_handle);
    Skybox *skybox = &scene->skybox;

    Ring_Queue< Serialized_Scene_Node > queue;
    init(&queue, scene->nodes.count, to_allocator(scratch_memory.arena));

    U32 serialized_node_index = 0;
    push(&queue, { .node_index = 0, .serialized_parent_index = -1 });

    String_Builder builder = {};
    begin_string_builder(&builder, scratch_memory.arena);

    append(&builder, "version 1\n");
    append(&builder, "skybox_material_asset %llu\n", skybox->skybox_material_asset);
    append(&builder, "node_count %u\n", scene->nodes.count);

    while (!empty(&queue))
    {
        Serialized_Scene_Node item = {};
        peek_front(&queue, &item);
        pop_front(&queue);

        Scene_Node *scene_node = get_node(scene, item.node_index);
        serialize_scene_node(scene_node, item.serialized_parent_index, &builder);

        S32 parent_index = serialized_node_index++;

        for (S32 child_node_index = scene_node->first_child_index; child_node_index != -1; child_node_index = scene->nodes[child_node_index].next_sibling_index)
        {
            push(&queue, { .node_index = (U32)child_node_index, .serialized_parent_index = parent_index });
        }
    }

    String contents = end_string_builder(&builder);
    bool success = write_entire_file(path, (void *)contents.data, contents.count);
    return success;
}

bool deserialize_transform(String *str, Transform *t)
{
    glm::vec3 &p = t->position;
    glm::quat &r = t->rotation;
    glm::vec3 &s = t->scale;

    String position_lit = HE_STRING_LITERAL("position");
    if (!starts_with(*str, position_lit))
    {
        return false;
    }

    *str = advance(*str, position_lit.count);
    *str = eat_white_space(*str);

    for (U32 i = 0; i < 3; i++)
    {
        String value = eat_none_white_space(str);
        p[i] = str_to_f32(value);
        *str = eat_white_space(*str);
    }

    String rotation_lit = HE_STRING_LITERAL("rotation");
    if (!starts_with(*str, rotation_lit))
    {
        return false;
    }

    *str = advance(*str, rotation_lit.count);
    *str = eat_white_space(*str);

    glm::vec4 rv = {};

    for (U32 i = 0; i < 4; i++)
    {
        String value = eat_none_white_space(str);
        rv[i] = str_to_f32(value);
        *str = eat_white_space(*str);
    }
    
    r = glm::quat(rv.w, rv.x, rv.y, rv.z);
    t->euler_angles = glm::eulerAngles(r);

    String scale_lit = HE_STRING_LITERAL("scale");
    if (!starts_with(*str, scale_lit))
    {
        return false;
    }

    *str = advance(*str, scale_lit.count);
    *str = eat_white_space(*str);

    for (U32 i = 0; i < 3; i++)
    {
        String value = eat_none_white_space(str);
        s[i] = str_to_f32(value);
        *str = eat_white_space(*str);
    }

    return true;
}

bool deserialize_light(String *str, Light_Component *light)
{
    Parse_Name_Value_Result result = parse_name_value(str, HE_STRING_LITERAL("type"));
    if (!result.success)
    {
        return false;
    }

    Light_Type type = str_to_light_type(result.value);

    String color_lit = HE_STRING_LITERAL("color");
    if (!starts_with(*str, color_lit))
    {
        return false;
    }
    *str = advance(*str, color_lit.count);

    glm::vec3 color = {};

    for (U32 i = 0; i < 3; i++)
    {
        *str = eat_white_space(*str);
        String value = eat_none_white_space(str);
        color[i] = str_to_f32(value);
        *str = eat_white_space(*str);
    }

    result = parse_name_value(str, HE_STRING_LITERAL("intensity"));
    if (!result.success)
    {
        return false;
    }
    F32 intensity = str_to_f32(result.value);

    result = parse_name_value(str, HE_STRING_LITERAL("radius"));
    if (!result.success)
    {
        return false;
    }

    F32 radius = str_to_f32(result.value);

    result = parse_name_value(str, HE_STRING_LITERAL("inner_angle"));
    if (!result.success)
    {
        return false;
    }

    F32 inner_angle = str_to_f32(result.value);

    result = parse_name_value(str, HE_STRING_LITERAL("outer_angle"));
    if (!result.success)
    {
        return false;
    }

    F32 outer_angle = str_to_f32(result.value);

    light->type = type;
    light->color = color;
    light->intensity = intensity;
    light->radius = radius;
    light->inner_angle = inner_angle;
    light->outer_angle = outer_angle;
    return true;
}

bool deserialize_scene(Scene_Handle scene_handle, String path)
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
    Read_Entire_File_Result read_result = read_entire_file(path, to_allocator(scratch_memory.arena));
    if (!read_result.success)
    {
        HE_LOG(Assets, Error, "failed to parse scene asset\n");
        return false;
    }

    Scene *scene = renderer_get_scene(scene_handle);

    remove_node(scene, get_root_node(scene));
    scene->first_free_node_index = -1;
    reset(&scene->nodes);

    Skybox *skybox = &scene->skybox;

    String contents = { .count = read_result.size, .data = (const char *)read_result.data };
    String str = eat_white_space(contents);
    Parse_Name_Value_Result result = parse_name_value(&str, HE_STRING_LITERAL("version"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "failed to parse scene asset\n");
        return false;
    }
    U64 version = str_to_u64(result.value);

    result = parse_name_value(&str, HE_STRING_LITERAL("skybox_material_asset"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "failed to parse scene asset\n");
        return false;
    }

    Asset_Handle skybox_material = { .uuid = str_to_u64(result.value) };
    skybox->skybox_material_asset = skybox_material.uuid;

    result = parse_name_value(&str, HE_STRING_LITERAL("node_count"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "failed to parse scene asset\n");
        return false;
    }

    U32 node_count = u64_to_u32(str_to_u64(result.value));
    set_capacity(&scene->nodes, node_count);

    for (U32 node_index = 0; node_index < node_count; node_index++)
    {
        result = parse_name_value(&str, HE_STRING_LITERAL("node_name"));
        if (!result.success)
        {
            HE_LOG(Assets, Error, "failed to parse scene asset\n");
            return false;
        }
        U64 name_count = str_to_u64(result.value);
        String name = sub_string(str, 0, name_count);
        str = advance(str, name_count);

        result = parse_name_value(&str, HE_STRING_LITERAL("parent"));
        if (!result.success)
        {
            HE_LOG(Assets, Error, "failed to parse scene asset\n");
            return false;
        }

        S32 parent_index = (S32)str_to_s64(result.value);

        result = parse_name_value(&str, HE_STRING_LITERAL("component_count"));
        if (!result.success)
        {
            HE_LOG(Assets, Error, "failed to parse scene asset\n");
            return false;
        }

        allocate_node(scene, name);
        Scene_Node *node = get_node(scene, node_index);
        if (parent_index != -1)
        {
            add_child_last(scene, get_node(scene, parent_index), node);
        }

        U32 component_count = u64_to_u32(str_to_u64(result.value));

        for (U32 component_index = 0; component_index < component_count; component_index++)
        {
            result = parse_name_value(&str, HE_STRING_LITERAL("component"));
            if (!result.success)
            {
                HE_LOG(Assets, Error, "failed to parse scene asset\n");
                return false;
            }

            String type = result.value;
            if (type == "transform")
            {
                if (!deserialize_transform(&str, &node->transform))
                {
                    HE_LOG(Assets, Error, "failed to parse scene asset\n");
                    return false;
                }
            }
            else if (type == "mesh")
            {
                result = parse_name_value(&str, HE_STRING_LITERAL("static_mesh_asset"));
                if (!result.success)
                {
                    HE_LOG(Assets, Error, "failed to parse scene asset\n");
                    return false;
                }

                node->has_mesh = true;
                U64 static_mesh_asset = str_to_u64(result.value);
                Static_Mesh_Component *static_mesh_comp = &node->mesh;
                static_mesh_comp->static_mesh_asset = static_mesh_asset;
            }
            else if (type == "light")
            {
                node->has_light = true;
                deserialize_light(&str, &node->light);
            }
        }
    }

    return true;
}

Scene_Node *get_root_node(Scene *scene)
{
    HE_ASSERT(scene->nodes.count);
    return &scene->nodes[0];
}

Scene_Node *get_node(Scene *scene, S32 node_index)
{
    HE_ASSERT(node_index >= 0 && node_index < (S32)scene->nodes.count);
    return &scene->nodes[node_index];
}

U32 allocate_node(Scene *scene, String name)
{
    U32 node_index = 0;

    if (scene->first_free_node_index == -1)
    {
        append(&scene->nodes);
        node_index = scene->nodes.count - 1;
    }
    else
    {
        node_index = scene->first_free_node_index;
        scene->first_free_node_index = get_node(scene, scene->first_free_node_index)->next_sibling_index;
    }

    Scene_Node *node = get_node(scene, node_index);
    node->name = copy_string(name, to_allocator(get_general_purpose_allocator()));

    node->parent_index = -1;
    node->first_child_index = -1;
    node->last_child_index = -1;
    node->next_sibling_index = -1;
    node->prev_sibling_index = -1;

    node->transform = get_identity_transform();

    node->has_mesh = false;
    node->has_light = false;

    return node_index;
}

void add_child_last(Scene *scene, Scene_Node *parent, Scene_Node *node)
{
    HE_ASSERT(scene);
    HE_ASSERT(parent);
    HE_ASSERT(node);

    node->parent_index = index_of(&scene->nodes, parent);
    U32 node_index = index_of(&scene->nodes, node);

    if (parent->last_child_index != -1)
    {
        node->prev_sibling_index = parent->last_child_index;
        scene->nodes[parent->last_child_index].next_sibling_index = node_index;
        parent->last_child_index = node_index;
    }
    else
    {
        parent->first_child_index = parent->last_child_index = node_index;
    }
}

void add_child_first(Scene *scene, Scene_Node *parent, Scene_Node *node)
{
    HE_ASSERT(scene);
    HE_ASSERT(parent);
    HE_ASSERT(node);

    node->parent_index = index_of(&scene->nodes, parent);
    U32 node_index = index_of(&scene->nodes, node);

    if (parent->first_child_index != -1)
    {
        node->next_sibling_index = parent->first_child_index;
        scene->nodes[parent->first_child_index].prev_sibling_index = node_index;
        parent->first_child_index = node_index;
    }
    else
    {
        parent->first_child_index = parent->last_child_index = node_index;
    }
}

void add_child_after(Scene *scene, Scene_Node *target, Scene_Node *node)
{
    HE_ASSERT(scene);
    HE_ASSERT(target);
    HE_ASSERT(node);

    Scene_Node *parent = get_node(scene, target->parent_index);

    U32 target_index = index_of(&scene->nodes, target);
    U32 parent_index = index_of(&scene->nodes, parent);
    U32 node_index = index_of(&scene->nodes, node);
    
    node->parent_index = parent_index;

    if (parent->last_child_index == target_index)
    {
        node->prev_sibling_index = parent->last_child_index;
        scene->nodes[parent->last_child_index].next_sibling_index = node_index;
        parent->last_child_index = node_index;
    }
    else
    {
        node->next_sibling_index = target->next_sibling_index;
        node->prev_sibling_index = target_index;

        scene->nodes[target->next_sibling_index].prev_sibling_index = node_index;
        target->next_sibling_index = node_index;
    }
}

void remove_child(Scene *scene, Scene_Node *parent, Scene_Node *node)
{
    HE_ASSERT(parent);
    HE_ASSERT(node);
    HE_ASSERT(node->parent_index == index_of(&scene->nodes, parent));
    
    if (node->prev_sibling_index != -1)
    {
        scene->nodes[node->prev_sibling_index].next_sibling_index = node->next_sibling_index;
    }
    else
    {
        parent->first_child_index = node->next_sibling_index;
    }

    if (node->next_sibling_index != -1)
    {
        scene->nodes[node->next_sibling_index].prev_sibling_index = node->prev_sibling_index;
    }
    else
    {
        parent->last_child_index = node->prev_sibling_index;
    }

    node->parent_index = -1;
    node->next_sibling_index = -1;
    node->prev_sibling_index = -1;
}

void remove_node(Scene *scene, Scene_Node *node)
{
    for (S32 node_index = node->first_child_index; node_index != -1; node_index = get_node(scene, node_index)->next_sibling_index)
    {
        Scene_Node *child = get_node(scene, node_index);
        remove_node(scene, child);
    }

    if (node->parent_index != -1)
    {
        remove_child(scene, get_node(scene, node->parent_index), node);
    }

    U32 node_index = index_of(&scene->nodes, node);
    deallocate(get_general_purpose_allocator(), (void *)node->name.data);

    if (scene->first_free_node_index == -1)
    {
        node->next_sibling_index = -1;
    }
    else
    {
        Scene_Node *first_free_node = get_node(scene, scene->first_free_node_index);
        first_free_node->next_sibling_index = node_index;
    }

    scene->first_free_node_index = node_index;
}

void traverse_scene_tree(Scene *scene, Scene_Node *node, const Transform &parent_transform)
{
    Transform transform = combine(parent_transform, node->transform);

    if (node->has_mesh)
    {
        Static_Mesh_Component *static_mesh_comp = &node->mesh;
        Asset_Handle static_mesh_asset = { .uuid = static_mesh_comp->static_mesh_asset };
        if (is_asset_handle_valid(static_mesh_asset))
        {
            if (!is_asset_loaded(static_mesh_asset))
            {
                aquire_asset(static_mesh_asset);
            }
            else
            {
                Static_Mesh_Handle static_mesh_handle = get_asset_handle_as<Static_Mesh>(static_mesh_asset);
                Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);
                if (static_mesh->is_uploaded_to_gpu)
                {
                    HE_ASSERT(renderer_state->object_data_count < HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT);
                    U32 object_data_index = renderer_state->object_data_count++;
                    Shader_Object_Data *object_data = &renderer_state->object_data_base[object_data_index];
                    object_data->model = get_world_matrix(transform);

                    Buffer_Handle vertex_buffers[] =
                    {
                        static_mesh->positions_buffer,
                        static_mesh->normals_buffer,
                        static_mesh->uvs_buffer,
                        static_mesh->tangents_buffer
                    };

                    U64 offsets[] = { 0, 0, 0, 0 };

                    renderer->set_vertex_buffers(to_array_view(vertex_buffers), to_array_view(offsets));
                    renderer->set_index_buffer(static_mesh->indices_buffer, 0);

                    const Dynamic_Array< Sub_Mesh > &sub_meshes = static_mesh->sub_meshes;
                    for (U32 sub_mesh_index = 0; sub_mesh_index < sub_meshes.count; sub_mesh_index++)
                    {
                        const Sub_Mesh *sub_mesh = &sub_meshes[sub_mesh_index];

                        Material_Handle material_handle = renderer_state->default_material;

                        Asset_Handle material_asset = { .uuid = sub_mesh->material_asset };

                        if (is_asset_handle_valid(material_asset))
                        {
                            if (!is_asset_loaded(material_asset))
                            {
                                aquire_asset(material_asset);
                            }
                            else
                            {
                                material_handle = get_asset_handle_as<Material>(material_asset);
                            }
                        }

                        renderer_use_material(material_handle);
                        renderer->draw_sub_mesh(static_mesh_handle, object_data_index, sub_mesh_index);
                    }
                }
            }
        }
    }

    for (S32 node_index = node->first_child_index; node_index != -1; node_index = scene->nodes[node_index].next_sibling_index)
    {
        Scene_Node *child = get_node(scene, node_index);
        traverse_scene_tree(scene, child, transform);
    }
}

void render_scene(Scene_Handle scene_handle)
{
    Scene *scene = renderer_get_scene(scene_handle);
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
    Draw_Command *opaque_commands = HE_ALLOCATE_ARRAY(scratch_memory.arena, Draw_Command, scene->nodes.count);
    Draw_Command *transparent_commands = HE_ALLOCATE_ARRAY(scratch_memory.arena, Draw_Command, scene->nodes.count);
    Scene_Node *root = get_root_node(scene);
    traverse_scene_tree(scene, root, get_identity_transform());
}

//
// Render Context
//

Upload_Request_Handle renderer_create_upload_request(const Upload_Request_Descriptor &descriptor)
{
    Upload_Request_Handle upload_request_handle = aquire_handle(&renderer_state->upload_requests);
    Upload_Request *upload_request = get(&renderer_state->upload_requests, upload_request_handle);

    Renderer_Semaphore_Descriptor semaphore_descriptor =
    {
        .initial_value = 0
    };

    Semaphore_Handle semaphore = renderer_create_semaphore(semaphore_descriptor);
    upload_request->name = descriptor.name;
    upload_request->semaphore = semaphore;
    upload_request->target_value = 0;
    upload_request->uploaded = descriptor.is_uploaded;
    upload_request->texture = Resource_Pool< Texture >::invalid_handle;
    reset(&upload_request->allocations_in_transfer_buffer);

    return upload_request_handle;
}

Upload_Request *renderer_get_upload_request(Upload_Request_Handle upload_request_handle)
{
    Upload_Request *upload_request = get(&renderer_state->upload_requests, upload_request_handle);
    return upload_request;
}

void renderer_add_pending_upload_request(Upload_Request_Handle upload_request_handle)
{
    platform_lock_mutex(&renderer_state->pending_upload_requests_mutex);
    append(&renderer_state->pending_upload_requests, upload_request_handle);
    platform_unlock_mutex(&renderer_state->pending_upload_requests_mutex);
}

void renderer_destroy_upload_request(Upload_Request_Handle upload_request_handle)
{
    Upload_Request *upload_request = get(&renderer_state->upload_requests, upload_request_handle);
    HE_ASSERT(upload_request->target_value);
    
    renderer_destroy_semaphore(upload_request->semaphore);

    for (U32 i = 0; i < upload_request->allocations_in_transfer_buffer.count; i++)
    {
        deallocate(&renderer_state->transfer_allocator, upload_request->allocations_in_transfer_buffer[i]);
    }
    
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->destroy_upload_request(upload_request_handle);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
    
    release_handle(&renderer_state->upload_requests, upload_request_handle);
}

void renderer_handle_upload_requests()
{
    platform_lock_mutex(&renderer_state->pending_upload_requests_mutex);
    
    for (U32 i = 0; i < renderer_state->pending_upload_requests.count; i++)
    {
        Upload_Request_Handle upload_request_handle = renderer_state->pending_upload_requests[i];
        Upload_Request *upload_request = get(&renderer_state->upload_requests, upload_request_handle);
        U64 semaphore_value = renderer_get_semaphore_value(upload_request->semaphore);
        if (upload_request->target_value == semaphore_value)
        {
            HE_ASSERT(upload_request->uploaded);
            HE_ASSERT(*upload_request->uploaded == false);
            *upload_request->uploaded = true;
            if (is_valid_handle(&renderer_state->textures, upload_request->texture))
            {
                renderer->imgui_add_texture(upload_request->texture);
            }
            renderer_destroy_upload_request(upload_request_handle);
            remove_and_swap_back(&renderer_state->pending_upload_requests, i);
            i--;
        }
    }

    platform_unlock_mutex(&renderer_state->pending_upload_requests_mutex);
}

Render_Context get_render_context()
{
    return { renderer, renderer_state };
}

//
// Settings
//

void renderer_set_anisotropic_filtering(Anisotropic_Filtering_Setting anisotropic_filtering_setting)
{  
    if (renderer_state->anisotropic_filtering_setting == anisotropic_filtering_setting)
    {
        return;
    }
    
    renderer->wait_for_gpu_to_finish_all_work();
    
    Sampler_Descriptor default_sampler_descriptor = {};
    default_sampler_descriptor.min_filter = Filter::LINEAR;
    default_sampler_descriptor.mag_filter = Filter::NEAREST;
    default_sampler_descriptor.mip_filter = Filter::LINEAR;
    default_sampler_descriptor.address_mode_u = Address_Mode::REPEAT;
    default_sampler_descriptor.address_mode_v = Address_Mode::REPEAT;
    default_sampler_descriptor.address_mode_w = Address_Mode::REPEAT;
    default_sampler_descriptor.anisotropy = get_anisotropic_filtering_value(anisotropic_filtering_setting);

    if (is_valid_handle(&renderer_state->samplers, renderer_state->default_texture_sampler))
    {
        renderer->destroy_sampler(renderer_state->default_texture_sampler);
    }

    renderer->create_sampler(renderer_state->default_texture_sampler, default_sampler_descriptor);
    renderer_state->anisotropic_filtering_setting = anisotropic_filtering_setting;
}

void renderer_set_msaa(MSAA_Setting msaa_setting)
{
    if (renderer_state->msaa_setting == msaa_setting)
    {
        return;
    }

    renderer->wait_for_gpu_to_finish_all_work();
    renderer_state->msaa_setting = msaa_setting;
    compile(&renderer_state->render_graph, renderer, renderer_state);
    invalidate(&renderer_state->render_graph, renderer, renderer_state);
}

void renderer_set_vsync(bool enabled)
{
    if (renderer_state->vsync == enabled)
    {
        return;
    }

    renderer->wait_for_gpu_to_finish_all_work();
    renderer_state->vsync = enabled;
    renderer->set_vsync(enabled);
}

void renderer_set_triple_buffering(bool enabled)
{
    if (renderer_state->triple_buffering == enabled)
    {
        return;
    }

    renderer->wait_for_gpu_to_finish_all_work();
    renderer_state->triple_buffering = enabled;
    if (enabled)
    {
        renderer_state->frames_in_flight = HE_MAX_FRAMES_IN_FLIGHT;
    }
    else
    {
        renderer_state->frames_in_flight = HE_MAX_FRAMES_IN_FLIGHT - 1;
    }
    
    renderer_state->current_frame_in_flight_index = 0;
}

//
// ImGui
//

bool init_imgui(Engine *engine)
{
    renderer_state->imgui_docking = false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    platform_init_imgui(engine);
    return renderer->init_imgui();
}

void imgui_new_frame()
{
    platform_imgui_new_frame();
    renderer->imgui_new_frame();
    ImGui::NewFrame();

    if (renderer_state->engine->show_imgui)
    {
        if (renderer_state->imgui_docking)
        {
            static bool opt_fullscreen_persistant = true;
            bool opt_fullscreen = opt_fullscreen_persistant;
            static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

            // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
            // because it would be confusing to have two docking targets within each others.
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar|ImGuiWindowFlags_NoDocking;

            if (opt_fullscreen)
            {
                ImGuiViewport *viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->Pos);
                ImGui::SetNextWindowSize(viewport->Size);
                ImGui::SetNextWindowViewport(viewport->ID);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                window_flags |= ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove;
                window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoNavFocus;
            }

            // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
            // and handle the pass-thru hole, so we ask Begin() to not render a background.
            if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            {
                window_flags |= ImGuiWindowFlags_NoBackground;
            }

            // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
            // This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
            // all active windows docked into it will lose their parent and become undocked.
            // We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
            // any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("DockSpace", &renderer_state->imgui_docking, window_flags);
            ImGui::PopStyleVar();

            if (opt_fullscreen)
            {
                ImGui::PopStyleVar(2);
            }

            // DockSpace
            ImGuiIO &io = ImGui::GetIO();

            ImGuiStyle& style = ImGui::GetStyle();
            float minWindowSizeX = style.WindowMinSize.x;
            style.WindowMinSize.x = 280.0f;

            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
            {
                ImGuiID dockspace_id = ImGui::GetID("DockSpace");
                ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
            }

            style.WindowMinSize.x = minWindowSizeX;
        }
    }
}