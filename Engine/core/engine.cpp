#include "engine.h"
#include "platform.h"
#include "rendering/renderer.h"
#include "debugging.h"
#include "cvars.h"
#include "job_system.h"
#include "file_system.h"
#include "resources/resource_system.h"

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
    api->get_render_context = &get_render_context;
}

bool startup(Engine *engine, void *platform_state)
{
    hock_engine_api(&engine->api);

#ifndef HE_SHIPPING

    U64 debug_memory_size = HE_MEGA(64);
    void *debug_memory = platform_allocate_memory(debug_memory_size);
    if (!debug_memory)
    {
        return false;
    }

    global_debug_state.arena = create_memory_arena(debug_memory, debug_memory_size);
    Logger* logger = &global_debug_state.main_logger;
    U64 channel_mask = 0xFFFFFFFFFFFFFFFF;
    bool logger_initied = init_logger(logger, "all", Verbosity_Trace, channel_mask, &global_debug_state.arena);
    if (!logger_initied)
    {
        return false;
    }

#endif

    U64 permenent_memory_size = HE_GIGA(2);
    U64 transient_memory_size = HE_GIGA(2);
    Size required_memory_size = permenent_memory_size + transient_memory_size;

    void *memory = platform_allocate_memory(required_memory_size);
    if (!memory)
    {
        HE_LOG(Core, Fetal, "failed to initialize memory system\n");
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

    engine->show_cursor = false;
    engine->lock_cursor = false;
    engine->platform_state = platform_state;
    engine->name = HE_STRING_LITERAL("Hope");
    engine->app_name = HE_STRING_LITERAL("Hope");

    String &engine_name = engine->name;
    String &app_name = engine->app_name;
    
    Window *window = &engine->window;
    window->width = 1296;
    window->height = 759;
    window->mode = Window_Mode::WINDOWED;

    U32 &window_width = window->width;
    U32 &window_height = window->height;
    U8 &window_mode = (U8 &)window->mode;
    
    HE_DECLARE_CVAR("platform", engine_name, CVarFlag_None);
    HE_DECLARE_CVAR("platform", app_name, CVarFlag_None);
    HE_DECLARE_CVAR("platform", window_width, CVarFlag_None);
    HE_DECLARE_CVAR("platform", window_height, CVarFlag_None);
    HE_DECLARE_CVAR("platform", window_mode, CVarFlag_None);

    // note(amer): @HardCoding dynamic library extension (.dll)
    Dynamic_Library game_code_dll = {};
    bool game_code_loaded = platform_load_dynamic_library(&game_code_dll, "../bin/Game.dll");
    HE_ASSERT(game_code_loaded);

    Game_Code *game_code = &engine->game_code;
    game_code->init_game = (Init_Game_Proc)platform_get_proc_address(&game_code_dll, "init_game");
    game_code->on_event  = (On_Event_Proc)platform_get_proc_address(&game_code_dll, "on_event");
    game_code->on_update = (On_Update_Proc)platform_get_proc_address(&game_code_dll, "on_update");
    if (!game_code->init_game || !game_code->on_event || !game_code->on_update)
    {
        HE_LOG(Core, Fetal, "failed to load game code\n");
        return false;    
    }

    bool window_created = platform_create_window(window, app_name.data, (U32)window_width, (U32)window_height, (Window_Mode)window_mode);
    if (!window_created)
    {
        HE_LOG(Core, Fetal, "failed to create window\n");
        return false;
    }

    bool input_inited = init_input(&engine->input);
    if (!input_inited)
    {
        HE_LOG(Core, Fetal, "failed to initialize input system\n");
        return false;
    }

    bool job_system_inited = init_job_system(engine);
    if (!job_system_inited)
    {
        HE_LOG(Core, Fetal, "failed to initialize job system\n");
        return false;
    }

    bool renderer_state_inited = init_renderer_state(engine);
    if (!renderer_state_inited)
    {
        HE_LOG(Core, Fetal, "failed to initialize render system\n");
        return false;
    }

    bool resource_system_inited = init_resource_system(HE_STRING_LITERAL("resources"), engine);
    if (!resource_system_inited)
    {
        HE_LOG(Core, Fetal, "failed to initialize resource system\n");
        return false;
    }

    Render_Context render_context = get_render_context();
    Scene_Data *scene_data = &render_context.renderer_state->scene_data;

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
    if (client_width != 0 && client_height != 0)
    {
        renderer_on_resize(client_width, client_height);
    }
}

static void draw_transform(Transform &transform)
{
    ImGui::Text("Position");
    ImGui::SameLine();
    ImGui::DragFloat3("##Position", &transform.position.x, 0.1f);

    ImGui::Text("Rotation");
    ImGui::SameLine();
    
    auto mod_angle = [](F32 angle, F32 range) -> F32
    {
        if (angle < 0.0f)
        {
            F32 result = glm::mod(angle, -range);
            return result + 360.0f;
        }
        
        return glm::mod(angle, range);
    };
    
    if (ImGui::DragFloat3("##Rotation", &transform.euler_angles.x, 0.5f, -360.0f, 360.0f))
    {
        transform.euler_angles.x = mod_angle(transform.euler_angles.x, 360.0f);
        transform.euler_angles.y = mod_angle(transform.euler_angles.y, 360.0f);
        transform.euler_angles.z = mod_angle(transform.euler_angles.z, 360.0f);
        transform.rotation = glm::quat(glm::radians(transform.euler_angles));
    }

    ImGui::Text("Scale");
    ImGui::SameLine();
    ImGui::DragFloat3("##Scale", &transform.scale.x, 0.25f);
}

Scene_Node *selected_node = nullptr;

static void draw_tree(Scene_Node *node)
{
    ImGui::PushID(node);

    ImGuiTreeNodeFlags flags = 0;

    if (node == selected_node)
    {
        flags = ImGuiTreeNodeFlags_Selected;
    }

    if (!node->first_child)
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    bool is_open = ImGui::TreeNodeEx(node->name.data, flags, "%.*s", HE_EXPAND_STRING(node->name));

    if (ImGui::IsItemClicked())
    {
        selected_node = node;
    }

    if (is_open)
    {
        for (Scene_Node *child = node->first_child; child; child = child->next_sibling)
        {
            draw_tree(child);
        }
        
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void game_loop(Engine *engine, F32 delta_time)
{
    imgui_new_frame();

    Render_Context render_context = get_render_context();
    Scene_Data *scene_data = &render_context.renderer_state->scene_data;
    Directional_Light *directional_light = &scene_data->directional_light;

    Renderer *renderer = render_context.renderer;
    Renderer_State *renderer_state = render_context.renderer_state;

    // ImGui Graphics Settings
    {
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

        ImGui::Text("");
        ImGui::Text("Settings");
        ImGui::Separator();

        //
        // VSync
        //
        {
            ImGui::Text("VSync");
            ImGui::SameLine();

            static bool vsync = renderer_state->vsync;
            if (ImGui::Checkbox("##VSync", &vsync))
            {
                renderer_set_vsync(vsync);
            }
        }

        //
        // Triple Buffering
        //
        {
            ImGui::Text("Triple Buffering");
            ImGui::SameLine();

            if (ImGui::Checkbox("##Triple Buffering", &renderer_state->triple_buffering))
            {
                if (renderer_state->triple_buffering)
                {
                    renderer_state->frames_in_flight = 3;
                }
                else
                {
                    renderer_state->frames_in_flight = 2;
                }
            }
        }

        //
        // Gamma
        //
        {
            ImGui::Text("Gamma");
            ImGui::SameLine();
            ImGui::SliderFloat("##Gamma", &renderer_state->gamma, 2.0, 2.4, "%.4f", ImGuiSliderFlags_AlwaysClamp);
        }

        //
        // Anisotropic Filtering
        //
        {
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
            for (U32 anisotropic_filtering_index = 0; anisotropic_filtering_index < HE_ARRAYCOUNT(anisotropic_filtering_text); anisotropic_filtering_index++)
            {
                if ((U32)renderer_state->anisotropic_filtering_setting == anisotropic_filtering_index)
                {
                    selected_anisotropic_filtering = anisotropic_filtering_text[anisotropic_filtering_index];
                    break;
                }
            }

            if (ImGui::BeginCombo("##Anistropic Filtering", selected_anisotropic_filtering))
            {
                for (U32 anisotropic_filtering_index = 0; anisotropic_filtering_index < HE_ARRAYCOUNT(anisotropic_filtering_text); anisotropic_filtering_index++)
                {
                    bool is_selected = (U32)renderer_state->anisotropic_filtering_setting == anisotropic_filtering_index;
                    if (ImGui::Selectable(anisotropic_filtering_text[anisotropic_filtering_index], is_selected))
                    {
                        renderer_set_anisotropic_filtering((Anisotropic_Filtering_Setting)anisotropic_filtering_index);
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
            for (U32 msaa_setting_index = 0; msaa_setting_index < HE_ARRAYCOUNT(msaa_text); msaa_setting_index++)
            {
                if ((U32)renderer_state->msaa_setting == msaa_setting_index)
                {
                    selected_msaa = msaa_text[msaa_setting_index];
                    break;
                }
            }

            if (ImGui::BeginCombo("##MSAA", selected_msaa))
            {
                for (U32 msaa_setting_index = 0; msaa_setting_index < HE_ARRAYCOUNT(msaa_text); msaa_setting_index++)
                {
                    bool is_selected = (U32)renderer_state->msaa_setting == msaa_setting_index;
                    if (ImGui::Selectable(msaa_text[msaa_setting_index], is_selected))
                    {
                        renderer_set_msaa((MSAA_Setting)msaa_setting_index);
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
    }

    // Scene
    {
        ImGui::Begin("Scene");

        for (Scene_Node *node = renderer_state->root_scene_node->first_child; node; node = node->next_sibling)
        {
            draw_tree(node);
        }

        ImGui::End();
    }

    // Inspector
    {
        ImGui::Begin("Inspector");
        
        if (selected_node)
        {
            ImGui::Text("Node: %.*s", HE_EXPAND_STRING(selected_node->name));
            ImGui::Separator();
            
            ImGui::Text("Transfom");
            ImGui::Separator();
            
            draw_transform(selected_node->transform);
        }

        ImGui::End();
    }

    Game_Code *game_code = &engine->game_code;
    game_code->on_update(engine, delta_time);

    begin_temprary_memory_arena(&renderer_state->frame_arena, &renderer_state->arena);

    U32 frame_index = renderer_state->current_frame_in_flight_index;

    Buffer *object_data_storage_buffer = get(&renderer_state->buffers, renderer_state->object_data_storage_buffers[frame_index]);
    renderer_state->object_data_base = (Object_Data *)object_data_storage_buffer->data;
    renderer_state->object_data_count = 0;
    
    renderer->begin_frame(scene_data);
    render(&renderer_state->render_graph, renderer, renderer_state);
    renderer->end_frame();

    renderer_state->current_frame_in_flight_index++;
    if (renderer_state->current_frame_in_flight_index >= renderer_state->frames_in_flight)
    {
        renderer_state->current_frame_in_flight_index = 0;
    }

    end_temprary_memory_arena(&renderer_state->frame_arena);

    platform_lock_mutex(&renderer_state->allocation_groups_mutex);

    for (U32 allocation_group_index = 0; allocation_group_index < renderer_state->allocation_groups.count; allocation_group_index++)
    {
        Allocation_Group &allocation_group = renderer_state->allocation_groups[allocation_group_index];
        
        if (allocation_group.target_value == renderer->get_semaphore_value(allocation_group.semaphore))
        {
            if (allocation_group.resource_index != -1)
            {
                Resource_Ref ref = { allocation_group.resource_index };
                Resource *resource = get_resource(ref);
                platform_lock_mutex(&resource->mutex);
                HE_ASSERT(resource->state != Resource_State::LOADED);
                resource->state = Resource_State::LOADED;
                platform_unlock_mutex(&resource->mutex);
                Texture *texture = get<Texture>(ref);
                HE_LOG(Resource, Trace, "resource loaded: %.*s\n", HE_EXPAND_STRING(allocation_group.resource_name));
            }
            
            renderer_destroy_semaphore(allocation_group.semaphore);
            
            switch (allocation_group.type)
            {
                case Allocation_Group_Type::GENERAL:
                {
                    for (void *memory : allocation_group.allocations)
                    {
                        deallocate(&renderer_state->transfer_allocator, memory);
                    }
                } break;

                case Allocation_Group_Type::MODEL:
                {
                    unload_model(&allocation_group);
                } break;
            };

            remove_and_swap_back(&renderer_state->allocation_groups, allocation_group_index);
        }
    }
    
    platform_unlock_mutex(&renderer_state->allocation_groups_mutex);
}

void shutdown(Engine *engine)
{
    deinit_resource_system();

    deinit_renderer_state();

    deinit_job_system();

#ifndef HE_SHIPPING
    Logger *logger = &global_debug_state.main_logger;
    deinit_logger(logger);
#endif

    deinit_cvars(engine);
}