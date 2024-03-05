#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"

#include "core/platform.h"
#include "core/cvars.h"
#include "core/memory.h"
#include "core/engine.h"
#include "core/file_system.h"
#include "core/job_system.h"
#include "core/logging.h"

#include "containers/queue.h"

#include "resources/resource_system.h"

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

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <imgui.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include "renderer.h"

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
            renderer->create_bind_group = &vulkan_renderer_create_bind_group;
            renderer->set_bind_groups = &vulkan_renderer_set_bind_groups;
            renderer->update_bind_group = &vulkan_renderer_update_bind_group;
            renderer->destroy_bind_group = &vulkan_renderer_destroy_bind_group;
            renderer->create_render_pass = &vulkan_renderer_create_render_pass;
            renderer->begin_render_pass = &vulkan_renderer_begin_render_pass;
            renderer->end_render_pass = &vulkan_renderer_end_render_pass;
            renderer->destroy_render_pass = &vulkan_renderer_destroy_render_pass;
            renderer->create_frame_buffer = &vulkan_renderer_create_frame_buffer;
            renderer->destroy_frame_buffer = &vulkan_renderer_destroy_frame_buffer;
            renderer->create_semaphore = &vulkan_renderer_create_semaphore;
            renderer->get_semaphore_value = &vulkan_renderer_get_semaphore_value;
            renderer->destroy_semaphore = &vulkan_renderer_destroy_semaphore;
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

    init(&renderer_state->buffers, HE_MAX_BUFFER_COUNT);
    init(&renderer_state->textures, HE_MAX_TEXTURE_COUNT);
    init(&renderer_state->samplers, HE_MAX_SAMPLER_COUNT);
    init(&renderer_state->shaders, HE_MAX_SHADER_COUNT);
    init(&renderer_state->pipeline_states, HE_MAX_PIPELINE_STATE_COUNT);
    init(&renderer_state->bind_groups, HE_MAX_BIND_GROUP_COUNT);
    init(&renderer_state->render_passes, HE_MAX_RENDER_PASS_COUNT);
    init(&renderer_state->frame_buffers, HE_MAX_FRAME_BUFFER_COUNT);
    init(&renderer_state->semaphores, HE_MAX_SEMAPHORE_COUNT);
    init(&renderer_state->materials,  HE_MAX_MATERIAL_COUNT);
    init(&renderer_state->static_meshes, HE_MAX_STATIC_MESH_COUNT);
    init(&renderer_state->scenes, HE_MAX_SCENE_COUNT);

    Scene_Node *root_scene_node = &renderer_state->root_scene_node;
    root_scene_node->name = HE_STRING_LITERAL("Root");
    root_scene_node->local_transform = root_scene_node->global_transform = get_identity_transform();
    root_scene_node->parent = nullptr;
    root_scene_node->last_child = nullptr;
    root_scene_node->first_child = nullptr;
    root_scene_node->next_sibling = nullptr;
    root_scene_node->static_mesh_uuid = HE_MAX_U64;

    platform_create_mutex(&renderer_state->root_scene_node_mutex);

    bool render_commands_mutex_created = platform_create_mutex(&renderer_state->render_commands_mutex);
    HE_ASSERT(render_commands_mutex_created);

    bool allocation_groups_mutex_created = platform_create_mutex(&renderer_state->allocation_groups_mutex);
    HE_ASSERT(allocation_groups_mutex_created);

    reset(&renderer_state->allocation_groups);

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
        void* while_pixel_datas[] = { white_pixel_data };

        Allocation_Group allocation_group =
        {
            .resource_name = HE_STRING_LITERAL("white pixel"),
            .semaphore = renderer_create_semaphore(semaphore_descriptor)
        };
        append(&allocation_group.allocations, (void*)white_pixel_data);

        Texture_Descriptor white_pixel_descriptor =
        {
            .width = 1,
            .height = 1,
            .format = Texture_Format::R8G8B8A8_SRGB,
            .data = to_array_view(while_pixel_datas),
            .mipmapping = false,
            .allocation_group = &append(&renderer_state->allocation_groups, allocation_group)
        };

        renderer_state->white_pixel_texture = renderer_create_texture(white_pixel_descriptor);
    }

    {
        U32 *normal_pixel_data = HE_ALLOCATE(&renderer_state->transfer_allocator, U32);
        *normal_pixel_data = 0xFFFF8080; // todo(amer): endianness
        HE_ASSERT(HE_ARCH_X64);

        void* normal_pixel_datas[] = { normal_pixel_data };

        Allocation_Group allocation_group =
        {
            .resource_name = HE_STRING_LITERAL("normal pixel"),
            .semaphore = renderer_create_semaphore(semaphore_descriptor),
        };
        append(&allocation_group.allocations, (void*)normal_pixel_data);

        Texture_Descriptor normal_pixel_descriptor =
        {
            .width = 1,
            .height = 1,
            .format = Texture_Format::R8G8B8A8_SRGB,
            .data = to_array_view(normal_pixel_datas),
            .mipmapping = false,
            .allocation_group = &append(&renderer_state->allocation_groups, allocation_group)
        };

        renderer_state->normal_pixel_texture = renderer_create_texture(normal_pixel_descriptor);
    }

    {
        U16 _indicies[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35 };
        U16 *indicies = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, U16, HE_ARRAYCOUNT(_indicies));
        copy_memory(indicies, _indicies, sizeof(U16) * HE_ARRAYCOUNT(_indicies));

        U32 index_count = HE_ARRAYCOUNT(_indicies);

        glm::vec3 _positions[] = { { 1.000000f, -1.000000f, 1.000000f }, { -1.000000f, -1.000000f, -1.000000f }, { 1.000000f, -1.000000f, -1.000000f }, { -1.000000f, 1.000000f, -1.000000f }, { 0.999999f, 1.000000f, 1.000001f }, { 1.000000f, 1.000000f, -0.999999f },{ 1.000000f, 1.000000f, -0.999999f },{ 1.000000f, -1.000000f, 1.000000f },{ 1.000000f, -1.000000f, -1.000000f }, { 0.999999f, 1.000000f, 1.000001f },{ -1.000000f, -1.000000f, 1.000000f },{ 1.000000f, -1.000000f, 1.000000f },{ -1.000000f, -1.000000f, 1.000000f },{ -1.000000f, 1.000000f, -1.000000f },{ -1.000000f, -1.000000f, -1.000000f },{ 1.000000f, -1.000000f, -1.000000f },{ -1.000000f, 1.000000f, -1.000000f },{ 1.000000f, 1.000000f, -0.999999f },{ 1.000000f, -1.000000f, 1.000000f },{ -1.000000f, -1.000000f, 1.000000f },{ -1.000000f, -1.000000f, -1.000000f },{ -1.000000f, 1.000000f, -1.000000f },{ -1.000000f, 1.000000f, 1.000000f },{ 0.999999f, 1.000000f, 1.000001f },{ 1.000000f, 1.000000f, -0.999999f },{ 0.999999f, 1.000000f, 1.000001f },{ 1.000000f, -1.000000f, 1.000000f },{ 0.999999f, 1.000000f, 1.000001f },{ -1.000000f, 1.000000f, 1.000000f },{ -1.000000f, -1.000000f, 1.000000f },{ -1.000000f, -1.000000f, 1.000000f },{ -1.000000f, 1.000000f, 1.000000f },{ -1.000000f, 1.000000f, -1.000000f },{ 1.000000f, -1.000000f, -1.000000f },{ -1.000000f, -1.000000f, -1.000000f },{ -1.000000f, 1.000000f, -1.000000f } };

        glm::vec3 *positions = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, glm::vec3, HE_ARRAYCOUNT(_positions));
        copy_memory(positions, _positions, sizeof(glm::vec3) * HE_ARRAYCOUNT(_positions));

        U32 vertex_count = HE_ARRAYCOUNT(_positions);

        glm::vec2 _uvs[] = { { 0.000000f, 0.000000f },{ -1.000000f, 1.000000f },{ 0.000000f, 1.000000f },{ 0.000000f, 0.000000f },{ 1.000000f, -1.000000f },{ 1.000000f, -0.000000f },{ 1.000000f, 0.000000f },{ 0.000000f, -1.000000f },{ 1.000000f, -1.000000f },{ 1.000000f, 0.000000f },{ -0.000000f, -1.000000f },{ 1.000000f, -1.000000f },{ 0.000000f, 0.000000f },{ 1.000000f, 1.000000f },{ 1.000000f, 0.000000f },{ 0.000000f, 0.000000f },{ -1.000000f, 1.000000f },{ 0.000000f, 1.000000f },{ 0.000000f, 0.000000f },{ -1.000000f, 0.000000f },{ -1.000000f, 1.000000f },{ 0.000000f, 0.000000f },{ -0.000000f, -1.000000f },{ 1.000000f, -1.000000f },{ 1.000000f, 0.000000f },{ -0.000000f, 0.000000f },{ 0.000000f, -1.000000f },{ 1.000000f, 0.000000f },{ -0.000000f, 0.000000f },{ -0.000000f, -1.000000f },{ 0.000000f, 0.000000f },{ 0.000000f, 1.000000f },{ 1.000000f, 1.000000f },{ 0.000000f, 0.000000f },{ -1.000000f, 0.000000f },{ -1.000000f, 1.000000f } };

        glm::vec2 *uvs = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, glm::vec2, HE_ARRAYCOUNT(_uvs));
        copy_memory(uvs, _uvs, sizeof(glm::vec2) * HE_ARRAYCOUNT(_uvs));

        glm::vec3 _normals[] = { { -0.000000f, -1.000000f, 0.000000f },{ -0.000000f, -1.000000f, 0.000000f },{ -0.000000f, -1.000000f, 0.000000f },{ 0.000000f, 1.000000f, -0.000000f },{ 0.000000f, 1.000000f, -0.000000f },{ 0.000000f, 1.000000f, -0.000000f },{ 1.000000f, -0.000000f, -0.000000f },{ 1.000000f, -0.000000f, -0.000000f },{ 1.000000f, -0.000000f, -0.000000f },{ -0.000000f, -0.000000f, 1.000000f },{ -0.000000f, -0.000000f, 1.000000f },{ -0.000000f, -0.000000f, 1.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ 0.000000f, 0.000000f, -1.000000f },{ 0.000000f, 0.000000f, -1.000000f },{ 0.000000f, 0.000000f, -1.000000f },{ 0.000000f, -1.000000f, 0.000000f },{ 0.000000f, -1.000000f, 0.000000f },{ 0.000000f, -1.000000f, 0.000000f },{ 0.000000f, 1.000000f, 0.000000f },{ 0.000000f, 1.000000f, 0.000000f },{ 0.000000f, 1.000000f, 0.000000f },{ 1.000000f, 0.000000f, 0.000001f },{ 1.000000f, 0.000000f, 0.000001f },{ 1.000000f, 0.000000f, 0.000001f },{ -0.000000f, 0.000000f, 1.000000f },{ -0.000000f, 0.000000f, 1.000000f },{ -0.000000f, 0.000000f, 1.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ -1.000000f, -0.000000f, -0.000000f },{ 0.000000f, 0.000000f, -1.000000f },{ 0.000000f, 0.000000f, -1.000000f },{ 0.000000f, 0.000000f, -1.000000f } };

        glm::vec3 *normals = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, glm::vec3, HE_ARRAYCOUNT(_normals));
        copy_memory(normals, _normals, sizeof(glm::vec3) * HE_ARRAYCOUNT(_normals));

        glm::vec4 _tangents[] = { { 1.000000f, -0.000000f, -0.000000f, -1.000000f },{ 1.000000f, -0.000000f, -0.000000f, -1.000000f },{ 1.000000f, -0.000000f, -0.000000f, -1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, -0.000000f, 0.000000f, 1.000000f },{ 0.000000f, 0.000000f, -1.000000f, 1.000000f },{ 0.000000f, 0.000000f, -1.000000f, 1.000000f },{ 0.000000f, 0.000000f, -1.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f },{ 1.000000f, -0.000000f, -0.000000f, -1.000000f },{ 1.000000f, 0.000000f, -0.000000f, -1.000000f },{ 1.000000f, -0.000000f, -0.000000f, -1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 0.000001f, 0.000000f, -1.000000f, 1.000000f },{ 0.000001f, 0.000000f, -1.000000f, 1.000000f },{ 0.000001f, 0.000000f, -1.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 1.000000f, 0.000000f, 0.000000f, 1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 0.000000f, -0.000000f, -1.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f },{ 1.000000f, -0.000000f, 0.000000f, -1.000000f } };

        glm::vec4 *tangents = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, glm::vec4, HE_ARRAYCOUNT(_tangents));
        copy_memory(tangents, _tangents, sizeof(glm::vec4) * HE_ARRAYCOUNT(_tangents));

        Allocation_Group allocation_group =
        {
            .resource_name = HE_STRING_LITERAL("cube static mesh"),
            .semaphore = renderer_create_semaphore(semaphore_descriptor),
        };

        append(&allocation_group.allocations, (void *)indicies);
        append(&allocation_group.allocations, (void *)positions);
        append(&allocation_group.allocations, (void *)normals);
        append(&allocation_group.allocations, (void *)uvs);
        append(&allocation_group.allocations, (void *)tangents);

        Dynamic_Array< Sub_Mesh > sub_meshes;
        init(&sub_meshes, 1, 1);

        Sub_Mesh &sub_mesh = sub_meshes[0];

        sub_mesh.vertex_offset = 0;
        sub_mesh.index_offset = 0;

        sub_mesh.index_count = index_count;
        sub_mesh.vertex_count = vertex_count;

        sub_mesh.material_uuid = HE_MAX_U64; // todo(amer): should we use handles here...

        Static_Mesh_Descriptor cube_static_mesh =
        {
            .vertex_count = vertex_count,
            .index_count = index_count,
            
            .positions = positions,
            .normals = normals,
            .uvs = uvs,
            .tangents = tangents,
            .indices = indicies,

            .sub_meshes = sub_meshes,
            .allocation_group = &append(&renderer_state->allocation_groups, allocation_group)
        };

        renderer_state->default_static_mesh = renderer_create_static_mesh(cube_static_mesh);
    }

    {
        renderer_state->default_scene = renderer_create_scene(1, 1);
        Scene *scene = renderer_get_scene(renderer_state->default_scene);
        Scene_Node *root = &scene->nodes[0];
        root->name = HE_STRING_LITERAL("Empty");
        root->parent = nullptr;
        root->first_child = nullptr;
        root->last_child = nullptr;
        root->next_sibling = nullptr;
        root->prev_sibling = nullptr;
        root->local_transform = get_identity_transform();
        root->global_transform = get_identity_transform();
        root->static_mesh_uuid = HE_MAX_U64; // todo(amer): should we use handles here...
    }

    init(&renderer_state->render_graph);

    {
        auto render = [](Renderer *renderer, Renderer_State *renderer_state)
        {
            renderer_use_material(renderer_state->skybox_material_handle);

            Static_Mesh_Handle static_mesh_handle = renderer_state->default_static_mesh;
            Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

            Buffer_Handle vertex_buffers[] =
            {
                static_mesh->positions_buffer,
            };
            U64 offsets[] = { 0 };

            renderer->set_vertex_buffers(to_array_view(vertex_buffers), to_array_view(offsets));
            renderer->set_index_buffer(static_mesh->indices_buffer, 0);
            renderer->draw_sub_mesh(static_mesh_handle, 0, 0);

            auto comp = [](const Render_Packet &a, const Render_Packet &b) -> bool
            {
                Material *a_mat = renderer_get_material(a.material);
                Material *b_mat = renderer_get_material(b.material);

                if (a.material.index != b.material.index)
                {
                    if (a_mat->pipeline_state_handle.index != b_mat->pipeline_state_handle.index)
                    {
                        return a_mat->pipeline_state_handle.index < b_mat->pipeline_state_handle.index;
                    }

                    return a.material.index < b.material.index;
                }

                if (a.static_mesh.index != b.static_mesh.index)
                {
                    return a.static_mesh.index < b.static_mesh.index;
                }

                return a.sub_mesh_index < b.sub_mesh_index;
            };

            // draw opaque objects
            std::sort(renderer_state->opaque_packets, renderer_state->opaque_packets + renderer_state->opaque_packet_count, comp);

            Material_Handle current_material_handle = Resource_Pool<Material>::invalid_handle;
            Static_Mesh_Handle current_static_mesh_handle = Resource_Pool<Static_Mesh>::invalid_handle;

            for (U32 packet_index = 0; packet_index < renderer_state->opaque_packet_count; packet_index++)
            {
                Render_Packet *packet = &renderer_state->opaque_packets[packet_index];

                if (current_material_handle != packet->material)
                {
                    renderer_use_material(packet->material);
                    current_material_handle = packet->material;
                }

                if (current_static_mesh_handle != packet->static_mesh)
                {
                    Static_Mesh *static_mesh = renderer_get_static_mesh(packet->static_mesh);

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

                    current_static_mesh_handle = packet->static_mesh;
                }

                renderer->draw_sub_mesh(packet->static_mesh, packet->transform_index, packet->sub_mesh_index);
            }
        };

        Render_Target_Info render_targets[] =
        {
            {
                .name = "multisample_main",
                .operation = Attachment_Operation::CLEAR,
                .info =
                {
                    .format = Texture_Format::B8G8R8A8_SRGB,
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
        Read_Entire_File_Result result = read_entire_file("shaders/default.glsl", scratch_memory.arena);
        String default_shader_source = { .data = (const char *)result.data, .count = result.size };
        
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
            .render_pass = get_render_pass(&renderer_state->render_graph, "opaque"), // todo(amer): we should not depend on the render graph here...
        };

        renderer_state->default_pipeline = renderer_create_pipeline_state(default_pipeline_state_descriptor);
        HE_ASSERT(is_valid_handle(&renderer_state->pipeline_states, renderer_state->default_pipeline));

        Material_Descriptor default_material_descriptor =
        {
            .pipeline_state_handle = renderer_state->default_pipeline,
        };

        renderer_state->default_material = renderer_create_material(default_material_descriptor);
        HE_ASSERT(is_valid_handle(&renderer_state->materials, renderer_state->default_material));
        
        set_property(renderer_state->default_material, "debug_texture_index", { .u32 = (U32)renderer_state->white_pixel_texture.index });
        set_property(renderer_state->default_material, "debug_color", { .v3 = { 1.0f, 0.0f, 1.0f }});

        Shader *default_shader = get(&renderer_state->shaders, renderer_state->default_shader);

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

        Shader_Struct *globals_struct = renderer_find_shader_struct(renderer_state->default_shader, HE_STRING_LITERAL("Globals"));
        HE_ASSERT(globals_struct);

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
                .size = sizeof(Object_Data) * HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT,
                .usage = Buffer_Usage::STORAGE_CPU_SIDE,
            };
            renderer_state->object_data_storage_buffers[frame_index] = renderer_create_buffer(object_data_storage_buffer_descriptor);

            renderer_state->per_frame_bind_groups[frame_index] = renderer_create_bind_group(per_frame_bind_group_descriptor);
            renderer_state->per_render_pass_bind_groups[frame_index] = renderer_create_bind_group(per_render_pass_bind_group_descriptor);

            Update_Binding_Descriptor globals_uniform_buffer_binding =
            {
                .binding_number = 0,
                .element_index = 0,
                .count = 1,
                .buffers = &renderer_state->globals_uniform_buffers[frame_index]
            };

            Update_Binding_Descriptor object_data_storage_buffer_binding =
            {
                .binding_number = 1,
                .element_index = 0,
                .count = 1,
                .buffers = &renderer_state->object_data_storage_buffers[frame_index]
            };

            Update_Binding_Descriptor update_binding_descriptors[] =
            {
                globals_uniform_buffer_binding,
                object_data_storage_buffer_binding
            };
            renderer_update_bind_group(renderer_state->per_frame_bind_groups[frame_index], to_array_view(update_binding_descriptors));
        }
    }


    // skybox
    {
        Allocation_Group allocation_group =
        {
            .resource_name = HE_STRING_LITERAL("skybox"),
            .semaphore = renderer_create_semaphore(semaphore_descriptor),
        };

        String paths[] =
        {
            HE_STRING_LITERAL("textures/skybox/right.jpg"),
            HE_STRING_LITERAL("textures/skybox/left.jpg"),
            HE_STRING_LITERAL("textures/skybox/top.jpg"),
            HE_STRING_LITERAL("textures/skybox/bottom.jpg"),
            HE_STRING_LITERAL("textures/skybox/front.jpg"),
            HE_STRING_LITERAL("textures/skybox/back.jpg"),
        };

        void *datas[6] = {};

        U32 width = 1;
        U32 height = 1;

        for (U32 i = 0; i < HE_ARRAYCOUNT(paths); i++)
        {
            const String &path = paths[i];

            S32 texture_width;
            S32 texture_height;
            S32 texture_channels;

            stbi_uc *pixels = stbi_load(path.data, &texture_width, &texture_height, &texture_channels, STBI_rgb_alpha);
            HE_ASSERT(pixels);

            width = texture_width;
            height = texture_height;

            U64 data_size = texture_width * texture_height * sizeof(U32);
            U32 *data = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, U32, data_size);
            memcpy(data, pixels, data_size);
            stbi_image_free(pixels);

            append(&allocation_group.allocations, (void *)data);
            datas[i] = data;
        }

        Texture_Descriptor cubmap_texture_descriptor =
        {
            .width = width,
            .height = height,
            .format = Texture_Format::R8G8B8A8_SRGB,
            .layer_count = HE_ARRAYCOUNT(paths),
            .data = to_array_view(datas),
            .mipmapping = true,
            .is_cubemap = true,
            .allocation_group = &append(&renderer_state->allocation_groups, allocation_group),
        };
        renderer_state->skybox = renderer_create_texture(cubmap_texture_descriptor);

        Read_Entire_File_Result result = read_entire_file("shaders/skybox.glsl", scratch_memory.arena);
        String skybox_shader_source = { .data = (const char *)result.data, .count = result.size };
        Shader_Compilation_Result skybox_compilation_result = renderer_compile_shader(skybox_shader_source, HE_STRING_LITERAL("shaders"));
        HE_ASSERT(skybox_compilation_result.success);
        
        Shader_Descriptor skybox_shader_descriptor =
        {
            .name = HE_STRING_LITERAL("skybox"),
            .compilation_result = &skybox_compilation_result
        };
        renderer_state->skybox_shader = renderer_create_shader(skybox_shader_descriptor);
        HE_ASSERT(is_valid_handle(&renderer_state->shaders, renderer_state->skybox_shader));

        Pipeline_State_Descriptor skybox_pipeline_state_descriptor =
        {
            .settings =
            {
                .cull_mode = Cull_Mode::NONE,
                .front_face = Front_Face::COUNTER_CLOCKWISE,
                .fill_mode = Fill_Mode::SOLID,
                .depth_testing = false,
                .sample_shading = true,
            },
            .shader = renderer_state->skybox_shader,
            .render_pass = get_render_pass(&renderer_state->render_graph, "opaque"),
        };
        renderer_state->skybox_pipeline = renderer_create_pipeline_state(skybox_pipeline_state_descriptor);

        Material_Descriptor skybox_material_descriptor =
        {
            .pipeline_state_handle = renderer_state->skybox_pipeline
        };

        renderer_state->skybox_material_handle = renderer_create_material(skybox_material_descriptor);
        set_property(renderer_state->skybox_material_handle, "skybox_texture_index", { .u32 = (U32)renderer_state->skybox.index });
        set_property(renderer_state->skybox_material_handle, "sky_color", { .v3 = { 1.0f, 1.0f, 1.0f } });
    }

    bool imgui_inited = init_imgui(engine);
    HE_ASSERT(imgui_inited);
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

glm::vec4 srgb_to_linear(const glm::vec4 &color)
{
    return glm::pow(color, glm::vec4(renderer_state->gamma));
}

glm::vec4 linear_to_srgb(const glm::vec4 &color)
{
    return glm::pow(color, glm::vec4(1.0f / renderer_state->gamma));
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
    Texture_Handle texture_handle = aquire_handle(&renderer_state->textures);
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_texture(texture_handle, descriptor);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
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

    texture->name = HE_STRING_LITERAL("");
    texture->width = 0;
    texture->height = 0;
    texture->format = Texture_Format::R8G8B8A8_SRGB;
    texture->sample_count = 1;
    texture->size = 0;
    texture->alignment = 0;
    texture->is_uploaded_to_gpu = false;
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
    Free_List_Allocator *allocator;
    String include_path;
};

shaderc_include_result *shaderc_include_resolve(void *user_data, const char *requested_source, int type, const char *requesting_source, size_t include_depth)
{
    Shaderc_UserData *ud = (Shaderc_UserData *)user_data;
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
    String source = HE_STRING(requested_source);
    String path = format_string(scratch_memory.arena, "%.*s/%.*s", HE_EXPAND_STRING(ud->include_path), HE_EXPAND_STRING(source));
    Read_Entire_File_Result file_result = read_entire_file(path.data, ud->allocator);
    HE_ASSERT(file_result.success);
    shaderc_include_result *result = HE_ALLOCATE(ud->allocator, shaderc_include_result);
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
    deallocate(ud->allocator, (void *)include_result->content);
    deallocate(ud->allocator, include_result);
}

Shader_Compilation_Result renderer_compile_shader(String source, String include_path)
{
    static shaderc_compiler_t compiler = shaderc_compiler_initialize();
    static shaderc_compile_options_t options = shaderc_compile_options_initialize();

    Free_List_Allocator *allocator = get_general_purpose_allocator();
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    Shaderc_UserData *shaderc_userdata = HE_ALLOCATE(allocator, Shaderc_UserData);
    shaderc_userdata->allocator = allocator;
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
        String blob = { data, size };
        compilation_result.stages[stage_index] = copy_string(blob, allocator);
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
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_bind_group(bind_group_handle, descriptor);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);

    Bind_Group *bind_group = &renderer_state->bind_groups.data[bind_group_handle.index];
    bind_group->descriptor = descriptor;
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
    renderer->destroy_bind_group(bind_group_handle);
    platform_lock_mutex(&renderer_state->render_commands_mutex);
    release_handle(&renderer_state->bind_groups, bind_group_handle);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
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

// todo(amer): gpu memory allocator
Static_Mesh_Handle renderer_create_static_mesh(const Static_Mesh_Descriptor &descriptor)
{
    Static_Mesh_Handle static_mesh_handle = aquire_handle(&renderer_state->static_meshes);
    Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

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

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    renderer->create_static_mesh(static_mesh_handle, descriptor);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);

    return static_mesh_handle;
}

Static_Mesh *renderer_get_static_mesh(Static_Mesh_Handle static_mesh_handle)
{
    return get(&renderer_state->static_meshes, static_mesh_handle);
}

void renderer_destroy_static_mesh(Static_Mesh_Handle &static_mesh_handle)
{
    Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

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
        append(&material->buffers, buffer_handle);
    }

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        Bind_Group_Descriptor bind_group_descriptor =
        {
            .shader = pipeline_state->descriptor.shader,
            .group_index = HE_PER_OBJECT_BIND_GROUP_INDEX
        };
        append(&material->bind_groups, renderer_create_bind_group(bind_group_descriptor));

        Update_Binding_Descriptor update_binding_descriptor =
        {
            .binding_number = 0,
            .element_index = 0,
            .count = 1,
            .buffers = &material->buffers[frame_index],
        };

        renderer_update_bind_group(material->bind_groups[frame_index], { .count = 1, .data = &update_binding_descriptor });
    }

    init(&material->properties, properties->member_count);

    for (U32 property_index = 0; property_index < properties->member_count; property_index++)
    {
        Shader_Struct_Member *member = &properties->members[property_index];

        Material_Property *property = &material->properties[property_index];
        property->name = member->name;
        property->data_type = member->data_type;
        property->offset_in_buffer = member->offset;

        property->is_texture_resource = ends_with(property->name, HE_STRING_LITERAL("_texture")) && member->data_type == Shader_Data_Type::U32;
        property->is_color = ends_with(property->name, HE_STRING_LITERAL("_color")) && (member->data_type == Shader_Data_Type::VECTOR3F || member->data_type == Shader_Data_Type::VECTOR4F);
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
    Pipeline_State *pipeline_state = get(&renderer_state->pipeline_states, material->pipeline_state_handle); 
    
    renderer_destroy_pipeline_state(material->pipeline_state_handle);

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        renderer_destroy_buffer(material->buffers[frame_index]);
        renderer_destroy_bind_group(material->bind_groups[frame_index]);
    }

    reset(&material->buffers);
    reset(&material->bind_groups);

    deallocate(get_general_purpose_allocator(), material->data);
    release_handle(&renderer_state->materials, material_handle);

    material_handle = Resource_Pool< Material >::invalid_handle;
}

S32 find_property(Material_Handle material_handle, const char *name)
{
    String name_ = HE_STRING(name);

    Material *material = get(&renderer_state->materials, material_handle);
    for (U32 property_index = 0; property_index < material->properties.count; property_index++)
    {
        Material_Property *property = &material->properties[property_index];
        if (property->name == name_)
        {
            return (S32)property_index;
        }
    }

    return -1;
}

bool set_property(Material_Handle material_handle, const char *name, Material_Property_Data data)
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

    if (property->is_texture_resource)
    {
        U32 *texture_index = (U32 *)&material->data[property->offset_in_buffer];

        if (data.u64 != HE_MAX_U64)
        {
            Resource_Ref ref = { data.u64 };
            Resource *resource = get_resource(ref);
            if (resource->state == Resource_State::LOADED)
            {
                *texture_index = resource->index;
            }
            else if (resource->state == Resource_State::UNLOADED)
            {
                aquire_resource(ref);
                *texture_index = (U32)renderer_state->white_pixel_texture.index;
            }
        }
        else
        {
            *texture_index = (U32)renderer_state->white_pixel_texture.index;
        }
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
            if (property->is_texture_resource)
            {
                U32 *texture_index = (U32 *)&material->data[property->offset_in_buffer];
                if (property->data.u64 != HE_MAX_U64)
                {
                    Resource_Ref ref = { property->data.u64 };
                    Resource *resource = get_resource(ref);
                    if (resource->state == Resource_State::PENDING)
                    {
                        material->dirty_count = HE_MAX_FRAMES_IN_FLIGHT;
                    }
                    else if (resource->state == Resource_State::LOADED)
                    {
                        *texture_index = resource->index;
                    }
                }
            }
        }

        Buffer *material_buffer = get(&renderer_state->buffers, material->buffers[renderer_state->current_frame_in_flight_index]);
        copy_memory(material_buffer->data, material->data, material->size);
    }

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

Scene_Handle renderer_create_scene(U32 node_capacity, U32 node_count)
{
    Scene_Handle scene_handle = aquire_handle(&renderer_state->scenes);
    Scene *scene = get(&renderer_state->scenes, scene_handle);
    Scene_Node *root = &scene->root;
    root->parent = nullptr;
    root->first_child = nullptr;
    root->last_child = nullptr;
    root->next_sibling = nullptr;
    root->prev_sibling = nullptr;
    root->static_mesh_uuid = HE_MAX_U64;
    root->local_transform = get_identity_transform();
    init(&scene->nodes, node_capacity, node_count);
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

void add_child(Scene_Node *parent, Scene_Node *node)
{
    HE_ASSERT(parent);
    HE_ASSERT(node);

    node->parent = parent;

    if (parent->last_child)
    {
        node->prev_sibling = parent->last_child;
        parent->last_child->next_sibling = node;
        parent->last_child = node;
    }
    else
    {
        parent->first_child = parent->last_child = node;
    }
}

void remove_child(Scene_Node *parent, Scene_Node *node)
{
    HE_ASSERT(parent);
    HE_ASSERT(node);
    HE_ASSERT(node->parent == parent);
    
    node->parent = nullptr;
    
    if (node->prev_sibling)
    {
        node->prev_sibling->next_sibling = node->next_sibling;
    }
    else
    {
        parent->first_child = node->next_sibling;
    }

    if (node->next_sibling)
    {
        node->next_sibling->prev_sibling = node->prev_sibling;
    }
    else
    {
        parent->last_child = node->prev_sibling;
    }
}

void renderer_parse_scene_tree(Scene_Node *scene_node, const Transform &parent_transform)
{
    Transform transform = combine(parent_transform, scene_node->local_transform);
    scene_node->global_transform = transform;

    Render_Pass_Handle opaque_pass = get_render_pass(&renderer_state->render_graph, "opaque");

    if (scene_node->static_mesh_uuid != HE_MAX_U64)
    {
        Resource_Ref static_mesh_ref = { scene_node->static_mesh_uuid };
        Static_Mesh_Handle static_mesh_handle = get_resource_handle_as<Static_Mesh>(static_mesh_ref);

        HE_ASSERT(renderer_state->object_data_count < HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT);
        U32 object_data_index = renderer_state->object_data_count++;
        Object_Data *object_data = &renderer_state->object_data_base[object_data_index];
        object_data->model = get_world_matrix(transform);

        Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

        Dynamic_Array< Sub_Mesh > &sub_meshes = static_mesh->sub_meshes;
        for (U32 sub_mesh_index = 0; sub_mesh_index < sub_meshes.count; sub_mesh_index++)
        {
            Sub_Mesh *sub_mesh = &sub_meshes[sub_mesh_index];

            Material_Handle material_handle = Resource_Pool<Material>::invalid_handle;

            if (sub_mesh->material_uuid != HE_MAX_U64)
            {
                Resource_Ref material_ref = { sub_mesh->material_uuid };
                material_handle = get_resource_handle_as<Material>(material_ref);
            }
            else
            {
                material_handle = renderer_state->default_material;
            }

            Material *material = renderer_get_material(material_handle);
            Pipeline_State *pipeline_state = renderer_get_pipeline_state(material->pipeline_state_handle);

            if (pipeline_state->descriptor.render_pass == opaque_pass)
            {
                Render_Packet *render_packet = &renderer_state->opaque_packets[renderer_state->opaque_packet_count++];
                render_packet->static_mesh = static_mesh_handle;
                render_packet->sub_mesh_index = sub_mesh_index;
                render_packet->transform_index = object_data_index;
                render_packet->material = material_handle;
            }
        }
        
    }

    for (Scene_Node *node = scene_node->first_child; node; node = node->next_sibling)
    {
        renderer_parse_scene_tree(node, transform);
    }
}

//
// Render Context
//

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
    //ImGui::StyleColorsClassic();

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