#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"

#include "core/platform.h"
#include "core/cvars.h"
#include "core/memory.h"
#include "core/engine.h"
#include "core/file_system.h"
#include "core/job_system.h"
#include "core/debugging.h"

#include "containers/queue.h"

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

static Free_List_Allocator *_transfer_allocator;
static Free_List_Allocator *_stbi_allocator;
#define STBI_MALLOC(sz) allocate(_stbi_allocator, sz, 0);
#define STBI_REALLOC(p, newsz) reallocate(_stbi_allocator, p, newsz, 0)
#define STBI_FREE(p) deallocate(_stbi_allocator, p)

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

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
            renderer->draw_static_mesh = &vulkan_renderer_draw_static_mesh;
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
    Memory_Arena *arena = &engine->memory.permanent_arena;
    renderer_state = HE_ALLOCATE(arena, Renderer_State);
    renderer_state->engine = engine;
    renderer_state->arena = create_sub_arena(arena, HE_MEGA(32));

    bool renderer_requested = request_renderer(RenderingAPI_Vulkan, &renderer_state->renderer);
    if (!renderer_requested)
    {
        HE_LOG(Rendering, Fetal, "failed to request vulkan renderer\n");
        return false;
    }

    renderer = &renderer_state->renderer;

    init(&renderer_state->buffers, arena, HE_MAX_BUFFER_COUNT);
    init(&renderer_state->textures, arena, HE_MAX_TEXTURE_COUNT);
    init(&renderer_state->samplers, arena, HE_MAX_SAMPLER_COUNT);
    init(&renderer_state->shaders, arena, HE_MAX_SHADER_COUNT);
    init(&renderer_state->shader_groups, arena, HE_MAX_SHADER_GROUP_COUNT);
    init(&renderer_state->pipeline_states, arena, HE_MAX_PIPELINE_STATE_COUNT);
    init(&renderer_state->bind_group_layouts, arena, HE_MAX_BIND_GROUP_LAYOUT_COUNT);
    init(&renderer_state->bind_groups, arena, HE_MAX_BIND_GROUP_COUNT);
    init(&renderer_state->render_passes, arena, HE_MAX_RENDER_PASS_COUNT);
    init(&renderer_state->frame_buffers, arena, HE_MAX_FRAME_BUFFER_COUNT);
    init(&renderer_state->materials,  arena, HE_MAX_MATERIAL_COUNT);
    init(&renderer_state->static_meshes, arena, HE_MAX_STATIC_MESH_COUNT);
    init(&renderer_state->semaphores, arena, HE_MAX_SEMAPHORE_COUNT);

    renderer_state->scene_nodes = HE_ALLOCATE_ARRAY(arena, Scene_Node, HE_MAX_SCENE_NODE_COUNT);
    renderer_state->root_scene_node = &renderer_state->scene_nodes[renderer_state->scene_node_count++];
    
    Scene_Node *root_scene_node = renderer_state->root_scene_node;
    root_scene_node->name = HE_STRING_LITERAL("Root");
    root_scene_node->transform = get_identity_transform();
    root_scene_node->parent = nullptr;
    root_scene_node->last_child = nullptr;
    root_scene_node->first_child = nullptr;
    root_scene_node->next_sibling = nullptr;
    root_scene_node->start_mesh_index = -1;
    root_scene_node->static_mesh_count = 0;

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
        .size = HE_GIGA(2),
        .usage = Buffer_Usage::TRANSFER,
        .is_device_local = false
    };
    renderer_state->transfer_buffer = renderer_create_buffer(transfer_buffer_descriptor);

    Buffer *transfer_buffer = get(&renderer_state->buffers, renderer_state->transfer_buffer);
    init_free_list_allocator(&renderer_state->transfer_allocator, transfer_buffer->data, transfer_buffer->size);

    _transfer_allocator = &renderer_state->transfer_allocator;
    _stbi_allocator = &engine->memory.free_list_allocator;

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
            .type = Allocation_Group_Type::GENERAL,
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
        U32* normal_pixel_data = HE_ALLOCATE(&renderer_state->transfer_allocator, U32);
        *normal_pixel_data = 0xFFFF8080; // todo(amer): endianness
        HE_ASSERT(HE_ARCH_X64);

        void* normal_pixel_datas[] = { normal_pixel_data };

        Allocation_Group allocation_group =
        {
            .resource_name = HE_STRING_LITERAL("normal pixel"),
            .type = Allocation_Group_Type::GENERAL,
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

    Sampler_Descriptor default_sampler_descriptor =
    {
        .address_mode_u = Address_Mode::REPEAT,
        .address_mode_v = Address_Mode::REPEAT,
        .address_mode_w = Address_Mode::REPEAT,
        .min_filter = Filter::LINEAR,
        .mag_filter = Filter::NEAREST,
        .mip_filter = Filter::LINEAR,
        .anisotropy = get_anisotropic_filtering_value(renderer_state->anisotropic_filtering_setting)
    };
    renderer_state->default_sampler = renderer_create_sampler(default_sampler_descriptor);

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        Buffer_Descriptor globals_uniform_buffer_descriptor =
        {
            .size = sizeof(Globals),
            .usage = Buffer_Usage::UNIFORM,
            .is_device_local = false,
        };
        renderer_state->globals_uniform_buffers[frame_index] = renderer_create_buffer(globals_uniform_buffer_descriptor);

        Buffer_Descriptor object_data_storage_buffer_descriptor =
        {
            .size = sizeof(Object_Data) * HE_MAX_OBJECT_DATA_COUNT,
            .usage = Buffer_Usage::STORAGE,
            .is_device_local = false
        };
        renderer_state->object_data_storage_buffers[frame_index] = renderer_create_buffer(object_data_storage_buffer_descriptor);
    }

    U32 max_vertex_count = 1'000'000; // todo(amer): @Hardcode
    renderer_state->max_vertex_count = max_vertex_count;

    Buffer_Descriptor position_buffer_descriptor =
    {
        .size = max_vertex_count * sizeof(glm::vec3),
        .usage = Buffer_Usage::VERTEX,
        .is_device_local = true
    };
    renderer_state->position_buffer = renderer_create_buffer(position_buffer_descriptor);

    Buffer_Descriptor normal_buffer_descriptor =
    {
        .size = max_vertex_count * sizeof(glm::vec3),
        .usage = Buffer_Usage::VERTEX,
        .is_device_local = true
    };
    renderer_state->normal_buffer = renderer_create_buffer(normal_buffer_descriptor);

    Buffer_Descriptor uv_buffer_descriptor =
    {
        .size = max_vertex_count * sizeof(glm::vec2),
        .usage = Buffer_Usage::VERTEX,
        .is_device_local = true
    };
    renderer_state->uv_buffer = renderer_create_buffer(uv_buffer_descriptor);

    Buffer_Descriptor tangent_buffer_descriptor =
    {
        .size = max_vertex_count * sizeof(glm::vec4),
        .usage = Buffer_Usage::VERTEX,
        .is_device_local = true
    };
    renderer_state->tangent_buffer = renderer_create_buffer(tangent_buffer_descriptor);

    Buffer_Descriptor index_buffer_descriptor =
    {
        .size = HE_MEGA(128),
        .usage = Buffer_Usage::INDEX,
        .is_device_local = true
    };
    renderer_state->index_buffer = renderer_create_buffer(index_buffer_descriptor);

    init(&renderer_state->render_graph, &engine->memory.free_list_allocator);

    {
        auto render = [](Renderer *renderer, Renderer_State *renderer_state)
        {
            Buffer_Handle vertex_buffers[] =
            {
                renderer_state->position_buffer,
            };

            U64 offsets[] =
            {
                0,
            };

            renderer->set_vertex_buffers(to_array_view(vertex_buffers), to_array_view(offsets));
            renderer->set_index_buffer(renderer_state->index_buffer, 0);

            Update_Binding_Descriptor globals_uniform_buffer_bindings[] =
            {
                {
                    .binding_number = 0,
                    .element_index = 0,
                    .count = 1,
                    .buffers = &renderer_state->globals_uniform_buffers[renderer_state->current_frame_in_flight_index]
                }
            };
            renderer->update_bind_group(renderer_state->skybox_bind_groups[0], to_array_view(globals_uniform_buffer_bindings));
            
            Bind_Group_Handle bind_groups[] =
            {
                renderer_state->skybox_bind_groups[0],
                renderer_state->skybox_bind_groups[1]
            };
            renderer->set_pipeline_state(renderer_state->skybox_pipeline);
            renderer->set_bind_groups(0, to_array_view(bind_groups));

            U32 mesh_index = renderer_state->cube_mesh->first_child->start_mesh_index;
            Static_Mesh_Handle static_mesh_handle = { (S32)mesh_index, renderer_state->static_meshes.generations[mesh_index] };
            renderer->draw_static_mesh(static_mesh_handle, 0);
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
            }
        };

        Render_Graph_Node &node = add_node(&renderer_state->render_graph, "skybox", to_array_view(render_targets), render);
        node.clear_values[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
    }

    {
        auto render = [](Renderer *renderer, Renderer_State *renderer_state)
        {
            Buffer_Handle vertex_buffers[] =
            {
                renderer_state->position_buffer,
                renderer_state->normal_buffer,
                renderer_state->uv_buffer,
                renderer_state->tangent_buffer
            };
            
            U64 offsets[] =
            {
                0,
                0,
                0,
                0
            };

            renderer->set_vertex_buffers(to_array_view(vertex_buffers), to_array_view(offsets));
            renderer->set_index_buffer(renderer_state->index_buffer, 0);

            U32 texture_count = renderer_state->textures.capacity;
            Texture_Handle *textures = HE_ALLOCATE_ARRAY(&renderer_state->frame_arena, Texture_Handle, texture_count);
            Sampler_Handle *samplers = HE_ALLOCATE_ARRAY(&renderer_state->frame_arena, Sampler_Handle, texture_count);

            for (auto it = iterator(&renderer_state->textures); next(&renderer_state->textures, it);)
            {
                if (renderer_state->textures.data[it.index].is_attachment)
                {
                    textures[it.index] = renderer_state->white_pixel_texture;
                }
                else
                {
                    textures[it.index] = it;
                }

                samplers[it.index] = renderer_state->default_sampler;
            }

            Update_Binding_Descriptor update_textures_binding_descriptors[] =
            {
                {
                    .binding_number = 0,
                    .element_index = 0,
                    .count = texture_count,
                    .textures = textures,
                    .samplers = samplers
                }
            };

            renderer->update_bind_group(renderer_state->per_render_pass_bind_groups[renderer_state->current_frame_in_flight_index], to_array_view(update_textures_binding_descriptors));

            Bind_Group_Handle bind_groups[] =
            {
                renderer_state->per_frame_bind_groups[renderer_state->current_frame_in_flight_index],
                renderer_state->per_render_pass_bind_groups[renderer_state->current_frame_in_flight_index]
            };

            renderer->set_bind_groups(0, to_array_view(bind_groups));
            render_scene_node(renderer_state->root_scene_node);
        };

        Render_Target_Info render_targets[] =
        {
            {
                .name = "multisample_main",
                .operation = Attachment_Operation::LOAD,
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

    {
        Shader_Descriptor opaquePBR_vertex_shader_descriptor =
        {
            .path = "shaders/bin/opaquePBR.vert.spv"
        };
        renderer_state->opaquePBR_vertex_shader = renderer_create_shader(opaquePBR_vertex_shader_descriptor);

        Shader_Descriptor opaquePBR_fragment_shader_descriptor =
        {
            .path = "shaders/bin/opaquePBR.frag.spv"
        };
        renderer_state->opaquePBR_fragment_shader = renderer_create_shader(opaquePBR_fragment_shader_descriptor);

        Shader_Group_Descriptor opaquePBR_shader_group_descriptor;
        opaquePBR_shader_group_descriptor.shaders =
        {
            renderer_state->opaquePBR_vertex_shader,
            renderer_state->opaquePBR_fragment_shader
        };
        renderer_state->opaquePBR_shader_group = renderer_create_shader_group(opaquePBR_shader_group_descriptor);

        Shader_Group *opaquePBR_shader_group = get(&renderer_state->shader_groups, renderer_state->opaquePBR_shader_group);

        Bind_Group_Descriptor per_frame_bind_group_descriptor =
        {
            .shader_group = renderer_state->opaquePBR_shader_group,
            .layout = opaquePBR_shader_group->bind_group_layouts[0]
        };

        Bind_Group_Descriptor per_render_pass_bind_group_descriptor =
        {
            .shader_group = renderer_state->opaquePBR_shader_group,
            .layout = opaquePBR_shader_group->bind_group_layouts[1]
        };

        for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
        {
            renderer_state->per_frame_bind_groups[frame_index] = renderer_create_bind_group(per_frame_bind_group_descriptor);

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

            renderer->update_bind_group(renderer_state->per_frame_bind_groups[frame_index], to_array_view(update_binding_descriptors));
            renderer_state->per_render_pass_bind_groups[frame_index] = renderer_create_bind_group(per_render_pass_bind_group_descriptor);
        }

        Pipeline_State_Descriptor opaquePBR_pipeline_state_descriptor =
        {
            .cull_mode = Cull_Mode::BACK,
            .front_face = Front_Face::COUNTER_CLOCKWISE,
            .fill_mode = Fill_Mode::SOLID,
            .sample_shading = true,
            .shader_group = renderer_state->opaquePBR_shader_group,
            .render_pass = get_render_pass(&renderer_state->render_graph, "opaque"),
        };
        renderer_state->opaquePBR_pipeline = renderer_create_pipeline_state(opaquePBR_pipeline_state_descriptor);
    }

    {
        Allocation_Group allocation_group =
        {
            .resource_name = HE_STRING_LITERAL("skybox"),
            .type = Allocation_Group_Type::GENERAL,
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
        
        void* datas[6] = {};

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
            
            append(&allocation_group.allocations, (void*)data);
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

        Sampler_Descriptor skybox_sampler_descriptor =
        {
            .address_mode_u = Address_Mode::CLAMP,
            .address_mode_v = Address_Mode::CLAMP,
            .address_mode_w = Address_Mode::CLAMP,

            .min_filter = Filter::LINEAR,
            .mag_filter = Filter::LINEAR,
            .mip_filter = Filter::LINEAR,

            .anisotropy = 1
        };
        renderer_state->skybox_sampler = renderer_create_sampler(skybox_sampler_descriptor);

        Shader_Descriptor skybox_vertex_shader_descriptor =
        {
            .path = "shaders/bin/skybox.vert.spv"
        };
        renderer_state->skybox_vertex_shader = renderer_create_shader(skybox_vertex_shader_descriptor);

        Shader_Descriptor skybox_fragment_shader_descriptor =
        {
            .path = "shaders/bin/skybox.frag.spv"
        };
        renderer_state->skybox_fragment_shader = renderer_create_shader(skybox_fragment_shader_descriptor);
        
        Shader_Group_Descriptor skybox_shader_descriptor = {};
        skybox_shader_descriptor.shaders =
        {
            renderer_state->skybox_vertex_shader,
            renderer_state->skybox_fragment_shader
        };
        renderer_state->skybox_shader_group = renderer_create_shader_group(skybox_shader_descriptor);
        
        Pipeline_State_Descriptor skybox_pipeline_state_descriptor =
        {
            .cull_mode = Cull_Mode::NONE,
            .front_face = Front_Face::COUNTER_CLOCKWISE,
            .fill_mode = Fill_Mode::SOLID,
            .sample_shading = true,
            .shader_group = renderer_state->skybox_shader_group,
            .render_pass = get_render_pass(&renderer_state->render_graph, "skybox"),
        };
        renderer_state->skybox_pipeline = renderer_create_pipeline_state(skybox_pipeline_state_descriptor);

        Shader_Group *skybox_shader_group = get(&renderer_state->shader_groups, renderer_state->skybox_shader_group);

        {
            Bind_Group_Descriptor skybox_bind_group_descriptor =
            {
                .shader_group = renderer_state->skybox_shader_group,
                .layout = skybox_shader_group->bind_group_layouts[0]
            };
            renderer_state->skybox_bind_groups[0] = renderer_create_bind_group(skybox_bind_group_descriptor);    
        }

        {
            Bind_Group_Descriptor skybox_bind_group_descriptor =
            {
                .shader_group = renderer_state->skybox_shader_group,
                .layout = skybox_shader_group->bind_group_layouts[1]
            };
            renderer_state->skybox_bind_groups[1] = renderer_create_bind_group(skybox_bind_group_descriptor);    
        }

        Update_Binding_Descriptor update_skybox_binding_descriptors[] =
        {
            {
                .binding_number = 0,
                .element_index = 0,
                .count = 1,
                .textures = &renderer_state->skybox,
                .samplers = &renderer_state->skybox_sampler
            }
        };
        renderer->update_bind_group(renderer_state->skybox_bind_groups[1], to_array_view(update_skybox_binding_descriptors));    
    }

    renderer_state->cube_mesh = load_model_threaded(HE_STRING_LITERAL("models/Cube/Cube.gltf"), false);

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

    for (auto it = iterator(&renderer_state->static_meshes); next(&renderer_state->static_meshes, it);)
    {
        renderer->destroy_static_mesh(it);
    }

    for (auto it = iterator(&renderer_state->shaders); next(&renderer_state->shaders, it);)
    {
        renderer->destroy_shader(it);
    }

    for (auto it = iterator(&renderer_state->shader_groups); next(&renderer_state->shader_groups, it);)
    {
        renderer->destroy_shader_group(it);
    }

    for (auto it = iterator(&renderer_state->bind_group_layouts); next(&renderer_state->bind_group_layouts, it);)
    {
        renderer->destroy_bind_group_layout(it);
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

Scene_Node* add_child_scene_node(Scene_Node *parent)
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

Texture_Handle find_texture(const String &name)
{
    for (auto it = iterator(&renderer_state->textures); next(&renderer_state->textures, it);)
    {
        Texture *texture = &renderer_state->textures.data[it.index];
        if (texture->name == name)
        {
            return it;
        }
    }

    return Resource_Pool< Texture >::invalid_handle;
}

Material_Handle find_material(U64 hash)
{
    for (auto it = iterator(&renderer_state->materials); next(&renderer_state->materials, it);)
    {
        Material *material = &renderer_state->materials.data[it.index];
        if (material->hash == hash)
        {
            return it;
        }
    }

    return Resource_Pool< Material >::invalid_handle;
}

static bool internal_create_texture(Texture_Handle texture_handle, void *pixels, U32 texture_width, U32 texture_height, Allocation_Group *allocation_group)
{
    U64 data_size = texture_width * texture_height * sizeof(U32);
    U32 *data = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, U32, data_size);
    memcpy(data, pixels, data_size);
    
    append(&allocation_group->allocations, (void*)data);
    void* datas[] = { (void*)data };

    Texture_Descriptor descriptor =
    {
        .width = texture_width,
        .height = texture_height,
        .format = Texture_Format::R8G8B8A8_SRGB,
        .data = to_array_view(datas),
        .mipmapping = true,
        .sample_count = 1,
        .allocation_group = allocation_group,
    };

    platform_lock_mutex(&renderer_state->render_commands_mutex);
    bool texture_created = renderer->create_texture(texture_handle, descriptor);
    HE_ASSERT(texture_created);
    platform_unlock_mutex(&renderer_state->render_commands_mutex);
    return true;
}

struct Load_Texture_Job_Data
{
    String path;
    Texture_Handle texture_handle;
    Allocation_Group allocation_group;
};

static Job_Result load_texture_job(const Job_Parameters &params)
{
    Load_Texture_Job_Data *job_data = (Load_Texture_Job_Data *)params.data;
    const String &path = job_data->path;

    HE_LOG(Rendering, Trace, "loading texture: %.*s\n", HE_EXPAND_STRING(path));

    S32 texture_width;
    S32 texture_height;
    S32 texture_channels;

    stbi_uc *pixels = stbi_load(path.data, &texture_width, &texture_height, &texture_channels, STBI_rgb_alpha);
    HE_ASSERT(pixels);
    
    bool texture_created = internal_create_texture(job_data->texture_handle, pixels, texture_width, texture_height, &job_data->allocation_group);
    HE_ASSERT(texture_created);
    stbi_image_free(pixels);

    platform_lock_mutex(&renderer_state->allocation_groups_mutex);
    append(&renderer_state->allocation_groups, job_data->allocation_group);
    platform_unlock_mutex(&renderer_state->allocation_groups_mutex);

    return Job_Result::SUCCEEDED;
}

static Texture_Handle cgltf_load_texture(cgltf_texture_view *texture_view, const String &model_path, Memory_Arena *arena, Allocation_Group *model_allocation_group)
{
    Temprary_Memory_Arena temprary_arena;
    begin_temprary_memory_arena(&temprary_arena, arena);
    HE_DEFER { end_temprary_memory_arena(&temprary_arena); };

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

        if (extension != ".png" && extension != ".jpg")
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

        texture_path = format_string(temprary_arena.arena, "%.*s/%.*s%s", HE_EXPAND_STRING(model_path), HE_EXPAND_STRING(texture_name), HE_EXPAND_STRING(extension_to_append));
    }

    Texture_Handle found_texture_handle = find_texture(texture_path);
    if (!is_valid_handle(&renderer_state->textures, found_texture_handle))
    {
        texture_handle = aquire_handle(&renderer_state->textures);
        Texture *texture = get(&renderer_state->textures, texture_handle);
        texture->name = copy_string(texture_path.data, texture_path.count, &renderer_state->engine->memory.free_list_allocator);

        if (file_exists(texture_path))
        {
            Renderer_Semaphore_Descriptor semaphore_descriptor =
            {
                .initial_value = 0
            };

            Load_Texture_Job_Data load_texture_job_data =
            {
                .path = texture->name,
                .texture_handle = texture_handle,
                .allocation_group =
                {
                    .resource_name = texture->name,
                    .type = Allocation_Group_Type::GENERAL,
                    .semaphore = renderer_create_semaphore(semaphore_descriptor),
                }
            };

            Job job = {};
            job.parameters.data = &load_texture_job_data;
            job.parameters.size = sizeof(load_texture_job_data);
            job.proc = load_texture_job;
            execute_job(job);
        }
        else
        {
            // todo(amer): untested code path...
            const auto *view = image->buffer_view;
            U8 *data_ptr = (U8*)view->buffer->data;
            U8 *image_data = data_ptr + view->offset;

            S32 texture_width;
            S32 texture_height;
            S32 texture_channels;

            stbi_uc *pixels = nullptr;
            pixels = stbi_load_from_memory(image_data, u64_to_u32(view->size), &texture_width, &texture_height, &texture_channels, STBI_rgb_alpha);
            HE_ASSERT(pixels);

            bool texture_created = internal_create_texture(texture_handle, pixels, texture_width, texture_height, model_allocation_group);
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
    Scene_Node *scene_node;
    Allocation_Group allocation_group;
};

Job_Result load_model_job(const Job_Parameters &params)
{
    Temprary_Memory_Arena *tempray_memory_arena = params.temprary_memory_arena;
    Load_Model_Job_Data *data = (Load_Model_Job_Data *)params.data;
    HE_LOG(Rendering, Trace, "loading model: %.*s\n", HE_EXPAND_STRING(data->path));
    bool model_loaded = load_model(data->scene_node, data->path, tempray_memory_arena->arena, &data->allocation_group);
    if (!model_loaded)
    {
        return Job_Result::FAILED;
    }
    platform_lock_mutex(&renderer_state->allocation_groups_mutex);
    append(&renderer_state->allocation_groups, data->allocation_group);
    platform_unlock_mutex(&renderer_state->allocation_groups_mutex);
    return Job_Result::SUCCEEDED;
}

Scene_Node* load_model_threaded(const String &path, bool add_to_scene)
{
    Scene_Node *scene_node = nullptr;

    if (add_to_scene)
    {
        scene_node = add_child_scene_node(renderer_state->root_scene_node);
    }
    else
    {
        scene_node = &renderer_state->scene_nodes[renderer_state->scene_node_count++];
        scene_node->parent = nullptr;
    }

    U32 index = 0;
    U32 count = (U32)path.count;

    S64 slash = find_first_char_from_right(path, "/\\");
    if (slash != -1)
    {
        index = (U32)slash;
    }

    S64 dot = find_first_char_from_right(path, ".");
    if (dot != -1)
    {
        count = (U32)(dot - index - 1);
    }

    scene_node->name = sub_string(path, index + 1, count);

    Renderer_Semaphore_Descriptor semaphore_descriptor =
    {
        .initial_value = 0
    };

    Load_Model_Job_Data data =
    {
        .path = path,
        .scene_node = scene_node,
        .allocation_group =
        {
            .resource_name = path,
            .type = Allocation_Group_Type::MODEL,
            .semaphore = renderer_create_semaphore(semaphore_descriptor),
        }
    };

    Job job = {};
    job.proc = load_model_job;
    job.parameters.data = &data;
    job.parameters.size = sizeof(Load_Model_Job_Data);
    execute_job(job);

    return scene_node;
}

// note(amer): https://github.com/deccer/CMake-Glfw-OpenGL-Template/blob/main/src/Project/ProjectApplication.cpp
// thanks to this giga chad for the example
bool load_model(Scene_Node *root_scene_node, const String &path, Memory_Arena *arena, Allocation_Group *allocation_group)
{
    Read_Entire_File_Result result = read_entire_file(path.data, &renderer_state->transfer_allocator);

    if (!result.success)
    {
        return false;
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
        return false;
    }

    if (cgltf_load_buffers(&options, model_data, path.data) != cgltf_result_success) // @Leak
    {
        return false;
    }

    for (U32 material_index = 0; material_index < model_data->materials_count; material_index++)
    {
        cgltf_material *material = &model_data->materials[material_index];
        U64 material_hash = (U64)material;

        HE_ASSERT(renderer_state->materials.count < HE_MAX_MATERIAL_COUNT);

        Material_Descriptor material_descriptor = {};
        material_descriptor.pipeline_state_handle = renderer_state->opaquePBR_pipeline;

        platform_lock_mutex(&renderer_state->render_commands_mutex);
        Material_Handle material_handle = renderer_create_material(material_descriptor);
        platform_unlock_mutex(&renderer_state->render_commands_mutex);

        Material *renderer_material = get(&renderer_state->materials, material_handle);

        if (material->name)
        {
            renderer_material->name = copy_string(material->name, string_length(material->name), &renderer_state->engine->memory.free_list_allocator);
        }
        
        renderer_material->hash = material_hash;

        Texture_Handle albedo = { -1 };
        Texture_Handle normal = { -1 };
        Texture_Handle metallic_roughness = { -1 };

        if (material->has_pbr_metallic_roughness)
        {
            if (material->pbr_metallic_roughness.base_color_texture.texture)
            {
                albedo = cgltf_load_texture(&material->pbr_metallic_roughness.base_color_texture, model_path, arena, allocation_group);
            }
            else
            {
                albedo = renderer_state->white_pixel_texture;
            }

            if (material->pbr_metallic_roughness.metallic_roughness_texture.texture)
            {
                metallic_roughness = cgltf_load_texture(&material->pbr_metallic_roughness.metallic_roughness_texture, model_path, arena, allocation_group);
            }
            else
            {
                metallic_roughness = renderer_state->white_pixel_texture;
            }
        }

        if (material->normal_texture.texture)
        {
            normal = cgltf_load_texture(&material->normal_texture, model_path, arena, allocation_group);
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

    root_scene_node->parent = nullptr;
    root_scene_node->transform = get_identity_transform();
    root_scene_node->start_mesh_index = -1;
    root_scene_node->static_mesh_count = 0;

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
        push(&nodes, { &model_data->nodes[node_index], add_child_scene_node(root_scene_node) });
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
        scene_node->start_mesh_index = -1;
        scene_node->static_mesh_count = 0;
        scene_node->transform = get_identity_transform();

        cgltf_node *node = node_bundle.cgltf_node;
        scene_node->transform.position = *(glm::vec3*)&node->translation;
        scene_node->transform.rotation = { node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2] };
        scene_node->transform.euler_angles = glm::degrees(glm::eulerAngles(scene_node->transform.rotation));
        scene_node->transform.scale = *(glm::vec3*)&node->scale;

        scene_node->name = copy_string(HE_STRING(node->name), &renderer_state->engine->memory.free_list_allocator);

        if (node->mesh)
        {
            scene_node->start_mesh_index = renderer_state->static_meshes.count;
            scene_node->static_mesh_count += u64_to_u32(node->mesh->primitives_count);

            for (U32 primitive_index = 0; primitive_index < node->mesh->primitives_count; primitive_index++)
            {
                cgltf_primitive *primitive = &node->mesh->primitives[primitive_index];
                HE_ASSERT(primitive->type == cgltf_primitive_type_triangles);

                HE_ASSERT(primitive->material);
                cgltf_material *material = primitive->material;

                U64 material_hash = (U64)material;
                Material_Handle material_handle = find_material(material_hash);

                Static_Mesh_Handle static_mesh_handle = aquire_handle(&renderer_state->static_meshes);
                Static_Mesh *static_mesh = get(&renderer_state->static_meshes, static_mesh_handle);
                static_mesh->material = material_handle;

                U32 position_count = 0;
                glm::vec3 *positions = nullptr;

                U32 normal_count = 0;
                glm::vec3 *normals = nullptr;

                U32 uv_count = 0;
                glm::vec2 *uvs = nullptr;

                U32 tangent_count = 0;
                glm::vec4 *tangents = nullptr;

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

                            U64 stride = attribute->data->stride;
                            HE_ASSERT(stride == sizeof(glm::vec3));

                            position_count = u64_to_u32(attribute->data->count);
                            positions = (glm::vec3 *)(data_ptr + view->offset + accessor->offset);
                        } break;

                        case cgltf_attribute_type_normal:
                        {
                            HE_ASSERT(attribute->data->type == cgltf_type_vec3);
                            HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                            U64 stride = attribute->data->stride;
                            HE_ASSERT(stride == sizeof(glm::vec3));

                            normal_count = u64_to_u32(attribute->data->count);
                            normals = (glm::vec3 *)(data_ptr + view->offset + accessor->offset);
                        } break;

                        case cgltf_attribute_type_texcoord:
                        {
                            HE_ASSERT(attribute->data->type == cgltf_type_vec2);
                            HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                            U64 stride = attribute->data->stride;
                            HE_ASSERT(stride == sizeof(glm::vec2));

                            uv_count = u64_to_u32(attribute->data->count);
                            uvs = (glm::vec2 *)(data_ptr + view->offset + accessor->offset);
                        } break;

                        case cgltf_attribute_type_tangent:
                        {
                            HE_ASSERT(attribute->data->type == cgltf_type_vec4);
                            HE_ASSERT(attribute->data->component_type == cgltf_component_type_r_32f);

                            U64 stride = attribute->data->stride;
                            HE_ASSERT(stride == sizeof(glm::vec4));

                            tangent_count = u64_to_u32(attribute->data->count);
                            tangents = (glm::vec4 *)(data_ptr + view->offset + accessor->offset);
                        } break;
                    }
                }

                // note(amer): we only support u16 indices for now.
                HE_ASSERT(primitive->indices->type == cgltf_type_scalar);
                HE_ASSERT(primitive->indices->component_type == cgltf_component_type_r_16u);
                HE_ASSERT(primitive->indices->stride == sizeof(U16));

                U32 index_count = u64_to_u32(primitive->indices->count);
                const auto *accessor = primitive->indices;
                const auto *view = accessor->buffer_view;
                U8 *data_ptr = (U8 *)view->buffer->data;
                U16 *indices = (U16 *)(data_ptr + view->offset + accessor->offset);

                HE_ASSERT(position_count == normal_count);
                HE_ASSERT(position_count == uv_count);
                // HE_ASSERT(position_count == tangent_count);

                Static_Mesh_Descriptor static_mesh_descriptor =
                {
                    .vertex_count = (U16)position_count,
                    .positions = positions,
                    .normals = normals,
                    .uvs = uvs,
                    .tangents = tangents,
                    .index_count = index_count,
                    .indices = indices,
                    .allocation_group = allocation_group
                };

                platform_lock_mutex(&renderer_state->render_commands_mutex);
                bool created = renderer->create_static_mesh(static_mesh_handle, static_mesh_descriptor);
                platform_unlock_mutex(&renderer_state->render_commands_mutex);
                HE_ASSERT(created);
            }
        }

        for (U32 child_node_index = 0; child_node_index < node->children_count; child_node_index++)
        {
            push(&nodes, { node->children[child_node_index], add_child_scene_node(scene_node) });
        }
    }

    return true;
}

void unload_model(Allocation_Group *allocation_group)
{
    cgltf_free((cgltf_data *)allocation_group->allocations[0]);
    deallocate(&renderer_state->transfer_allocator, allocation_group->allocations[1]);
}

void render_scene_node(Scene_Node *scene_node, const Transform &parent_transform)
{
    Transform transform = combine(parent_transform, scene_node->transform);

    for (U32 static_mesh_index = 0; static_mesh_index < scene_node->static_mesh_count; static_mesh_index++)
    {
        U32 mesh_index = scene_node->start_mesh_index + static_mesh_index;
        Static_Mesh_Handle static_mesh_handle = { (S32)mesh_index, renderer_state->static_meshes.generations[mesh_index] };

        HE_ASSERT(renderer_state->object_data_count < HE_MAX_OBJECT_DATA_COUNT);
        U32 object_data_index = renderer_state->object_data_count++;
        Object_Data *object_data = &renderer_state->object_data_base[object_data_index];
        object_data->model = get_world_matrix(transform);

        Static_Mesh *static_mesh = get(&renderer_state->static_meshes, static_mesh_handle);
        Material *material = get(&renderer_state->materials, static_mesh->material);

        Buffer *material_buffer = get(&renderer_state->buffers, material->buffers[renderer_state->current_frame_in_flight_index]);
        copy_memory(material_buffer->data, material->data, material->size);

        Pipeline_State *pipeline_state = get(&renderer_state->pipeline_states, material->pipeline_state_handle);

        Bind_Group_Handle material_bind_groups[] =
        {
            material->bind_groups[renderer_state->current_frame_in_flight_index]
        };

        renderer->set_pipeline_state(material->pipeline_state_handle);
        renderer->set_bind_groups(2, to_array_view(material_bind_groups));

        renderer->draw_static_mesh(static_mesh_handle, object_data_index);
    }

    for (Scene_Node *node = scene_node->first_child; node; node = node->next_sibling)
    {
        render_scene_node(node, transform);
    }
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
    renderer->create_buffer(buffer_handle, descriptor);

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
    renderer->create_texture(texture_handle, descriptor);
    return texture_handle;
}

Texture* renderer_get_texture(Texture_Handle texture_handle)
{
    return get(&renderer_state->textures, texture_handle);
}

void renderer_destroy_texture(Texture_Handle &texture_handle)
{
    renderer->destroy_texture(texture_handle);
    release_handle(&renderer_state->textures, texture_handle);
    texture_handle = Resource_Pool< Texture >::invalid_handle;
}

//
// Samplers
//

Sampler_Handle renderer_create_sampler(const Sampler_Descriptor &descriptor)
{
    Sampler_Handle sampler_handle = aquire_handle(&renderer_state->samplers);
    renderer->create_sampler(sampler_handle, descriptor);
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

Shader_Handle renderer_create_shader(const Shader_Descriptor &descriptor)
{
    Shader_Handle shader_handle = aquire_handle(&renderer_state->shaders);
    renderer->create_shader(shader_handle, descriptor);
    return shader_handle;
}

Shader* renderer_get_shader(Shader_Handle shader_handle)
{
    return get(&renderer_state->shaders, shader_handle);
}

void renderer_destroy_shader(Shader_Handle &shader_handle)
{
    renderer->destroy_shader(shader_handle);
    release_handle(&renderer_state->shaders, shader_handle);
    shader_handle = Resource_Pool< Shader >::invalid_handle;
}

//
// Shader Groups
//

Shader_Group_Handle renderer_create_shader_group(const Shader_Group_Descriptor &descriptor)
{
    Shader_Group_Handle shader_group_handle = aquire_handle(&renderer_state->shader_groups);
    renderer->create_shader_group(shader_group_handle, descriptor);
    Shader_Group *shader_group = &renderer_state->shader_groups.data[shader_group_handle.index];
    copy(&shader_group->shaders, &descriptor.shaders);
    return shader_group_handle;
}

Shader_Group* renderer_get_shader_group(Shader_Group_Handle shader_group_handle)
{
    return get(&renderer_state->shader_groups, shader_group_handle);
}

void renderer_destroy_shader_group(Shader_Group_Handle &shader_group_handle)
{
    renderer->destroy_shader_group(shader_group_handle);
    release_handle(&renderer_state->shader_groups, shader_group_handle);
    shader_group_handle = Resource_Pool< Shader_Group >::invalid_handle;
}

//
// Bind Group Layouts
//

Bind_Group_Layout_Handle renderer_create_bind_group_layout(const Bind_Group_Layout_Descriptor &descriptor)
{
    Bind_Group_Layout_Handle bind_group_layout_handle = aquire_handle(&renderer_state->bind_group_layouts);
    renderer->create_bind_group_layout(bind_group_layout_handle, descriptor);
    Bind_Group_Layout *bind_group_layout = &renderer_state->bind_group_layouts.data[bind_group_layout_handle.index]; 
    bind_group_layout->descriptor = descriptor;
    return bind_group_layout_handle;
}

Bind_Group_Layout* renderer_get_bind_group_layout(Bind_Group_Layout_Handle bind_group_layout_handle)
{
    return get(&renderer_state->bind_group_layouts, bind_group_layout_handle);
}

void renderer_destroy_bind_group_layout(Bind_Group_Layout_Handle &bind_group_layout_handle)
{
    renderer->destroy_bind_group_layout(bind_group_layout_handle);
    release_handle(&renderer_state->bind_group_layouts, bind_group_layout_handle);
    bind_group_layout_handle = Resource_Pool< Bind_Group_Layout >::invalid_handle;
}

//
// Bind Groups
//

Bind_Group_Handle renderer_create_bind_group(const Bind_Group_Descriptor &descriptor)
{
    Bind_Group_Handle bind_group_handle = aquire_handle(&renderer_state->bind_groups);
    renderer->create_bind_group(bind_group_handle, descriptor);
    Bind_Group *bind_group = &renderer_state->bind_groups.data[bind_group_handle.index]; 
    bind_group->descriptor = descriptor;
    return bind_group_handle;
}

Bind_Group *renderer_get_bind_group(Bind_Group_Handle bind_group_handle)
{
    return get(&renderer_state->bind_groups, bind_group_handle);
}

void renderer_destroy_bind_group(Bind_Group_Handle &bind_group_handle)
{
    renderer->destroy_bind_group(bind_group_handle);
    release_handle(&renderer_state->bind_groups, bind_group_handle);
    bind_group_handle = Resource_Pool< Bind_Group >::invalid_handle;
}

//
// Pipeline States
//

Pipeline_State_Handle renderer_create_pipeline_state(const Pipeline_State_Descriptor &descriptor)
{
    Pipeline_State_Handle pipeline_state_handle = aquire_handle(&renderer_state->pipeline_states);
    renderer->create_pipeline_state(pipeline_state_handle, descriptor);

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
    renderer->create_render_pass(render_pass_handle, descriptor);
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
    renderer->create_frame_buffer(frame_buffer_handle, descriptor);
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
// Static Meshes
//

Static_Mesh_Handle renderer_create_static_mesh(const Static_Mesh_Descriptor &descriptor)
{
    Static_Mesh_Handle static_mesh_handle = aquire_handle(&renderer_state->static_meshes);
    renderer->create_static_mesh(static_mesh_handle, descriptor);
    return static_mesh_handle;
}

Static_Mesh *renderer_get_static_mesh(Static_Mesh_Handle static_mesh_handle)
{
    return get(&renderer_state->static_meshes, static_mesh_handle);
}

void renderer_destroy_static_mesh(Static_Mesh_Handle &static_mesh_handle)
{
    renderer->destroy_static_mesh(static_mesh_handle);
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
    Shader_Group *shader_group = get(&renderer_state->shader_groups, pipeline_state->descriptor.shader_group);

    Shader_Struct *properties = nullptr;

    for (U32 shader_index = 0; shader_index < shader_group->shaders.count; shader_index++)
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
        Buffer_Descriptor material_buffer_descriptor = {};
        material_buffer_descriptor.usage = Buffer_Usage::UNIFORM;
        material_buffer_descriptor.size = size;
        material_buffer_descriptor.is_device_local = false;
        material->buffers[frame_index] = renderer_create_buffer(material_buffer_descriptor);
    }

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        Bind_Group_Descriptor bind_group_descriptor = {};
        bind_group_descriptor.shader_group = pipeline_state->descriptor.shader_group;
        bind_group_descriptor.layout = shader_group->bind_group_layouts[2]; // todo(amer): @Hardcoding
        material->bind_groups[frame_index] = renderer_create_bind_group(bind_group_descriptor);

        Update_Binding_Descriptor update_binding_descriptors[1] = {};
        update_binding_descriptors[0].binding_number = 0;
        update_binding_descriptors[0].element_index = 0;
        update_binding_descriptors[0].count = 1;
        update_binding_descriptors[0].buffers = &material->buffers[frame_index];

        renderer->update_bind_group(material->bind_groups[frame_index], to_array_view(update_binding_descriptors));
    }

    material->pipeline_state_handle = descriptor.pipeline_state_handle;
    material->data = HE_ALLOCATE_ARRAY(&renderer_state->engine->memory.free_list_allocator, U8, size);
    material->size = size;
    material->properties = properties;

    return material_handle;
}

Material *renderer_get_material(Material_Handle material_handle)
{
    return get(&renderer_state->materials, material_handle);
}

void renderer_destroy_material(Material_Handle &material_handle)
{
    Material *material = get(&renderer_state->materials, material_handle);

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        renderer_destroy_buffer(material->buffers[frame_index]);
        renderer_destroy_bind_group(material->bind_groups[frame_index]);
    }

    deallocate(&renderer_state->engine->memory.free_list_allocator, material->data);
    release_handle(&renderer_state->materials, material_handle);

    material_handle = Resource_Pool< Material >::invalid_handle;
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

//
// Semaphores
//
Semaphore_Handle renderer_create_semaphore(const Renderer_Semaphore_Descriptor &descriptor)
{
    Semaphore_Handle semaphore_handle = aquire_handle(&renderer_state->semaphores);
    renderer->create_semaphore(semaphore_handle, descriptor);
    return semaphore_handle;
}

Renderer_Semaphore* renderer_get_semaphore(Semaphore_Handle semaphore_handle)
{
    return get(&renderer_state->semaphores, semaphore_handle);
}

void renderer_destroy_semaphore(Semaphore_Handle &semaphore_handle)
{
    renderer->destroy_semaphore(semaphore_handle);
    release_handle(&renderer_state->semaphores, semaphore_handle);
    semaphore_handle = Resource_Pool< Renderer_Semaphore >::invalid_handle;
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

    if (is_valid_handle(&renderer_state->samplers, renderer_state->default_sampler))
    {
        renderer->destroy_sampler(renderer_state->default_sampler);
    }

    renderer->create_sampler(renderer_state->default_sampler, default_sampler_descriptor);
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