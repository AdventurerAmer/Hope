#include "engine.h"
#include "platform.h"
#include "rendering/renderer.h"
#include "debugging.h"
#include "cvars.h"
#include "job_system.h"

#include <chrono>

#include <imgui.h>

extern Debug_State global_debug_state;

static void init_imgui(Engine *engine)
{
    engine->show_imgui = false;
    engine->imgui_docking = false;

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
}

static void imgui_new_frame(Engine *engine)
{
    platform_imgui_new_frame();
    engine->renderer.imgui_new_frame();
    ImGui::NewFrame();

    if (engine->show_imgui)
    {
        if (engine->imgui_docking)
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
            ImGui::Begin("DockSpace", &engine->imgui_docking, window_flags);
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
    api->render_scene_node = &render_scene_node;
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
    engine->memory.permanent_arena = create_memory_arena(permanent_memory,
                                                         permenent_memory_size);

    U8 *transient_memory = (U8 *)memory + permenent_memory_size;
    engine->memory.transient_memory_size = transient_memory_size;
    engine->memory.transient_arena = create_memory_arena(transient_memory,
                                                         transient_memory_size);

    init_free_list_allocator(&engine->memory.free_list_allocator,
                             &engine->memory.transient_arena,
                             HE_MEGA(512));

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
    bool game_code_loaded = platform_load_dynamic_library(&game_code_dll, "../bin/Game.dll"); // note(amer): hard coding dynamic library extension (.dll)
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

    init_imgui(engine);

    Renderer_State *renderer_state = &engine->renderer_state;
    bool renderer_state_per_inited = pre_init_renderer_state(renderer_state, engine);
    if (!renderer_state_per_inited)
    {
        return false;
    }

    Renderer *renderer = &engine->renderer;
    bool renderer_requested = request_renderer(RenderingAPI_Vulkan, renderer);
    if (!renderer_requested)
    {
        return false;
    }

    bool renderer_inited = renderer->init(engine);
    if (!renderer_inited)
    {
        return false;
    }

    bool renderer_state_inited = init_renderer_state(renderer_state, engine);
    if (!renderer_state_inited)
    {
        return false;
    }

    Scene_Data *scene_data = &renderer_state->scene_data;
    scene_data->directional_light.direction = { 0.0f, -1.0f, 0.0f };
    scene_data->directional_light.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    scene_data->directional_light.intensity = 1.0f;

    auto start = std::chrono::steady_clock::now();

    bool game_initialized = game_code->init_game(engine);
    wait_for_all_jobs_to_finish();
    renderer->wait_for_gpu_to_finish_all_work();

    auto end = std::chrono::steady_clock::now();
    const std::chrono::duration< double > elapsed_seconds = end - start;
    HE_LOG(Core, Trace, "assets loaded %.2f ms to finish\n", elapsed_seconds * 1000.0);
    return game_initialized;
}

void game_loop(Engine *engine, F32 delta_time)
{
    imgui_new_frame(engine);

    Renderer *renderer = &engine->renderer;
    Renderer_State *renderer_state = &engine->renderer_state;
    Scene_Data *scene_data = &renderer_state->scene_data;
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

    ImGui::End();

    Game_Code *game_code = &engine->game_code;
    game_code->on_update(engine, delta_time);

    begin_temprary_memory_arena(&renderer_state->frame_arena, &renderer_state->arena);

    U32 current_frame_in_flight_index = renderer_state->current_frame_in_flight_index;

    Buffer *object_data_storage_buffer = get(&renderer_state->buffers, renderer_state->object_data_storage_buffers[current_frame_in_flight_index]);
    renderer_state->object_data_base = (Object_Data *)object_data_storage_buffer->data;
    renderer_state->object_data_count = 0;

    renderer->begin_frame(scene_data);

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

    renderer->set_vertex_buffers(vertex_buffers, offsets, HE_ARRAYCOUNT(vertex_buffers));
    renderer->set_index_buffer(renderer_state->index_buffer, 0);

    U32 texture_count = renderer_state->textures.capacity;
    Texture_Handle *textures = HE_ALLOCATE_ARRAY(&renderer_state->frame_arena, Texture_Handle, texture_count);
    Sampler_Handle *samplers = HE_ALLOCATE_ARRAY(&renderer_state->frame_arena, Sampler_Handle, texture_count);

    for (S32 texture_index = 0; texture_index < (S32)texture_count; texture_index++)
    {
        if (renderer_state->textures.is_allocated[texture_index])
        {
            textures[texture_index] = { texture_index, renderer_state->textures.generations[texture_index] };
        }
        else
        {
            textures[texture_index] = renderer_state->white_pixel_texture;
        }

        samplers[texture_index] = renderer_state->default_sampler;
    }

    Update_Binding_Descriptor update_textures_binding_descriptor = {};
    update_textures_binding_descriptor.binding_number = 0;
    update_textures_binding_descriptor.element_index = 0;
    update_textures_binding_descriptor.count = texture_count;
    update_textures_binding_descriptor.textures = textures;
    update_textures_binding_descriptor.samplers = samplers;
    renderer->update_bind_group(renderer_state->per_render_pass_bind_groups[current_frame_in_flight_index], &update_textures_binding_descriptor, 1);

    Bind_Group_Handle bind_groups[] =
    {
        renderer_state->per_frame_bind_groups[current_frame_in_flight_index],
        renderer_state->per_render_pass_bind_groups[current_frame_in_flight_index]
    };

    // todo(amer): i don't like mesh_shader_group here...
    renderer->set_bind_groups(0, bind_groups, HE_ARRAYCOUNT(bind_groups), renderer_state->mesh_shader_group);

    render_scene_node(renderer, renderer_state, renderer_state->root_scene_node, glm::mat4(1.0f));
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
    Renderer *renderer = &engine->renderer;
    Renderer_State *renderer_state = &engine->renderer_state;
    renderer->wait_for_gpu_to_finish_all_work();

    deinit_renderer_state(renderer, renderer_state);
    renderer->deinit();

    deinit_job_system();

    platform_shutdown_imgui();
    ImGui::DestroyContext();

#ifndef HE_SHIPPING
    Logger *logger = &global_debug_state.main_logger;
    deinit_logger(logger);
#endif

    deinit_cvars(engine);
}