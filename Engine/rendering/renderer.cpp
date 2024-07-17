#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"
#include "rendering/render_passes.h"
#include "rendering/renderer_types.h"

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
#include <imgui_internal.h>
#include <ImGuizmo/ImGuizmo.h>

#pragma warning(pop)

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
            renderer->draw_fullscreen_triangle = &vulkan_renderer_draw_fullscreen_triangle;
            renderer->fill_buffer = &vulkan_renderer_fill_buffer;
            renderer->clear_texture = &vulkan_renderer_clear_texture;
            renderer->change_texture_state = &vulkan_renderer_change_texture_state;
            renderer->invalidate_buffer = &vulkan_renderer_invalidate_buffer;
            renderer->begin_compute_pass = &vulkan_renderer_begin_compute_pass;
            renderer->dispatch_compute = &vulkan_renderer_dispatch_compute;
            renderer->end_compute_pass = &vulkan_renderer_end_compute_pass;
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
    Memory_Context memory_context = grab_memory_context();

    renderer_state = HE_ALLOCATOR_ALLOCATE(memory_context.permenent_allocator, Renderer_State);
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
    
    init(&renderer_state->buffers, HE_MAX_BUFFER_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->textures, HE_MAX_TEXTURE_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->samplers, HE_MAX_SAMPLER_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->shaders, HE_MAX_SHADER_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->pipeline_states, HE_MAX_PIPELINE_STATE_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->bind_groups, HE_MAX_BIND_GROUP_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->render_passes, HE_MAX_RENDER_PASS_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->frame_buffers, HE_MAX_FRAME_BUFFER_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->semaphores, HE_MAX_SEMAPHORE_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->materials, HE_MAX_MATERIAL_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->static_meshes, HE_MAX_STATIC_MESH_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->scenes, HE_MAX_SCENE_COUNT, memory_context.permenent_allocator);
    init(&renderer_state->upload_requests, HE_MAX_UPLOAD_REQUEST_COUNT, memory_context.permenent_allocator);

    platform_create_mutex(&renderer_state->pending_upload_requests_mutex);
    reset(&renderer_state->pending_upload_requests);

    U32 &back_buffer_width = renderer_state->back_buffer_width;
    U32 &back_buffer_height = renderer_state->back_buffer_height;
    bool &triple_buffering = renderer_state->triple_buffering;
    bool &vsync = renderer_state->vsync;
    U8 &anisotropic_filtering_setting = (U8&)renderer_state->anisotropic_filtering_setting;
    F32 &gamma = renderer_state->gamma;

    // default settings
    back_buffer_width = 1280;
    back_buffer_height = 720;
    anisotropic_filtering_setting = (U8)Anisotropic_Filtering_Setting::X16;
    triple_buffering = true;
    vsync = false;
    gamma = 2.2f;

    HE_DECLARE_CVAR("renderer", back_buffer_width, CVarFlag_None);
    HE_DECLARE_CVAR("renderer", back_buffer_height, CVarFlag_None);
    HE_DECLARE_CVAR("renderer", triple_buffering, CVarFlag_None);
    HE_DECLARE_CVAR("renderer", gamma, CVarFlag_None);
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
        .size = HE_GIGA_BYTES(1),
        .usage = Buffer_Usage::TRANSFER
    };
    renderer_state->transfer_buffer = renderer_create_buffer(transfer_buffer_descriptor);

    Buffer *transfer_buffer = get(&renderer_state->buffers, renderer_state->transfer_buffer);
    init_free_list_allocator(&renderer_state->transfer_allocator, transfer_buffer->data, transfer_buffer->size, transfer_buffer->size, "transfer_allocator");

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
        U32 white_pixel_data = 0xFFFFFFFF;
        void *data_array[6] = {};

        for (U32 i = 0; i < 6; i++)
        {
            U32 *pixel = HE_ALLOCATE(&renderer_state->transfer_allocator, U32);
            *pixel = white_pixel_data;
            data_array[i] = (void *)pixel;
        }

        Texture_Descriptor cubemap_descriptor =
        {
            .name = HE_STRING_LITERAL("default_cubemap"),
            .width = 1,
            .height = 1,
            .format = Texture_Format::R8G8B8A8_UNORM,
            .layer_count = 6,
            .data_array = to_array_view(data_array),
            .mipmapping = false,
            .is_cubemap = true
        };

        renderer_state->default_cubemap = renderer_create_texture(cubemap_descriptor);
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
    setup_render_passes(&renderer_state->render_graph, renderer_state);

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

        .anisotropy = 0
    };

    renderer_state->default_cubemap_sampler = renderer_create_sampler(default_cubemap_sampler_descriptor);

    {
        renderer_state->default_shader = load_shader(HE_STRING_LITERAL("default"));
        HE_ASSERT(is_valid_handle(&renderer_state->shaders, renderer_state->default_shader));

        Pipeline_State_Settings settings =
        {
            .cull_mode = Cull_Mode::BACK,
            .front_face = Front_Face::COUNTER_CLOCKWISE,
            .fill_mode = Fill_Mode::SOLID,
            .depth_operation = Compare_Operation::LESS_OR_EQUAL,
            .depth_testing = true,
            .depth_writing = false,
            .sample_shading = true,
        };

        Material_Descriptor default_material_descriptor =
        {
            .name = HE_STRING_LITERAL("default"),
            .type = Material_Type::OPAQUE,
            .shader = renderer_state->default_shader,
            .settings = settings,
        };

        renderer_state->default_material = renderer_create_material(default_material_descriptor);
        HE_ASSERT(is_valid_handle(&renderer_state->materials, renderer_state->default_material));

        set_property(renderer_state->default_material, HE_STRING_LITERAL("debug_texture_index"), { .u32 = (U32)renderer_state->white_pixel_texture.index });
        set_property(renderer_state->default_material, HE_STRING_LITERAL("debug_color"), { .v3f = { 1.0f, 0.0f, 1.0f }});

        Shader *default_shader = get(&renderer_state->shaders, renderer_state->default_shader);

        Frame_Render_Data *render_data = &renderer_state->render_data;
        init(&render_data->skybox_commands);
        init(&render_data->opaque_commands);
        init(&render_data->alpha_cutoff_commands);
        init(&render_data->transparent_commands);
        init(&render_data->outline_commands);

        render_data->light_bin_count = HE_LIGHT_BIN_COUNT;

        Shader_Struct *globals_struct = renderer_find_shader_struct(renderer_state->default_shader, HE_STRING_LITERAL("Globals"));
        HE_ASSERT(globals_struct);

        Bind_Group_Descriptor globals_bind_group_descriptor =
        {
            .shader = renderer_state->default_shader,
            .group_index = SHADER_GLOBALS_BIND_GROUP
        };

        Bind_Group_Descriptor pass_bind_group_descriptor =
        {
            .shader = renderer_state->default_shader,
            .group_index = SHADER_PASS_BIND_GROUP
        };

        for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
        {
            Buffer_Descriptor globals_uniform_buffer_descriptor =
            {
                .size = globals_struct->size,
                .usage = Buffer_Usage::UNIFORM,
            };
            render_data->globals_uniform_buffers[frame_index] = renderer_create_buffer(globals_uniform_buffer_descriptor);

            Buffer_Descriptor instance_storage_buffer_descriptor =
            {
                .size = sizeof(Shader_Instance_Data) * HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT,
                .usage = Buffer_Usage::STORAGE_CPU_SIDE,
            };
            render_data->instance_storage_buffers[frame_index] = renderer_create_buffer(instance_storage_buffer_descriptor);

            Buffer_Descriptor light_storage_buffer_descriptor =
            {
                .size = sizeof(Shader_Light) * HE_MAX_LIGHT_COUNT,
                .usage = Buffer_Usage::STORAGE_CPU_SIDE,
            };
            render_data->light_storage_buffers[frame_index] = renderer_create_buffer(light_storage_buffer_descriptor);

            render_data->light_bins[frame_index] = Resource_Pool< Buffer >::invalid_handle;

            render_data->globals_bind_groups[frame_index] = renderer_create_bind_group(globals_bind_group_descriptor);
            render_data->pass_bind_groups[frame_index] = renderer_create_bind_group(pass_bind_group_descriptor);

            Buffer_Descriptor scene_buffer_descriptor =
            {
                .size = sizeof(S32),
                .usage = Buffer_Usage::TRANSFER,
            };
            render_data->scene_buffers[frame_index] = renderer_create_buffer(scene_buffer_descriptor);

            render_data->head_index_textures[frame_index] = Resource_Pool< Texture >::invalid_handle;
            render_data->node_buffers[frame_index] = Resource_Pool< Buffer >::invalid_handle;

            Buffer_Descriptor node_count_buffer_descriptor =
            {
                .size = sizeof(S32),
                .usage = Buffer_Usage::STORAGE_GPU_SIDE,
            };
            render_data->node_count_buffers[frame_index] = renderer_create_buffer(node_count_buffer_descriptor);
        }
    }

    {
        renderer_state->depth_prepass_shader = load_shader(HE_STRING_LITERAL("depth_prepass"));

        Pipeline_State_Descriptor pipeline_state_descriptor =
        {
            .shader = renderer_state->depth_prepass_shader,
            .render_pass = get_render_pass(&renderer_state->render_graph, HE_STRING_LITERAL("depth_prepass")), // todo(amer): we should not depend on the render graph here...
            .settings =
            {
                .cull_mode = Cull_Mode::BACK,
                .front_face = Front_Face::COUNTER_CLOCKWISE,
                .fill_mode = Fill_Mode::SOLID,

                .depth_operation = Compare_Operation::LESS,
                .depth_testing = true,
                .depth_writing = true,

                .stencil_operation = Compare_Operation::ALWAYS,
                .stencil_fail = Stencil_Operation::KEEP,
                .stencil_pass = Stencil_Operation::REPLACE,
                .depth_fail = Stencil_Operation::KEEP,
                .stencil_compare_mask = 0xFF,
                .stencil_write_mask = 0xFF,
                .stencil_reference_value = 1,
                .stencil_testing = true,

                .sample_shading = false,
            },
        };

        renderer_state->depth_prepass_pipeline = renderer_create_pipeline_state(pipeline_state_descriptor);
        HE_ASSERT(is_valid_handle(&renderer_state->pipeline_states, renderer_state->depth_prepass_pipeline));
    }

    {
        renderer_state->world_shader = load_shader(HE_STRING_LITERAL("world"));
        set_shader(&renderer_state->render_graph, get_node(&renderer_state->render_graph, HE_STRING_LITERAL("world")), renderer_state->world_shader, 3);
    }

    {
        renderer_state->transparent_shader = load_shader(HE_STRING_LITERAL("transparent"));

        Pipeline_State_Descriptor transparent_pipeline_descriptor =
        {
            .shader = renderer_state->transparent_shader,
            .render_pass = get_render_pass(&renderer_state->render_graph, HE_STRING_LITERAL("transparent")), // todo(amer): we should not depend on the render graph here...
            .settings =
            {
                .cull_mode = Cull_Mode::NONE,
                .front_face = Front_Face::COUNTER_CLOCKWISE,
                .fill_mode = Fill_Mode::SOLID,

                .depth_operation = Compare_Operation::LESS_OR_EQUAL,
                .depth_testing = true,
                .depth_writing = false,

                .stencil_operation = Compare_Operation::ALWAYS,
                .stencil_fail = Stencil_Operation::KEEP,
                .stencil_pass = Stencil_Operation::KEEP,
                .depth_fail = Stencil_Operation::KEEP,
                .stencil_compare_mask = 0xFF,
                .stencil_write_mask = 0xFF,
                .stencil_reference_value = 0,
                .stencil_testing = false,

                .sample_shading = false,
                .alpha_blending = false,
            },
        };

        renderer_state->transparent_pipeline = renderer_create_pipeline_state(transparent_pipeline_descriptor);
        set_shader(&renderer_state->render_graph, get_node(&renderer_state->render_graph, HE_STRING_LITERAL("transparent")), renderer_state->transparent_shader);
    }

    {
        renderer_state->outline_shader = load_shader(HE_STRING_LITERAL("outline"));

        Material_Descriptor first_pass_outline_material =
        {
            .name = HE_STRING_LITERAL("outline_first_pass"),
            .type = Material_Type::UI,
            .shader = renderer_state->outline_shader,
            .settings =
            {
                .cull_mode = Cull_Mode::BACK,
                .front_face = Front_Face::COUNTER_CLOCKWISE,
                .fill_mode = Fill_Mode::SOLID,

                .depth_operation = Compare_Operation::LESS_OR_EQUAL,
                .depth_testing = false,
                .depth_writing = false,

                .stencil_operation = Compare_Operation::ALWAYS,
                .stencil_fail = Stencil_Operation::KEEP,
                .stencil_pass = Stencil_Operation::REPLACE,
                .depth_fail = Stencil_Operation::KEEP,
                .stencil_compare_mask = 0xFF,
                .stencil_write_mask = 0xFF,
                .stencil_reference_value = 1,
                .stencil_testing = true,

                .sample_shading = true,
                .color_mask = (Color_Write_Mask)0,
            }
        };

        Material_Descriptor second_pass_outline_material =
        {
            .name = HE_STRING_LITERAL("outline_second_pass"),
            .type = Material_Type::UI,
            .shader = renderer_state->outline_shader,
            .settings =
            {
                .cull_mode = Cull_Mode::BACK,
                .front_face = Front_Face::COUNTER_CLOCKWISE,
                .fill_mode = Fill_Mode::SOLID,

                .depth_operation = Compare_Operation::LESS_OR_EQUAL,
                .depth_testing = false,
                .depth_writing = false,

                .stencil_operation = Compare_Operation::NOT_EQUAL,
                .stencil_fail = Stencil_Operation::KEEP,
                .stencil_pass = Stencil_Operation::KEEP,
                .depth_fail = Stencil_Operation::KEEP,
                .stencil_compare_mask = 0xFF,
                .stencil_write_mask = 0xFF,
                .stencil_reference_value = 1,
                .stencil_testing = true,

                .sample_shading = true,
            }
        };

        glm::vec3 outline_color = { 1.0f, 1.0f, 0.2f };

        renderer_state->outline_first_pass = renderer_create_material(first_pass_outline_material);
        set_property(renderer_state->outline_first_pass, HE_STRING_LITERAL("scale_factor"), { .f32 = 1.0f });
        set_property(renderer_state->outline_first_pass, HE_STRING_LITERAL("outline_color"), { .v3f = outline_color });

        renderer_state->outline_second_pass = renderer_create_material(second_pass_outline_material);

        set_property(renderer_state->outline_second_pass, HE_STRING_LITERAL("scale_factor"), { .f32 = 1.01f });
        set_property(renderer_state->outline_second_pass, HE_STRING_LITERAL("outline_color"), { .v3f = outline_color });
    }

    {
        Render_Pass_Descriptor cubemap_render_pass_descriptor =
        {
            .name = HE_STRING_LITERAL("cubemap_render_pass"),
            .color_attachments =
            {{
                {
                    .format = Texture_Format::R16G16B16A16_SFLOAT
                }
            }}
        };

        Render_Pass_Handle cubemap_render_pass_handle = renderer_create_render_pass(cubemap_render_pass_descriptor);
        HE_ASSERT(is_valid_handle(&renderer_state->render_passes, cubemap_render_pass_handle));
        renderer_state->cubemap_render_pass = cubemap_render_pass_handle;

        Shader_Handle brdf_lut_shader_handle = load_shader(HE_STRING_LITERAL("brdf_lut"));
        HE_ASSERT(is_valid_handle(&renderer_state->shaders, brdf_lut_shader_handle));
        renderer_state->brdf_lut_shader = brdf_lut_shader_handle;

        Pipeline_State_Descriptor brdf_lut_pipeline =
        {
            .shader = brdf_lut_shader_handle,
            .render_pass = renderer_state->cubemap_render_pass,
            .settings =
            {
                .cull_mode = Cull_Mode::NONE,
                .fill_mode = Fill_Mode::SOLID,

                .depth_testing = false,
                .depth_writing = false,

                .stencil_testing = false,

                .sample_shading = true,
            },
        };

        Pipeline_State_Handle brdf_lut_pipeline_state_handle = renderer_create_pipeline_state(brdf_lut_pipeline);
        HE_ASSERT(is_valid_handle(&renderer_state->pipeline_states, brdf_lut_pipeline_state_handle));
        renderer_state->brdf_lut_pipeline_state = brdf_lut_pipeline_state_handle;

        Shader_Handle hdr_shader_handle = load_shader(HE_STRING_LITERAL("hdr"));
        HE_ASSERT(is_valid_handle(&renderer_state->shaders, hdr_shader_handle));
        renderer_state->hdr_shader = hdr_shader_handle;

        Shader_Handle irradiance_shader_handle = load_shader(HE_STRING_LITERAL("irradiance"));
        HE_ASSERT(is_valid_handle(&renderer_state->shaders, irradiance_shader_handle));
        renderer_state->irradiance_shader = irradiance_shader_handle;

        Shader_Handle prefilter_shader_handle = load_shader(HE_STRING_LITERAL("prefilter"));
        HE_ASSERT(is_valid_handle(&renderer_state->shaders, prefilter_shader_handle));
        renderer_state->prefilter_shader = prefilter_shader_handle;

        Pipeline_State_Descriptor hdr_pipeline =
        {
            .shader = hdr_shader_handle,
            .render_pass = renderer_state->cubemap_render_pass,
            .settings =
            {
                .cull_mode = Cull_Mode::NONE,
                .fill_mode = Fill_Mode::SOLID,

                .depth_testing = false,
                .depth_writing = false,

                .stencil_testing = false,

                .sample_shading = true,
            },
        };

        Pipeline_State_Handle hdr_pipeline_state_handle = renderer_create_pipeline_state(hdr_pipeline);
        HE_ASSERT(is_valid_handle(&renderer_state->pipeline_states, hdr_pipeline_state_handle));
        renderer_state->hdr_pipeline_state = hdr_pipeline_state_handle;

        Pipeline_State_Descriptor irradiance_pipeline =
        {

            .shader = irradiance_shader_handle,
            .render_pass = renderer_state->cubemap_render_pass,
            .settings =
            {
                .cull_mode = Cull_Mode::NONE,
                .fill_mode = Fill_Mode::SOLID,

                .depth_testing = false,
                .depth_writing = false,

                .stencil_testing = false,

                .sample_shading = true,
            },
        };

        Pipeline_State_Handle irradiance_pipeline_state_handle = renderer_create_pipeline_state(irradiance_pipeline);
        HE_ASSERT(is_valid_handle(&renderer_state->pipeline_states, irradiance_pipeline_state_handle));
        renderer_state->irradiance_pipeline_state = irradiance_pipeline_state_handle;

        Pipeline_State_Descriptor prefilter_pipeline =
        {
            .shader = prefilter_shader_handle,
            .render_pass = renderer_state->cubemap_render_pass,
            .settings =
            {
                .cull_mode = Cull_Mode::NONE,
                .fill_mode = Fill_Mode::SOLID,

                .depth_testing = false,
                .depth_writing = false,

                .stencil_testing = false,

                .sample_shading = true,
            },
        };

        Pipeline_State_Handle prefilter_pipeline_state_handle = renderer_create_pipeline_state(prefilter_pipeline);
        HE_ASSERT(is_valid_handle(&renderer_state->pipeline_states, prefilter_pipeline_state_handle));
        renderer_state->prefilter_pipeline_state = prefilter_pipeline_state_handle;

        Texture_Descriptor brdf_lut_texture_descriptor =
        {
            .name = HE_STRING_LITERAL("brdf_lut"),
            .width = 512,
            .height = 512,
            .format = Texture_Format::R16G16B16A16_SFLOAT,
            .layer_count = 1,
            .mipmapping = false,
            .sample_count = 1,
            .is_attachment = true,
            .is_cubemap = false,
        };

        Texture_Handle brdf_lut_texture_handle = renderer_create_texture(brdf_lut_texture_descriptor);
        vulkan_renderer_fill_brdf_lut(brdf_lut_texture_handle);
        renderer_state->brdf_lut_texture = brdf_lut_texture_handle;
    }

    return true;
}

void deinit_renderer_state()
{
    renderer->wait_for_gpu_to_finish_all_work();

    for (auto it = iterator(&renderer_state->buffers); next(&renderer_state->buffers, it);)
    {
        renderer->destroy_buffer(it, true);
    }

    for (auto it = iterator(&renderer_state->textures); next(&renderer_state->textures, it);)
    {
        renderer->destroy_texture(it, true);
    }

    for (auto it = iterator(&renderer_state->samplers); next(&renderer_state->samplers, it);)
    {
        renderer->destroy_sampler(it, true);
    }

    for (auto it = iterator(&renderer_state->shaders); next(&renderer_state->shaders, it);)
    {
        renderer->destroy_shader(it, true);
    }

    for (auto it = iterator(&renderer_state->frame_buffers); next(&renderer_state->frame_buffers, it);)
    {
        renderer->destroy_frame_buffer(it, true);
    }

    for (auto it = iterator(&renderer_state->render_passes); next(&renderer_state->render_passes, it);)
    {
        renderer->destroy_render_pass(it, true);
    }

    for (auto it = iterator(&renderer_state->pipeline_states); next(&renderer_state->pipeline_states, it);)
    {
        renderer->destroy_pipeline_state(it, true);
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
    renderer->destroy_buffer(buffer_handle, false);
    release_handle(&renderer_state->buffers, buffer_handle);
    buffer_handle = Resource_Pool< Buffer >::invalid_handle;
}

//
// Textures
//
Texture_Handle renderer_create_texture(const Texture_Descriptor &descriptor)
{
    HE_ASSERT(descriptor.data_array.count <= HE_MAX_UPLOAD_REQUEST_ALLOCATION_COUNT);

    Memory_Context memory_context = grab_memory_context();

    Texture_Handle texture_handle = aquire_handle(&renderer_state->textures);
    Texture *texture = renderer_get_texture(texture_handle);

    if (descriptor.name.data)
    {
        texture->name = copy_string(descriptor.name, memory_context.general_allocator);
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
        texture->state = Resource_State::SHADER_READ_ONLY;
        renderer_add_pending_upload_request(upload_request_handle);
    }

    texture->width = descriptor.width;
    texture->height = descriptor.height;
    texture->is_attachment = descriptor.is_attachment;
    texture->is_cubemap = descriptor.is_cubemap;
    texture->format = descriptor.format;
    texture->sample_count = descriptor.sample_count;
    texture->is_storage = descriptor.is_storage;

    return texture_handle;
}

Texture* renderer_get_texture(Texture_Handle texture_handle)
{
    return get(&renderer_state->textures, texture_handle);
}

void renderer_destroy_texture(Texture_Handle &texture_handle)
{
    HE_ASSERT(texture_handle != renderer_state->white_pixel_texture);
    
    Memory_Context memory_context = grab_memory_context();

    Texture *texture = renderer_get_texture(texture_handle);

    if (texture->name.count && texture->name.data)
    {
        HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, (void *)texture->name.data);
    }

    texture->width = 0;
    texture->height = 0;
    texture->sample_count = 1;
    texture->size = 0;
    texture->alignment = 0;
    texture->is_attachment = false;
    texture->is_cubemap = false;
    texture->is_uploaded_to_gpu = false;

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->destroy_texture(texture_handle, false);
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

Environment_Map renderer_hdr_to_environment_map(F32 *data, U32 width, U32 height)
{
    Texture_Descriptor hdr_cubemap_descriptor =
    {
        .name = HE_STRING_LITERAL("hdr"),
        .width = 512,
        .height = 512,
        .format = Texture_Format::R16G16B16A16_SFLOAT,
        .layer_count = 6,
        .mipmapping = true,
        .sample_count = 1,
        .is_attachment = true,
        .is_cubemap = true,
    };

    Texture_Handle hdr_cubemap_handle = renderer_create_texture(hdr_cubemap_descriptor);

    Texture_Descriptor irradiance_cubemap_descriptor =
    {
        .name = HE_STRING_LITERAL("irradiance"),
        .width = 64,
        .height = 64,
        .format = Texture_Format::R16G16B16A16_SFLOAT,
        .layer_count = 6,
        .mipmapping = false,
        .sample_count = 1,
        .is_attachment = true,
        .is_cubemap = true,
    };

    Texture_Handle irradiance_cubemap_handle = renderer_create_texture(irradiance_cubemap_descriptor);

    Texture_Descriptor prefilter_cubemap_descriptor =
    {
        .name = HE_STRING_LITERAL("prefilter"),
        .width = 512,
        .height = 512,
        .format = Texture_Format::R16G16B16A16_SFLOAT,
        .layer_count = 6,
        .mipmapping = true,
        .sample_count = 1,
        .is_attachment = true,
        .is_cubemap = true,
    };

    Texture_Handle prefilter_cubemap_handle = renderer_create_texture(prefilter_cubemap_descriptor);

    Buffer_Descriptor globals_uniform_buffer_descriptor =
    {
        .size = sizeof(glm::mat4),
        .usage = Buffer_Usage::UNIFORM
    };
    Buffer_Handle globals_uniform_buffer = renderer_create_buffer(globals_uniform_buffer_descriptor);

    Enviornment_Map_Render_Data render_data
    {
        .hdr_data = data,
        .hdr_width = width,
        .hdr_height = height,

        .globals_uniform_buffer = globals_uniform_buffer,

        .hdr_cubemap_handle = hdr_cubemap_handle,
        .irradiance_cubemap_handle = irradiance_cubemap_handle,
        .prefilter_cubemap_handle = prefilter_cubemap_handle,
    };

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    vulkan_renderer_hdr_to_environment_map(render_data);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);

    return
    {
        .environment_map = hdr_cubemap_handle,
        .irradiance_map = irradiance_cubemap_handle,
        .prefilter_map = prefilter_cubemap_handle,
    };
}

Sampler* renderer_get_sampler(Sampler_Handle sampler_handle)
{
    return get(&renderer_state->samplers, sampler_handle);
}

void renderer_destroy_sampler(Sampler_Handle &sampler_handle)
{
    renderer->destroy_sampler(sampler_handle, false);
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
        case Shader_Stage::COMPUTE: return shaderc_compute_shader;

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

    Memory_Context memory_context = grab_memory_context();

    String source = HE_STRING(requested_source);
    String path = format_string(memory_context.temp_allocator, "%.*s/%.*s", HE_EXPAND_STRING(ud->include_path), HE_EXPAND_STRING(source));
    Read_Entire_File_Result file_result = read_entire_file(path, ud->allocator);
    HE_ASSERT(file_result.success);
    shaderc_include_result *result = HE_ALLOCATOR_ALLOCATE(ud->allocator, shaderc_include_result);
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
    HE_ALLOCATOR_DEALLOCATE(ud->allocator, (void *)include_result->content);
    HE_ALLOCATOR_DEALLOCATE(ud->allocator, include_result);
}

Shader_Compilation_Result renderer_compile_shader(String source, String include_path)
{
    static shaderc_compiler_t compiler = shaderc_compiler_initialize();
    static shaderc_compile_options_t options = shaderc_compile_options_initialize();

    Memory_Context memory_context = grab_memory_context();

    Shaderc_UserData *shaderc_userdata = HE_ALLOCATOR_ALLOCATE(memory_context.general_allocator, Shaderc_UserData);
    shaderc_userdata->allocator = memory_context.general_allocator;
    shaderc_userdata->include_path = include_path;

    HE_DEFER { HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, shaderc_userdata); };

    shaderc_compile_options_set_include_callbacks(options, shaderc_include_resolve, shaderc_include_result_release, shaderc_userdata);
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    shaderc_compile_options_set_generate_debug_info(options);
    shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_zero); // todo(amer): shaderc_optimization_level_performance
    shaderc_compile_options_set_auto_map_locations(options, true);

    static String shader_stage_signature[(U32)Shader_Stage::COUNT];
    shader_stage_signature[(U32)Shader_Stage::VERTEX] = HE_STRING_LITERAL("vertex");
    shader_stage_signature[(U32)Shader_Stage::FRAGMENT] = HE_STRING_LITERAL("fragment");
    shader_stage_signature[(U32)Shader_Stage::COMPUTE] = HE_STRING_LITERAL("compute");

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

    Shader_Type shader_type = Shader_Type::GRAPHICS;

    for (U32 stage_index = 0; stage_index < (U32)Shader_Stage::COUNT; stage_index++)
    {
        if (!sources[stage_index].count)
        {
            continue;
        }

        if (stage_index == (U32)Shader_Stage::COMPUTE)
        {
            shader_type = Shader_Type::COMPUTE;
        }

        // shaderc requires a string to be null-terminated
        String source = format_string(memory_context.temp_allocator, "%.*s", HE_EXPAND_STRING(sources[stage_index]));

        shaderc_shader_kind kind = shader_stage_to_shaderc_kind((Shader_Stage)stage_index);
        shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, source.data, source.count, kind, include_path.data, "main", options);

        HE_DEFER { shaderc_result_release(result); };

        shaderc_compilation_status status = shaderc_result_get_compilation_status(result);
        if (status != shaderc_compilation_status_success)
        {
            HE_LOG(Resource, Fetal, "failed to compile %.*s stage: %s\n", HE_EXPAND_STRING(shader_stage_signature[stage_index]), shaderc_result_get_error_message(result));

            for (U32 i = 0; i < stage_index; i++)
            {
                if (compilation_result.stages[stage_index].count)
                {
                    HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, (void *)compilation_result.stages[stage_index].data);
                }
            }

            return { .success = false };
        }

        const char *data = shaderc_result_get_bytes(result);
        U64 size = shaderc_result_get_length(result);
        String blob = { .count = size, .data = data };
        compilation_result.stages[stage_index] = copy_string(blob, memory_context.general_allocator);
    }

    compilation_result.type = shader_type;
    compilation_result.success = true;
    return compilation_result;
}

Shader_Handle load_shader(String name)
{
    Memory_Context memory_context = grab_memory_context();

    Read_Entire_File_Result result = read_entire_file( format_string(memory_context.temp_allocator, "shaders/%.*s.glsl", HE_EXPAND_STRING(name)), memory_context.temp_allocator);

    if (!result.success)
    {
        HE_LOG(Rendering, Fetal, "failed to read shader file: shaders/%.*s\n", HE_EXPAND_STRING(name));
        return Resource_Pool< Shader >::invalid_handle;
    }

    String shader_source = { .count = result.size, .data = (const char *)result.data };

    Shader_Compilation_Result compilation_result = renderer_compile_shader(shader_source, HE_STRING_LITERAL("shaders"));
    HE_ASSERT(compilation_result.success);

    Shader_Descriptor shader_descriptor =
    {
        .name = name,
        .compilation_result = &compilation_result
    };

    Shader_Handle shader = renderer_create_shader(shader_descriptor);
    HE_ASSERT(is_valid_handle(&renderer_state->shaders, shader));
    renderer_destroy_shader_compilation_result(&compilation_result);

    return shader;
}

void renderer_destroy_shader_compilation_result(Shader_Compilation_Result *result)
{
    Memory_Context memory_context = grab_memory_context();

    for (U32 stage_index = 0; stage_index < (U32)Shader_Stage::COUNT; stage_index++)
    {
        if (!result->stages[stage_index].count)
        {
            continue;
        }

        HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, (void *)result->stages[stage_index].data);
        result->stages[stage_index] = {};
    }
}

Shader_Handle renderer_create_shader(const Shader_Descriptor &descriptor)
{
    HE_ASSERT(descriptor.compilation_result->success);
    Shader_Handle shader_handle = aquire_handle(&renderer_state->shaders);
    Shader *shader = get(&renderer_state->shaders, shader_handle);
    shader->type = descriptor.compilation_result->type;
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
    Memory_Context memory_context = grab_memory_context();

    // todo(amer): can we make the shader reflection data a one memory block to free it in one step...
    Shader *shader = renderer_get_shader(shader_handle);

    for (U32 struct_index = 0; struct_index < shader->struct_count; struct_index++)
    {
        Shader_Struct *shader_struct = &shader->structs[struct_index];
        HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, shader_struct->members);
    }

    HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, shader->structs);

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->destroy_shader(shader_handle, false);
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
    pipeline_state->shader = descriptor.shader;
    pipeline_state->render_pass = descriptor.render_pass;
    pipeline_state->settings = descriptor.settings;

    return pipeline_state_handle;
}

Pipeline_State* renderer_get_pipeline_state(Pipeline_State_Handle pipeline_state_handle)
{
    return get(&renderer_state->pipeline_states, pipeline_state_handle);
}

void renderer_destroy_pipeline_state(Pipeline_State_Handle &pipeline_state_handle)
{
    renderer->destroy_pipeline_state(pipeline_state_handle, false);
    release_handle(&renderer_state->pipeline_states, pipeline_state_handle);
    pipeline_state_handle = Resource_Pool< Pipeline_State >::invalid_handle;
}

//
// Render Passes
//

Render_Pass_Handle renderer_create_render_pass(const Render_Pass_Descriptor &descriptor)
{
    Memory_Context memory_context = grab_memory_context();

    Render_Pass_Handle render_pass_handle = aquire_handle(&renderer_state->render_passes);

    Render_Pass *render_pass = renderer_get_render_pass(render_pass_handle);
    render_pass->name = copy_string(descriptor.name, memory_context.general_allocator);

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
    renderer->destroy_render_pass(render_pass_handle, false);
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
    renderer->destroy_frame_buffer(frame_buffer_handle, false);
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
    Memory_Context memory_context = grab_memory_context();

    Static_Mesh_Handle static_mesh_handle = aquire_handle(&renderer_state->static_meshes);
    Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

    if (descriptor.name.data)
    {
        static_mesh->name = copy_string(descriptor.name, memory_context.general_allocator);
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

void renderer_use_static_mesh(Static_Mesh_Handle static_mesh_handle)
{
    Frame_Render_Data *render_data = &renderer_state->render_data;

    if (render_data->current_static_mesh_handle == static_mesh_handle)
    {
        return;
    }

    render_data->current_static_mesh_handle = static_mesh_handle;

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
}

void renderer_destroy_static_mesh(Static_Mesh_Handle &static_mesh_handle)
{
    Memory_Context memory_context = grab_memory_context();

    Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

    if (static_mesh->name.data)
    {
        HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, (void *)static_mesh->name.data);
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
    Memory_Context memory_context = grab_memory_context();

    Material_Handle material_handle = aquire_handle(&renderer_state->materials);
    Material *material = get(&renderer_state->materials, material_handle);

    if (descriptor.name.count)
    {
        material->name = copy_string(descriptor.name, memory_context.general_allocator);
    }

    String pass_name = descriptor.type == Material_Type::UI ? HE_STRING_LITERAL("ui") : HE_STRING_LITERAL("world");

    Pipeline_State_Descriptor pipeline_state_desc =
    {
        .shader = descriptor.shader,
        .render_pass = get_render_pass(&renderer_state->render_graph, pass_name),
        .settings = descriptor.settings,
    };

    Pipeline_State_Handle pipeline_state_handle = renderer_create_pipeline_state(pipeline_state_desc);
    Pipeline_State *pipeline_state = get(&renderer_state->pipeline_states, pipeline_state_handle);
    Shader *shader = get(&renderer_state->shaders, descriptor.shader);
    Shader_Struct *properties = renderer_find_shader_struct(descriptor.shader, HE_STRING_LITERAL("Material"));
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

        Bind_Group_Descriptor object_bind_group_descriptor =
        {
            .shader = descriptor.shader,
            .group_index = SHADER_OBJECT_BIND_GROUP
        };

        material->bind_groups[frame_index] = renderer_create_bind_group(object_bind_group_descriptor);
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

    material->type = descriptor.type;
    material->pipeline_state_handle = pipeline_state_handle;
    material->data = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.general_allocator, U8, properties->size);
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
    Memory_Context memory_context = grab_memory_context();

    Material *material = get(&renderer_state->materials, material_handle);

    if (material->name.data)
    {
        HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, (void *)material->name.data);
    }

    Pipeline_State *pipeline_state = get(&renderer_state->pipeline_states, material->pipeline_state_handle);
    renderer_destroy_pipeline_state(material->pipeline_state_handle);

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        renderer_destroy_buffer(material->buffers[frame_index]);
        renderer_destroy_bind_group(material->bind_groups[frame_index]);
    }

    HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, material->data);
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

    if (property->is_texture_asset)
    {
        Asset_Handle previous_asset_handle = { .uuid = property->data.u64 };
        release_asset(previous_asset_handle);

        Asset_Handle asset_handle = { .uuid = data.u64 };
        if (is_asset_handle_valid(asset_handle))
        {
            acquire_asset(asset_handle);
        }
        else
        {
            U32 *texture_index = (U32 *)&material->data[property->offset_in_buffer];
            *texture_index = renderer_state->white_pixel_texture.index;
        }
    }
    else if (property->is_color)
    {
        switch (property->data_type)
        {
            case Shader_Data_Type::VECTOR3F:
            {
                // srgb to linear
                glm::vec3 *color = (glm::vec3 *)&material->data[property->offset_in_buffer];
                *color = srgb_to_linear(data.v3f);
            } break;

            case Shader_Data_Type::VECTOR4F:
            {
                // srgb to linear
                glm::vec4 *color = (glm::vec4 *)&material->data[property->offset_in_buffer];
                *color = srgb_to_linear(data.v4f);
            } break;
        }
    }
    else
    {
        memcpy(material->data + property->offset_in_buffer, &data, get_size_of_shader_data_type(property->data_type));
    }

    property->data = data;
    material->dirty_count = HE_MAX_FRAMES_IN_FLIGHT;
    return true;
}

void renderer_use_material(Material_Handle material_handle)
{
    Frame_Render_Data *render_data = &renderer_state->render_data;

    if (render_data->current_material_handle == material_handle)
    {
        return;
    }

    render_data->current_material_handle = material_handle;

    Material *material = get(&renderer_state->materials, material_handle);

    if (material->dirty_count > 0)
    {
        material->dirty_count--;

        for (U32 property_index = 0; property_index < material->properties.count; property_index++)
        {
            Material_Property *property = &material->properties[property_index];
            if (property->is_texture_asset)
            {
                U32 *texture_index = (U32 *)&material->data[property->offset_in_buffer];
                Asset_Handle texture_asset = { .uuid = property->data.u64 };
                if (is_asset_loaded(texture_asset))
                {
                    const Asset_Info *info = get_asset_info(texture_asset);
                    if (info->name == HE_STRING_LITERAL("texture"))
                    {
                        Texture_Handle texture = get_asset_handle_as<Texture>(texture_asset);
                        *texture_index = texture.index;
                    }
                    else if (info->name == HE_STRING_LITERAL("environment_map"))
                    {
                        Environment_Map *env_map = get_asset_as<Environment_Map>(texture_asset);
                        *texture_index = env_map->environment_map.index;
                    }
                }
                else
                {
                    *texture_index = renderer_state->white_pixel_texture.index;
                    material->dirty_count = HE_MAX_FRAMES_IN_FLIGHT;
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

    renderer->set_bind_groups(SHADER_OBJECT_BIND_GROUP, to_array_view(material_bind_groups));

    if (render_data->current_pipeline_state_handle.index != material->pipeline_state_handle.index)
    {
        renderer->set_pipeline_state(material->pipeline_state_handle);
        render_data->current_pipeline_state_handle = material->pipeline_state_handle;
    }
}

static const char* cull_mode_to_string(Cull_Mode mode)
{
    switch (mode)
    {
        case Cull_Mode::NONE:
        {
            return "none";
        } break;

        case Cull_Mode::FRONT:
        {
            return "front";
        } break;

        case Cull_Mode::BACK:
        {
            return "back";
        } break;

        default:
        {
            HE_ASSERT(!"unsupported cull mode");
        } break;
    }

    return "";
}

static const char* front_face_to_string(Front_Face front_face)
{
    switch (front_face)
    {
        case Front_Face::CLOCKWISE:
        {
            return "clockwise";
        } break;

        case Front_Face::COUNTER_CLOCKWISE:
        {
            return "counter_clockwise";
        } break;

        default:
        {
            HE_ASSERT(!"unsupported front face");
        } break;
    }

    return "";
}

static const char* compare_operation_to_str(Compare_Operation op)
{
    switch (op)
    {
        case Compare_Operation::NEVER: return "never";
        case Compare_Operation::LESS: return "less";
        case Compare_Operation::EQUAL: return "equal";
        case Compare_Operation::LESS_OR_EQUAL: return "less_or_equal";
        case Compare_Operation::GREATER: return "greater";
        case Compare_Operation::NOT_EQUAL: return "not_equal";
        case Compare_Operation::GREATER_OR_EQUAL: return "greater_or_equal";
        case Compare_Operation::ALWAYS: return "always";

        default:
        {
            HE_ASSERT(!"unsupported compare operation");
        } break;
    }

    return "";
}

static const char* stencil_operation_to_str(Stencil_Operation op)
{
    switch (op)
    {
        case Stencil_Operation::KEEP: return "keep";
        case Stencil_Operation::ZERO: return "zero";
        case Stencil_Operation::REPLACE: return "replace";
        case Stencil_Operation::INCREMENT_AND_CLAMP: return "increment_and_clamp";
        case Stencil_Operation::DECREMENT_AND_CLAMP: return "decrement_and_clamp";
        case Stencil_Operation::INVERT: return "invert";
        case Stencil_Operation::INCREMENT_AND_WRAP: return "increment_and_wrap";
        case Stencil_Operation::DECREMENT_AND_WRAP: return "decrement_and_wrap";

        default:
        {
            HE_ASSERT(!"unsupported stencil operation");
        } break;
    }

    return "";
}

static const char* material_type_to_str(Material_Type type)
{
    switch (type)
    {
        case Material_Type::OPAQUE:
        {
            return "opaque";
        } break;

        case Material_Type::ALPHA_CUTOFF:
        {
            return "alpha_cutoff";
        } break;

        case Material_Type::TRANSPARENT:
        {
            return "transparent";
        } break;

        default:
        {
            HE_ASSERT(!"unsupported rendering pass");
        } break;
    }

    return "";
}

bool serialize_material(Material_Handle material_handle, U64 shader_asset_uuid, String path)
{
    Memory_Context memory_context = grab_memory_context();

    Material *material = renderer_get_material(material_handle);
    Pipeline_State *pipeline_state = renderer_get_pipeline_state(material->pipeline_state_handle);

    const Pipeline_State_Settings &settings = pipeline_state->settings;

    String_Builder builder = {};
    begin_string_builder(&builder, memory_context.temprary_memory.arena);

    append(&builder, "version 1\n");
    append(&builder, "type %s\n", material_type_to_str(material->type));
    append(&builder, "shader %llu\n", shader_asset_uuid);
    append(&builder, "cull_mode %s\n", cull_mode_to_string(settings.cull_mode));
    append(&builder, "front_face %s\n", front_face_to_string(settings.front_face));

    append(&builder, "depth_operation %s\n", compare_operation_to_str(settings.depth_operation));
    append(&builder, "depth_testing %s\n", settings.depth_testing ? "true" : "false");
    append(&builder, "depth_writing %s\n", settings.depth_writing ? "true" : "false");

    append(&builder, "stencil_operation %s\n", compare_operation_to_str(settings.stencil_operation));
    append(&builder, "stencil_testing %s\n", settings.stencil_testing ? "true" : "false");
    append(&builder, "stencil_pass %s\n", stencil_operation_to_str(settings.stencil_pass));
    append(&builder, "stencil_fail %s\n", stencil_operation_to_str(settings.stencil_fail));
    append(&builder, "depth_fail %s\n", stencil_operation_to_str(settings.depth_fail));

    append(&builder, "stencil_compare_mask %u\n", settings.stencil_compare_mask);
    append(&builder, "stencil_write_mask %u\n", settings.stencil_write_mask);
    append(&builder, "stencil_reference_value %u\n", settings.stencil_reference_value);

    append(&builder, "property_count %u\n", material->properties.count);

    for (U32 i = 0; i < material->properties.count; i++)
    {
        Material_Property *property = &material->properties[i];
        bool is_texture_asset = ends_with(property->name, HE_STRING_LITERAL("texture")) || ends_with(property->name, HE_STRING_LITERAL("cubemap"));

        append(&builder, "%.*s %.*s ", HE_EXPAND_STRING(property->name), HE_EXPAND_STRING(shader_data_type_to_str(property->data_type)));
        switch (property->data_type)
        {
            case Shader_Data_Type::U8:
            case Shader_Data_Type::U16:
            case Shader_Data_Type::U64:
            {
                append(&builder, "%llu\n", property->data.u64);
            } break;

            case Shader_Data_Type::U32:
            {
                append(&builder, "%llu\n", is_texture_asset ? property->data.u64 : property->data.u32);
            } break;

            case Shader_Data_Type::S8:
            {
                append(&builder, "%ll\n", property->data.s8);
            } break;

            case Shader_Data_Type::S16:
            {
                append(&builder, "%ll\n", property->data.s16);
            } break;

            case Shader_Data_Type::S32:
            {
                append(&builder, "%ll\n", property->data.s32);
            } break;

            case Shader_Data_Type::S64:
            {
                append(&builder, "%ll\n", property->data.s64);
            } break;

            case Shader_Data_Type::F16:
            case Shader_Data_Type::F32:
            case Shader_Data_Type::F64:
            {
                append(&builder, "%f\n", property->data.f64);
            } break;

            case Shader_Data_Type::VECTOR2F:
            {
                append(&builder, "%f %f\n", property->data.v2f.x, property->data.v2f.y);
            } break;

            case Shader_Data_Type::VECTOR2S:
            {
                append(&builder, "%ll %ll\n", property->data.v2s.x, property->data.v2s.y);
            } break;

            case Shader_Data_Type::VECTOR2U:
            {
                append(&builder, "%llu %llu\n", property->data.v2u.x, property->data.v2u.y);
            } break;

            case Shader_Data_Type::VECTOR3F:
            {
                append(&builder, "%f %f %f\n", property->data.v3f.x, property->data.v3f.y, property->data.v3f.z);
            } break;

            case Shader_Data_Type::VECTOR3S:
            {
                append(&builder, "%ll %ll %ll\n", property->data.v3s.x, property->data.v3s.y, property->data.v3s.z);
            } break;

            case Shader_Data_Type::VECTOR3U:
            {
                append(&builder, "%llu %llu %llu\n", property->data.v3u.x, property->data.v3u.y, property->data.v3u.z);
            } break;

            case Shader_Data_Type::VECTOR4F:
            {
                append(&builder, "%f %f %f %f\n", property->data.v4f.x, property->data.v4f.y, property->data.v4f.z, property->data.v4f.w);
            } break;

            case Shader_Data_Type::VECTOR4S:
            {
                append(&builder, "%ll %ll %ll %ll\n", property->data.v4s.x, property->data.v4s.y, property->data.v4s.z, property->data.v4s.w);
            } break;

            case Shader_Data_Type::VECTOR4U:
            {
                append(&builder, "%llu %llu %llu %llu\n", property->data.v4u.x, property->data.v4u.y, property->data.v4u.z, property->data.v4u.w);
            } break;
        }
    }
    String contents = end_string_builder(&builder);
    bool success = write_entire_file(path, (void *)contents.data, contents.count);
    return success;
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
    scene->node_count = 0;
    scene->first_free_node_index = -1;
    Skybox *skybox = &scene->skybox;
    skybox->skybox_material_asset = 0;
    skybox->ambient_color = { 1.0f, 1.0f, 1.0f };
    return scene_handle;
}

Scene_Handle renderer_create_scene(String name, U32 node_capacity)
{
    Scene_Handle scene_handle = renderer_create_scene(node_capacity);
    Scene *scene = get(&renderer_state->scenes, scene_handle);
    allocate_node(scene, name);
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
    if (scene->nodes.count)
    {
        remove_node(scene, 0);
    }
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
        append(builder, "material_count %llu\n", static_mesh_comp->materials.count);
        for (U32 i = 0; i < static_mesh_comp->materials.count; i++)
        {
            append(builder, "material_asset %llu\n", static_mesh_comp->materials[i]);
        }
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
    Memory_Context memory_context = grab_memory_context();

    struct Serialized_Scene_Node
    {
        U32 node_index;
        S32 serialized_parent_index;
    };

    Scene *scene = get(&renderer_state->scenes, scene_handle);
    Skybox *skybox = &scene->skybox;

    Ring_Queue< Serialized_Scene_Node > queue;
    init(&queue, scene->node_count, memory_context.temp_allocator);

    U32 serialized_node_index = 0;
    push(&queue, { .node_index = 0, .serialized_parent_index = -1 });

    String_Builder builder = {};
    begin_string_builder(&builder, memory_context.temprary_memory.arena);

    append(&builder, "version 1\n");

    {
        glm::vec3 &a = skybox->ambient_color;
        append(&builder, "ambient_color %f %f %f\n", a.r, a.g, a.b);
    }

    append(&builder, "skybox_material_asset %llu\n", skybox->skybox_material_asset);
    append(&builder, "node_count %u\n", scene->node_count);

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
    Memory_Context memory_context = grab_memory_context();

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
    node->name = copy_string(name, memory_context.general_allocator);

    node->parent_index = -1;
    node->first_child_index = -1;
    node->last_child_index = -1;
    node->next_sibling_index = -1;
    node->prev_sibling_index = -1;

    node->transform = get_identity_transform();

    node->has_mesh = false;
    node->has_light = false;

    scene->node_count++;
    return node_index;
}

void add_child_last(Scene *scene, S32 parent_index, U32 node_index)
{
    HE_ASSERT(scene);

    Scene_Node *parent = get_node(scene, parent_index);
    Scene_Node *node = get_node(scene, node_index);
    node->parent_index = parent_index;

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

void add_child_first(Scene *scene, S32 parent_index, U32 node_index)
{
    HE_ASSERT(scene);

    Scene_Node *parent = get_node(scene, parent_index);
    Scene_Node *node = get_node(scene, node_index);

    node->parent_index = parent_index;

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

void add_child_after(Scene *scene, U32 target_node_index, U32 node_index)
{
    HE_ASSERT(scene);

    Scene_Node *target_node = get_node(scene, target_node_index);
    Scene_Node *parent = get_node(scene, target_node->parent_index);
    Scene_Node *node = get_node(scene, node_index);
    U32 parent_index = target_node->parent_index;

    node->parent_index = parent_index;

    if (parent->last_child_index == target_node_index)
    {
        node->prev_sibling_index = parent->last_child_index;
        scene->nodes[parent->last_child_index].next_sibling_index = node_index;
        parent->last_child_index = node_index;
    }
    else
    {
        node->next_sibling_index = target_node->next_sibling_index;
        node->prev_sibling_index = target_node_index;

        scene->nodes[target_node->next_sibling_index].prev_sibling_index = node_index;
        target_node->next_sibling_index = node_index;
    }
}

void remove_child(Scene *scene, S32 parent_index, U32 node_index)
{
    Scene_Node *parent = get_node(scene, parent_index);
    Scene_Node *node = get_node(scene, node_index);
    HE_ASSERT(node->parent_index == parent_index);

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

void remove_node(Scene *scene, U32 node_index)
{
    Memory_Context memory_context = grab_memory_context();

    Scene_Node *node = get_node(scene, node_index);

    for (S32 child_node_index = node->first_child_index; child_node_index != -1; child_node_index = get_node(scene, child_node_index)->next_sibling_index)
    {
        remove_node(scene, child_node_index);
    }

    if (node->parent_index != -1)
    {
        remove_child(scene, node->parent_index, node_index);
    }

    HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, (void *)node->name.data);

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

    HE_ASSERT(scene->node_count);
    scene->node_count--;
}

static void traverse_scene_tree(Scene *scene, U32 node_index, Transform parent_transform, Frame_Render_Data *render_data)
{
    Scene_Node *node = get_node(scene, node_index);

    Transform transform = combine(parent_transform, node->transform);

    if (node->has_mesh)
    {
        Static_Mesh_Component *static_mesh_comp = &node->mesh;
        Asset_Handle static_mesh_asset = { .uuid = static_mesh_comp->static_mesh_asset };
        if (is_asset_loaded(static_mesh_asset))
        {
            Static_Mesh_Handle static_mesh_handle = get_asset_handle_as<Static_Mesh>(static_mesh_asset);

            Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);
            if (static_mesh->is_uploaded_to_gpu)
            {
                HE_ASSERT(render_data->instance_count < HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT);
                U32 instance_index = render_data->instance_count++;
                Shader_Instance_Data *object_data = &render_data->instance_base[instance_index];
                object_data->local_to_world = get_world_matrix(transform);
                object_data->entity_index = node_index;

                const Dynamic_Array< Sub_Mesh > &sub_meshes = static_mesh->sub_meshes;
                for (U32 sub_mesh_index = 0; sub_mesh_index < sub_meshes.count; sub_mesh_index++)
                {
                    const Sub_Mesh *sub_mesh = &sub_meshes[sub_mesh_index];

                    Material_Handle material_handle = renderer_state->default_material;

                    Asset_Handle material_asset = { .uuid = static_mesh_comp->materials[sub_mesh_index] };

                    if (is_asset_loaded(material_asset))
                    {
                        material_handle = get_asset_handle_as<Material>(material_asset);
                    }

                    HE_ASSERT(is_valid_handle(&renderer_state->static_meshes, static_mesh_handle));
                    HE_ASSERT(is_valid_handle(&renderer_state->materials, material_handle));

                    Material *material = renderer_get_material(material_handle);

                    Dynamic_Array< Draw_Command > *command_list = nullptr;

                    switch (material->type)
                    {
                        case Material_Type::OPAQUE:
                        {
                            command_list = &render_data->opaque_commands;
                        } break;

                        case Material_Type::ALPHA_CUTOFF:
                        {
                            command_list = &render_data->alpha_cutoff_commands;
                        } break;

                        case Material_Type::TRANSPARENT:
                        {
                            command_list = &render_data->transparent_commands;
                        } break;
                    }

                    Draw_Command &draw_command = append(command_list);
                    draw_command.static_mesh = static_mesh_handle;
                    draw_command.sub_mesh_index = sub_mesh_index;
                    draw_command.material = material_handle;
                    draw_command.instance_index = instance_index;

                    if (node_index == render_data->selected_node_index)
                    {
                        Dynamic_Array< Draw_Command > *outlines = &render_data->outline_commands;
                        Draw_Command &draw_command = append(outlines);
                        draw_command.static_mesh = static_mesh_handle;
                        draw_command.sub_mesh_index = sub_mesh_index;
                        draw_command.material = renderer_state->default_material;
                        draw_command.instance_index = instance_index;
                    }
                }
            }
        }
    }

    if (node->has_light)
    {
        Light_Component *light_comp = &node->light;

        Shader_Light &light = append(&render_data->lights);
        render_data->globals->light_count++;

        light.type = (U32)light_comp->type;

        glm::vec3 *light_direction = (glm::vec3 *)light.direction;
        *light_direction = glm::rotate(transform.rotation, { 0.0f, 0.0f, -1.0f, 0.0f });

        glm::vec3 *light_position = (glm::vec3 *)light.position;
        *light_position = transform.position;

        light.radius = light_comp->radius;
        light.outer_angle = glm::radians(light_comp->outer_angle);
        light.inner_angle = glm::radians(light_comp->inner_angle);

        glm::vec3 *light_color = (glm::vec3 *)light.color;
        *light_color = srgb_to_linear(light_comp->color) * light_comp->intensity;
    }

    for (S32 child_node_index = node->first_child_index; child_node_index != -1; child_node_index = get_node(scene, child_node_index)->next_sibling_index)
    {
        traverse_scene_tree(scene, child_node_index, transform, render_data);
    }
}

void render_scene(Scene_Handle scene_handle)
{
    Scene *scene = renderer_get_scene(scene_handle);
    Skybox *skybox = &scene->skybox;
    Asset_Handle skybox_material_asset = { .uuid = skybox->skybox_material_asset };

    Frame_Render_Data *render_data = &renderer_state->render_data;

    if (is_asset_loaded(skybox_material_asset))
    {
        U32 instance_index = render_data->instance_count++;
        Shader_Instance_Data *object_data = &render_data->instance_base[instance_index];
        object_data->local_to_world = get_world_matrix(get_identity_transform());
        object_data->entity_index = -1;

        Draw_Command &dc = append(&render_data->skybox_commands);
        dc.static_mesh = renderer_state->default_static_mesh;
        dc.sub_mesh_index = 0;
        dc.material = get_asset_handle_as<Material>(skybox_material_asset);
        dc.instance_index = instance_index;

        glm::vec3 *ambient = (glm::vec3 *)render_data->globals->ambient;
        *ambient = srgb_to_linear(skybox->ambient_color);
    }

    traverse_scene_tree(scene, 0, get_identity_transform(), render_data);
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

    for (S32 index = 0; index < (S32)renderer_state->pending_upload_requests.count; index++)
    {
        Upload_Request_Handle upload_request_handle = renderer_state->pending_upload_requests[index];
        Upload_Request *upload_request = get(&renderer_state->upload_requests, upload_request_handle);
        U64 semaphore_value = renderer_get_semaphore_value(upload_request->semaphore);
        if (upload_request->target_value == semaphore_value)
        {
            HE_ASSERT(upload_request->uploaded);
            HE_ASSERT(*upload_request->uploaded == false);
            *upload_request->uploaded = true;
            if (is_valid_handle(&renderer_state->textures, upload_request->texture))
            {
                platform_lock_mutex(&renderer_state->render_commands_mutex);
                renderer->imgui_add_texture(upload_request->texture);
                platform_unlock_mutex(&renderer_state->render_commands_mutex);
            }
            renderer_destroy_upload_request(upload_request_handle);
            remove_and_swap_back(&renderer_state->pending_upload_requests, index);
            index--;
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
        renderer->destroy_sampler(renderer_state->default_texture_sampler, true);
    }

    renderer->create_sampler(renderer_state->default_texture_sampler, default_sampler_descriptor);
    renderer_state->anisotropic_filtering_setting = anisotropic_filtering_setting;
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

static U8* get_pointer(Shader_Struct *_struct, U8 *data, String name)
{
    for (U32 i = 0; i < _struct->member_count; i++)
    {
        Shader_Struct_Member *member = &_struct->members[i];
        if (member->name == name)
        {
            return &data[member->offset];
        }
    }

    return nullptr;
}

void begin_rendering(const Camera *camera)
{
    Memory_Context memory_context = grab_memory_context();

    renderer->begin_frame();

    U32 frame_index = renderer_state->current_frame_in_flight_index;

    Frame_Render_Data *render_data = &renderer_state->render_data;
    Buffer *global_uniform_buffer = get(&renderer_state->buffers, render_data->globals_uniform_buffers[frame_index]);

    Shader_Globals *globals = (Shader_Globals *)global_uniform_buffer->data;
    render_data->globals = globals;

    globals->gamma = renderer_state->gamma;
    globals->light_count = 0;

    globals->max_node_count = renderer_state->back_buffer_width * renderer_state->back_buffer_height * 20;

    globals->view = camera->view;
    glm::mat4 proj = camera->projection;
    proj[1][1] *= -1;
    globals->projection = proj;

    glm::vec3 *eye = (glm::vec3 *)globals->eye;
    *eye = camera->position;

    render_data->view = camera->view;
    render_data->projection = camera->projection;
    render_data->near_z = camera->near_clip;
    render_data->far_z = camera->far_clip;

    globals->z_near = camera->near_clip;
    globals->z_far = camera->far_clip;

    static bool ls_use_enviornment_map = true;
    ImGui::Begin("Environment_Mapping");
    ImGui::Checkbox("Use Environment Map", &ls_use_enviornment_map);
    globals->use_environment_map = (U32)ls_use_enviornment_map;
    ImGui::End();

    globals->brdf_lut = renderer_state->brdf_lut_texture.index;

    Asset_Handle environment_map_asset = import_asset(HE_STRING_LITERAL("env_map.hdr"));

    if (is_asset_handle_valid(environment_map_asset) && is_asset_loaded(environment_map_asset))
    {
        Environment_Map *env_map = get_asset_as<Environment_Map>(environment_map_asset);
        globals->irradiance_map = env_map->irradiance_map.index;
        Texture *prefilter_map = renderer_get_texture(env_map->prefilter_map);
        globals->prefilter_map_lod = prefilter_map->mip_levels;
        globals->prefilter_map = env_map->prefilter_map.index;
    }
    else
    {
        globals->irradiance_map = renderer_state->default_cubemap.index;
        globals->prefilter_map = renderer_state->default_cubemap.index;
    }

    Buffer *instance_storage_buffer = get(&renderer_state->buffers, render_data->instance_storage_buffers[frame_index]);
    render_data->instance_base = (Shader_Instance_Data *)instance_storage_buffer->data;
    render_data->instance_count = 0;

    Buffer *light_storage_buffer = get(&renderer_state->buffers, render_data->light_storage_buffers[frame_index]);

    Buffer_Handle light_bins_buffer_handle = render_data->light_bins[frame_index];
    bool recreate_light_bins_buffer = !is_valid_handle(&renderer_state->buffers, light_bins_buffer_handle);

    if (is_valid_handle(&renderer_state->buffers, light_bins_buffer_handle))
    {
        Buffer *light_bins_buffer = renderer_get_buffer(light_bins_buffer_handle);
        if (light_bins_buffer->size != render_data->light_bin_count * sizeof(U32))
        {
            recreate_light_bins_buffer = true;
            renderer_destroy_buffer(light_bins_buffer_handle);
        }
    }

    if (recreate_light_bins_buffer)
    {
        Buffer_Descriptor light_bins_buffer_descriptor =
        {
            .size = render_data->light_bin_count * sizeof(U32),
            .usage = Buffer_Usage::STORAGE_CPU_SIDE
        };
        render_data->light_bins[frame_index] = renderer_create_buffer(light_bins_buffer_descriptor);
    }

    render_data->current_pipeline_state_handle = Resource_Pool< Pipeline_State >::invalid_handle;
    render_data->current_material_handle = Resource_Pool< Material >::invalid_handle;
    render_data->current_static_mesh_handle = Resource_Pool< Static_Mesh >::invalid_handle;

    reset(&render_data->skybox_commands);
    reset(&render_data->opaque_commands);
    reset(&render_data->alpha_cutoff_commands);
    reset(&render_data->transparent_commands);
    reset(&render_data->outline_commands);
    reset(&render_data->lights);

    U32 texture_count = renderer_state->textures.capacity;
    Texture_Handle *textures = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, Texture_Handle, texture_count);
    Sampler_Handle *samplers = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, Sampler_Handle, texture_count);

    platform_lock_mutex(&renderer_state->textures.mutex);

    for (auto it = iterator(&renderer_state->textures); next(&renderer_state->textures, it);)
    {
        Texture *texture = get(&renderer_state->textures, it);

        if (texture->is_attachment || !texture->is_uploaded_to_gpu || texture->is_storage)
        {
            textures[it.index] = renderer_state->white_pixel_texture;
        }
        else
        {
            textures[it.index] = it;
        }

        samplers[it.index] = texture->is_cubemap ? renderer_state->default_cubemap_sampler : renderer_state->default_texture_sampler;
    }

    platform_unlock_mutex(&renderer_state->textures.mutex);

    Update_Binding_Descriptor update_globals_bindings[] =
    {
        {
            .binding_number = SHADER_GLOBALS_UNIFORM_BINDING,
            .element_index = 0,
            .count = 1,
            .buffers = &render_data->globals_uniform_buffers[frame_index]
        },
        {
            .binding_number = SHADER_INSTANCE_STORAGE_BUFFER_BINDING,
            .element_index = 0,
            .count = 1,
            .buffers = &render_data->instance_storage_buffers[frame_index]
        },
    };
    renderer_update_bind_group(render_data->globals_bind_groups[frame_index], to_array_view(update_globals_bindings));

    Update_Binding_Descriptor update_pass_bindings[] =
    {
        {
            .binding_number = SHADER_LIGHT_STORAGE_BUFFER_BINDING,
            .element_index = 0,
            .count = 1,
            .buffers = &render_data->light_storage_buffers[frame_index]
        },
        {
            .binding_number = SHADER_LIGHT_BINS_STORAGE_BUFFER_BINDING,
            .element_index = 0,
            .count = 1,
            .buffers = &render_data->light_bins[frame_index]
        },
        {
            .binding_number = SHADER_BINDLESS_TEXTURES_BINDING,
            .element_index = 0,
            .count = texture_count,
            .textures = textures,
            .samplers = samplers
        },
    };

    renderer_update_bind_group(render_data->pass_bind_groups[frame_index], to_array_view(update_pass_bindings));

    Bind_Group_Handle bind_groups[] =
    {
        render_data->globals_bind_groups[frame_index],
        render_data->pass_bind_groups[frame_index]
    };

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->set_bind_groups(SHADER_GLOBALS_BIND_GROUP, to_array_view(bind_groups));
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
}

static bool calc_light_aabb(Shader_Light *light, const glm::vec3 &view_p, Frame_Render_Data *render_data)
{
    U16 width = renderer_state->back_buffer_width;
    U16 height = renderer_state->back_buffer_height;

    glm::uvec2 *light_screen_aabb = (glm::uvec2 *)&light->screen_aabb;
    glm::vec3 *light_position = (glm::vec3 *)&light->position;

    bool camera_inside = (glm::length(view_p) - light->radius) < render_data->near_z;

    if (camera_inside)
    {
        *light_screen_aabb = { 0, (width - 1) | ((height - 1) << 16) };
        return true;
    }

    glm::vec2 aabb_min = { HE_MAX_F32, HE_MAX_F32 };
    glm::vec2 aabb_max = { HE_MIN_F32, HE_MIN_F32 };

    for (U32 corner_index = 0; corner_index < 8; corner_index++)
    {
        F32 cx = (corner_index % 2) ? 1.0f : -1.0f;
        F32 cy = (corner_index & 2) ? 1.0f : -1.0f;
        F32 cz = (corner_index & 4) ? 1.0f : -1.0f;
        glm::vec3 corner_world_pos = glm::vec3(cx, cy, cz) * light->radius + *light_position;
        glm::vec4 corner_view_pos = render_data->view * glm::vec4(corner_world_pos, 1.0f);

        if (corner_view_pos.z > -render_data->near_z + 0.0001f)
        {
            corner_view_pos.z = -render_data->near_z;
        }

        glm::vec4 corner_ndc = render_data->projection * corner_view_pos;
        corner_ndc /= corner_ndc.w;

        glm::vec2 corner_ndc_xy = { corner_ndc.x, corner_ndc.y };
        aabb_min = glm::min(aabb_min, corner_ndc_xy);
        aabb_max = glm::max(aabb_max, corner_ndc_xy);
    }

    {
        F32 min_y = aabb_min.y;
        F32 max_y = aabb_max.y;

        aabb_min.y = max_y * -1.0f;
        aabb_max.y = min_y * -1.0f;
    }

    glm::vec2 aabb_size = aabb_max - aabb_min;

    F32 threshold_size = 0.0001f;
    if (aabb_min.x > 1.0f || aabb_max.x < -1.0f || aabb_min.y > 1.0f || aabb_max.y < -1.0f || aabb_size.x < threshold_size || aabb_size.y < threshold_size)
    {
        return false;
    }

    aabb_min = glm::clamp(aabb_min, -1.0f, 1.0f);
    aabb_max = glm::clamp(aabb_max, -1.0f, 1.0f);

    glm::vec2 half = { 0.5f, 0.5f };
    glm::vec2 screen_size_minus_one = { renderer_state->back_buffer_width - 1, renderer_state->back_buffer_height - 1 };

    glm::vec2 screen_aabb_min = (aabb_min * 0.5f + half) * screen_size_minus_one;
    glm::vec2 screen_aabb_max = (aabb_max * 0.5f + half) * screen_size_minus_one;

    U32 min_x = (U32)screen_aabb_min.x;
    U32 min_y = (U32)screen_aabb_min.y;

    U32 max_x = (U32)screen_aabb_max.x;
    U32 max_y = (U32)screen_aabb_max.y;

    *light_screen_aabb = { min_x | (min_y << 16), max_x | (max_y << 16) };
    return true;
}

void end_rendering()
{
    Memory_Context memory_context = grab_memory_context();

    Frame_Render_Data *render_data = &renderer_state->render_data;

    U32 width = renderer_state->back_buffer_width;
    U32 height = renderer_state->back_buffer_height;

    U32 total_light_count = render_data->globals->light_count;
    U32 light_count = render_data->globals->light_count;
    Shader_Light *lights = render_data->lights.data;

    struct Sorted_Light
    {
        F32 depth;
        F32 min_depth;
        F32 max_depth;
        U16 index;
    };

    U32 sorted_light_count = 0;
    Sorted_Light *sorted_lights = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp_allocator, Sorted_Light, light_count + 1);

    F32 one_over_render_dist = 1.0f / (render_data->far_z - render_data->near_z);
    U32 directional_light_count = 0;

    for (U32 light_index = 0; light_index < light_count; light_index++)
    {
        Shader_Light *light = &lights[light_index];
        glm::vec3 *light_position = (glm::vec3 *)light->position;
        glm::uvec2 *light_screen_aabb = (glm::uvec2 *)light->screen_aabb;

        Sorted_Light *sorted_light = &sorted_lights[sorted_light_count];
        sorted_light->index = light_index;

        if (light->type == (U32)Light_Type::DIRECTIONAL)
        {
            *light_screen_aabb = { 0, (width - 1) | ((height - 1) << 16) };
            sorted_light->depth = -HE_MAX_F32;
            sorted_light->min_depth = 0.0f;
            sorted_light->max_depth = 1.0f;
            directional_light_count++;
            sorted_light_count++;
            continue;
        }

        glm::vec4 view_p = render_data->view * glm::vec4(*light_position, 1.0f);

        F32 depth = (-view_p.z - render_data->near_z) * one_over_render_dist;
        F32 min_depth = (-view_p.z - light->radius - render_data->near_z) * one_over_render_dist;
        F32 max_depth = (-view_p.z + light->radius - render_data->near_z) * one_over_render_dist;

        if (min_depth > 1.0f || max_depth < 0.0f)
        {
            continue;
        }

        if (!calc_light_aabb(light, view_p, render_data))
        {
            continue;
        }

        sorted_light->depth = depth;
        sorted_light->min_depth = min_depth;
        sorted_light->max_depth = max_depth;
        sorted_light_count++;
    }

    light_count = sorted_light_count;
    render_data->globals->light_count = sorted_light_count;
    render_data->globals->directional_light_count = directional_light_count;
    
    std::sort(sorted_lights, sorted_lights + light_count, [](const Sorted_Light &a, const Sorted_Light &b) { return a.depth < b.depth; });

    Buffer *light_storage_buffer = renderer_get_buffer(render_data->light_storage_buffers[renderer_state->current_frame_in_flight_index]);
    Shader_Light *light_stroage = (Shader_Light *)light_storage_buffer->data;

    for (U32 i = 0; i < light_count; i++)
    {
        light_stroage[i] = lights[ sorted_lights[i].index ];
    }

    Buffer *light_binds_buffer = renderer_get_buffer(render_data->light_bins[renderer_state->current_frame_in_flight_index]);

    U32 *light_bins = (U32 *)light_binds_buffer->data;

    F32 light_bin_size = 1.0f / (F32)render_data->light_bin_count;
    U32 current_min_light_index = directional_light_count;

    for (U32 bin_index = 0; bin_index < render_data->light_bin_count; bin_index++)
    {
        U32 min_light_index = light_count + 1;
        U32 max_light_index = 0;

        F32 eps = 0.001f;
        F32 bin_min = (bin_index * light_bin_size) - eps;
        F32 bin_max = (bin_index + 1) * light_bin_size + eps;

        for (U32 light_index = current_min_light_index; light_index < light_count; light_index++)
        {
            const Sorted_Light *sorted_light = &sorted_lights[light_index];

            if ((sorted_light->depth >= bin_min && sorted_light->depth <= bin_max) ||
                (sorted_light->min_depth >= bin_min && sorted_light->min_depth <= bin_max) ||
                (sorted_light->max_depth >= bin_min && sorted_light->max_depth <= bin_max))
            {
                if (light_index < min_light_index)
                {
                    min_light_index = light_index;
                    current_min_light_index = light_index;
                }

                if (light_index > max_light_index)
                {
                    max_light_index = light_index;
                }
            }
            else if (sorted_light->min_depth > bin_max)
            {
                break;
            }
        }

        light_bins[bin_index] = min_light_index|(max_light_index << 16);
    }

    render(&renderer_state->render_graph, renderer, renderer_state);
    renderer->end_frame();
    
    renderer_state->current_frame_in_flight_index++;
    if (renderer_state->current_frame_in_flight_index >= renderer_state->frames_in_flight)
    {
        renderer_state->current_frame_in_flight_index = 0;
    }
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
    ImGuizmo::BeginFrame();
    
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