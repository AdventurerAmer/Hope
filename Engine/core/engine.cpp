#include "engine.h"
#include "platform.h"
#include "rendering/renderer.h"
#include "debugging.h"
#include "cvars.h"
#include "job_system.h"

#include <chrono>
#include <imgui.h>

extern Debug_State global_debug_state;

void hock_engine_api(Engine_API *api)
{
    api->allocate_memory = &platform_allocate_memory;
    api->deallocate_memory = &platform_deallocate_memory;
    api->debug_printf = &platform_debug_printf;
    api->set_window_mode = &platform_set_window_mode;
    api->init_camera = &init_camera;
    api->init_fps_camera_controller = &init_fps_camera_controller;
    api->control_camera = &control_camera;
    api->update_camera = &update_camera;
    api->load_model_threaded = &load_model_threaded;
    api->get_viewport = &renderer_get_viewport;
    api->get_scene_data = &renderer_get_scene_data;
}

bool startup(Engine *engine, void *platform_state)
{
    U64 permenent_memory_size = HE_GIGA(1);
    U64 transient_memory_size = HE_GIGA(1);
    Size required_memory_size = permenent_memory_size + transient_memory_size;

    void *memory = platform_allocate_memory(required_memory_size);
    if (!memory)
    {
        return false;
    }

    U8 *permanent_memory = (U8 *)memory;
    engine->memory.permanent_memory_size = permenent_memory_size;
    engine->memory.permanent_arena = create_memory_arena(permanent_memory, permenent_memory_size);

    U8 *transient_memory = (U8 *)memory + permenent_memory_size;
    engine->memory.transient_memory_size = transient_memory_size;
    engine->memory.transient_arena = create_memory_arena(transient_memory, transient_memory_size);

    init_free_list_allocator(&engine->memory.free_list_allocator, &engine->memory.transient_arena, HE_MEGA(512));

    init_cvars("config.cvars", engine);

#ifndef HE_SHIPPING

    U64 debug_state_arena_size = HE_MEGA(64);
    U8 *debug_state_arena_data = HE_ALLOCATE_ARRAY(&engine->memory.permanent_arena, U8, debug_state_arena_size);
    global_debug_state.arena = create_memory_arena(debug_state_arena_data, debug_state_arena_size);

    Logger* logger = &global_debug_state.main_logger;
    U64 channel_mask = 0xFFFFFFFFFFFFFFFF;
    bool logger_initied = init_logger(logger, "all", Verbosity_Trace, channel_mask, &engine->memory.transient_arena);
    if (!logger_initied)
    {
        return false;
    }

#endif

    Dynamic_Library game_code_dll = {};
    bool game_code_loaded = platform_load_dynamic_library(&game_code_dll, "../bin/Game.dll"); // note(amer): @HardCoding dynamic library extension (.dll)
    HE_ASSERT(game_code_loaded);

    Game_Code *game_code = &engine->game_code;
    game_code->init_game = (Init_Game_Proc)platform_get_proc_address(&game_code_dll, "init_game");
    game_code->on_event  = (On_Event_Proc)platform_get_proc_address(&game_code_dll, "on_event");
    game_code->on_update = (On_Update_Proc)platform_get_proc_address(&game_code_dll, "on_update");
    HE_ASSERT(game_code->init_game);
    HE_ASSERT(game_code->on_event);
    HE_ASSERT(game_code->on_update);

    hock_engine_api(&engine->api);

    engine->show_cursor = false;
    engine->lock_cursor = false;
    engine->platform_state = platform_state;
    engine->name = HE_STRING_LITERAL("Hope");
    engine->app_name = HE_STRING_LITERAL("Hope");

    String &engine_name = engine->name;
    String &app_name = engine->app_name;

    HE_DECLARE_CVAR("platform", engine_name, CVarFlag_None);
    HE_DECLARE_CVAR("platform", app_name, CVarFlag_None);

    Window *window = &engine->window;
    window->width = 1296;
    window->height = 759;
    window->mode = Window_Mode::WINDOWED;

    U32 &window_width = window->width;
    U32 &window_height = window->height;
    U8 &window_mode = (U8 &)window->mode;

    HE_DECLARE_CVAR("platform", window_width, CVarFlag_None);
    HE_DECLARE_CVAR("platform", window_height, CVarFlag_None);
    HE_DECLARE_CVAR("platform", window_mode, CVarFlag_None);

    bool window_created = platform_create_window(window, app_name.data, (U32)window_width, (U32)window_height, (Window_Mode)window_mode);

    if (!window_created)
    {
        return false;
    }

    bool input_inited = init_input(&engine->input);
    if (!input_inited)
    {
        return false;
    }

    bool job_system_inited = init_job_system(engine);
    HE_ASSERT(job_system_inited);

    bool renderer_state_per_inited = pre_init_renderer_state(engine);
    if (!renderer_state_per_inited)
    {
        return false;
    }

    bool renderer_state_inited = init_renderer_state(engine);
    if (!renderer_state_inited)
    {
        return false;
    }

    Scene_Data *scene_data = renderer_get_scene_data();
    scene_data->directional_light.direction = { 0.0f, -1.0f, 0.0f };
    scene_data->directional_light.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    scene_data->directional_light.intensity = 1.0f;

    auto start = std::chrono::steady_clock::now();

    bool game_initialized = game_code->init_game(engine);
    wait_for_all_jobs_to_finish();
    renderer_wait_for_gpu_to_finish_all_work();

    auto end = std::chrono::steady_clock::now();
    const std::chrono::duration< double > elapsed_seconds = end - start;
    HE_LOG(Core, Trace, "assets loaded %.2f ms to finish\n", elapsed_seconds * 1000.0);
    return game_initialized;
}

void on_resize(Engine *engine, U32 window_width, U32 window_height, U32 client_width, U32 client_height)
{
    Window *window = &engine->window;
    window->width = window_width;
    window->height = window_height;
    
    renderer_on_resize(client_width, client_height);
}

void game_loop(Engine *engine, F32 delta_time)
{
    imgui_new_frame();

    Scene_Data *scene_data = renderer_get_scene_data();
    Directional_Light *directional_light = &scene_data->directional_light;

    ImGui::Begin("Graphics");

    ImGui::Text("Directional Light");
    ImGui::Separator();

    ImGui::Text("Direction");
    ImGui::SameLine();

    ImGui::DragFloat3("##Direction", &directional_light->direction.x, 0.1f, -1.0f, 1.0f);

    if (glm::length(directional_light->direction) > 0.0f)
    {
        directional_light->direction = glm::normalize(directional_light->direction);
    }

    ImGui::Text("Color");
    ImGui::SameLine();

    ImGui::ColorEdit4("##Color", &directional_light->color.r);

    ImGui::Text("Intensity");
    ImGui::SameLine();

    ImGui::DragFloat("##Intensity", &directional_light->intensity, 0.1f, 0.0f, HE_MAX_F32);

    Renderer *renderer = get_renderer();
    Renderer_State *renderer_state = get_renderer_state();

    //
    // Anisotropic Filtering
    //
    {
        U32 anisotropic_filtering[] =
        {
            0,
            2,
            4,
            8,
            16
        };

        const char *anisotropic_filtering_text[] =
        {
            "NONE",
            "X2  ",
            "X4  ",
            "X8  ",
            "X16 "
        };

        ImGui::Text("Anisotropic Filtering");
        ImGui::SameLine();

        const char *selected_anisotropic_filtering = nullptr;
        for (U32 i = 0; i < HE_ARRAYCOUNT(anisotropic_filtering); i++)
        {
            if (renderer_state->anisotropic_filtering == anisotropic_filtering[i])
            {
                selected_anisotropic_filtering = anisotropic_filtering_text[i];
                break;
            }
        }

        if (ImGui::BeginCombo("##Anistropic Filtering", selected_anisotropic_filtering))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(anisotropic_filtering); i++)
            {
                bool is_selected = renderer_state->anisotropic_filtering == anisotropic_filtering[i];
                if (ImGui::Selectable(anisotropic_filtering_text[i], is_selected))
                {
                    if (renderer_state->anisotropic_filtering != anisotropic_filtering[i])
                    {
                        renderer_state->anisotropic_filtering = anisotropic_filtering[i];
                        renderer->wait_for_gpu_to_finish_all_work();

                        Sampler_Descriptor default_sampler_descriptor = {};
                        default_sampler_descriptor.min_filter = Filter::LINEAR;
                        default_sampler_descriptor.mag_filter = Filter::NEAREST;
                        default_sampler_descriptor.mip_filter = Filter::LINEAR;
                        default_sampler_descriptor.address_mode_u = Address_Mode::REPEAT;
                        default_sampler_descriptor.address_mode_v = Address_Mode::REPEAT;
                        default_sampler_descriptor.address_mode_w = Address_Mode::REPEAT;
                        default_sampler_descriptor.anisotropy = renderer_state->anisotropic_filtering;

                        renderer->destroy_sampler(renderer_state->default_sampler);
                        renderer->create_sampler(renderer_state->default_sampler, default_sampler_descriptor);
                    }
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    //
    // MSAA
    //
    {
        U32 msaa[] =
        {
            1,
            2,
            4,
            8
        };

        const char* msaa_text[] =
        {
            "NONE",
            "X2  ",
            "X4  ",
            "X8  "
        };

        ImGui::Text("MSAA");
        ImGui::SameLine();

        const char *selected_msaa = nullptr;
        for (U32 i = 0; i < HE_ARRAYCOUNT(msaa); i++)
        {
            if (renderer_state->sample_count == msaa[i])
            {
                selected_msaa = msaa_text[i];
                break;
            }
        }

        if (ImGui::BeginCombo("##MSAA", selected_msaa))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(msaa); i++)
            {
                bool is_selected = renderer_state->sample_count == msaa[i];
                if (ImGui::Selectable(msaa_text[i], is_selected))
                {
                    if (renderer_state->sample_count != msaa[i])
                    {
                        renderer_state->sample_count = msaa[i];
                        invalidate(&renderer_state->render_graph, renderer, renderer_state, renderer_state->back_buffer_width, renderer_state->back_buffer_height);
                    }
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }


    ImGui::End();

    Game_Code *game_code = &engine->game_code;
    game_code->on_update(engine, delta_time);

    begin_temprary_memory_arena(&renderer_state->frame_arena, &renderer_state->arena);

    U32 current_frame_in_flight_index = renderer_state->current_frame_in_flight_index;

    Buffer *object_data_storage_buffer = get(&renderer_state->buffers, renderer_state->object_data_storage_buffers[current_frame_in_flight_index]);
    renderer_state->object_data_base = (Object_Data *)object_data_storage_buffer->data;
    renderer_state->object_data_count = 0;
    
    renderer->begin_frame(scene_data);
    
#if HE_RENDER_GRAPH
    render(&renderer_state->render_graph, renderer, renderer_state);
#else
    Frame_Buffer *frame_buffer = get(&renderer_state->frame_buffers, renderer_state->world_frame_buffers[current_frame_in_flight_index]);
    if ((frame_buffer->width != renderer_state->back_buffer_width || frame_buffer->height != renderer_state->back_buffer_height)
        && (renderer_state->back_buffer_width != 0 && renderer_state->back_buffer_height != 0))
    {
        invalidate_render_entities();
    }

    renderer->set_viewport(renderer_state->back_buffer_width, renderer_state->back_buffer_height);

    Clear_Value clear_values[3] = {};
    U32 clear_value_count = 3;

    if (renderer_state->sample_count != 1)
    {
        clear_values[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
        clear_values[1].color = { 1.0f, 0.0f, 1.0f, 1.0f };
        clear_values[2].depth = 1.0f;

    }
    else
    {
        clear_value_count = 2;
        clear_values[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
        clear_values[1].depth = 1.0f;
    }

    renderer->begin_render_pass(renderer_state->world_render_pass, renderer_state->world_frame_buffers[current_frame_in_flight_index], to_array_view(clear_values));

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

    Update_Binding_Descriptor update_textures_binding_descriptors[1] = {};
    update_textures_binding_descriptors[0].binding_number = 0;
    update_textures_binding_descriptors[0].element_index = 0;
    update_textures_binding_descriptors[0].count = texture_count;
    update_textures_binding_descriptors[0].textures = textures;
    update_textures_binding_descriptors[0].samplers = samplers;
    renderer->update_bind_group(renderer_state->per_render_pass_bind_groups[current_frame_in_flight_index], to_array_view(update_textures_binding_descriptors));

    Bind_Group_Handle bind_groups[] =
    {
        renderer_state->per_frame_bind_groups[current_frame_in_flight_index],
        renderer_state->per_render_pass_bind_groups[current_frame_in_flight_index]
    };

    renderer->set_bind_groups(0, to_array_view(bind_groups));

    render_scene_node(renderer_state->root_scene_node, glm::mat4(1.0f));

    renderer->end_render_pass(renderer_state->world_render_pass);

    Clear_Value ui_clear_values[1] = {};
    renderer->begin_render_pass(renderer_state->ui_render_pass, renderer_state->ui_frame_buffers[current_frame_in_flight_index], to_array_view(ui_clear_values));
    renderer->imgui_render();
    renderer->end_render_pass(renderer_state->ui_render_pass);

#endif

    renderer->end_frame();

    renderer_state->current_frame_in_flight_index++;
    if (renderer_state->current_frame_in_flight_index == renderer_state->frames_in_flight)
    {
        renderer_state->current_frame_in_flight_index = 0;
    }

    end_temprary_memory_arena(&renderer_state->frame_arena);
}

void shutdown(Engine *engine)
{
    deinit_renderer_state();

    deinit_job_system();

#ifndef HE_SHIPPING
    Logger *logger = &global_debug_state.main_logger;
    deinit_logger(logger);
#endif

    deinit_cvars(engine);
}