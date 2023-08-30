#include "engine.h"
#include "platform.h"
#include "rendering/renderer.h"
#include "debugging.h"
#include "cvars.h"
#include "job_system.h"

HOPE_CVarString(engine_name, "name of the engine", "Hope", "platform", CVarFlag_None);
HOPE_CVarString(app_name, "name of the application", "Hope", "platform", CVarFlag_None);
HOPE_CVarInt(window_mode, "window mode", Window_Mode::WINDOWED, "platform", CVarFlag_None);
HOPE_CVarInt(window_width, "window width", -1, "platform", CVarFlag_None);
HOPE_CVarInt(window_height, "window height", -1, "platform", CVarFlag_None);

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
    api->init_camera = &init_camera;
    api->init_fps_camera_controller = &init_fps_camera_controller;
    api->control_camera = &control_camera;
    api->update_camera = &update_camera;
    api->load_model = &load_model;
    api->render_scene_node = &render_scene_node;
}

struct Print_Job_Data
{
    const char *string_to_print;
};

Job_Result print_string_job(const Job_Parameters &params)
{
    HOPE_Assert(params.data);
    Print_Job_Data *data = (Print_Job_Data *)params.data;
    Job_Result result = Job_Result::SUCCEEDED;
    platform_debug_printf(data->string_to_print);
    return result;
}

bool startup(Engine *engine, void *platform_state)
{
    U64 permenent_memory_size = HOPE_MegaBytes(512);
    U64 transient_memory_size = HOPE_GigaBytes(1);
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
                             HOPE_MegaBytes(512));

    init_cvars("config.cvars", engine);

    HOPE_CVarGetInt(window_mode, "platform");
    HOPE_CVarGetString(app_name, "platform");
    HOPE_CVarGetInt(window_width, "platform");
    HOPE_CVarGetInt(window_height, "platform");

#ifndef HOPE_SHIPPING

    U64 debug_state_arena_size = HOPE_MegaBytes(64);
    U8 *debug_state_arena_data = AllocateArray(&engine->memory.permanent_arena, U8, debug_state_arena_size);
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
    HOPE_Assert(game_code_loaded);

    Game_Code *game_code = &engine->game_code;
    game_code->init_game = (Init_Game_Proc)platform_get_proc_address(&game_code_dll, "init_game");
    game_code->on_event  = (On_Event_Proc)platform_get_proc_address(&game_code_dll, "on_event");
    game_code->on_update = (On_Update_Proc)platform_get_proc_address(&game_code_dll, "on_update");
    HOPE_Assert(game_code->init_game);
    HOPE_Assert(game_code->on_event);
    HOPE_Assert(game_code->on_update);

    hock_engine_api(&engine->api);

    engine->show_cursor = false;
    engine->lock_cursor = false;
    engine->platform_state = platform_state;

    if (*window_width == -1 || *window_height == -1)
    {
        // note(amer): video modes
        *window_width = 1296;
        *window_height = 759;
    }

    Window *window = &engine->window;
    bool window_created = platform_create_window(window, app_name->data, (U32)*window_width, (U32)*window_height, (Window_Mode)*window_mode);

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
    HOPE_Assert(job_system_inited);

    Print_Job_Data data0 = 
    {
        "ahmed\n"
    };

    Print_Job_Data data1 = 
    {
        "mohamed\n"
    };

    Job job0 = {};
    job0.proc = print_string_job;
    job0.parameters.data = &data0;
    execute_job(job0, JobFlag_GeneralPurpose);

    Job job1 = {};
    job1.proc = print_string_job;
    job1.parameters.data = &data1;
    execute_job(job1, JobFlag_GeneralPurpose);

    wait_for_all_jobs_to_finish();
    init_imgui(engine);

    Renderer_State *renderer_state = &engine->renderer_state;
    renderer_state->engine = engine;
    renderer_state->textures = AllocateArray(&engine->memory.transient_arena, Texture, MAX_TEXTURE_COUNT);
    renderer_state->materials = AllocateArray(&engine->memory.transient_arena, Material, MAX_MATERIAL_COUNT);
    renderer_state->static_meshes = AllocateArray(&engine->memory.transient_arena, Static_Mesh, MAX_STATIC_MESH_COUNT);
    renderer_state->scene_nodes = AllocateArray(&engine->memory.transient_arena, Scene_Node, MAX_SCENE_NODE_COUNT);
    renderer_state->shaders = AllocateArray(&engine->memory.transient_arena, Shader, MAX_SHADER_COUNT);
    renderer_state->pipeline_states = AllocateArray(&engine->memory.transient_arena, Pipeline_State, MAX_PIPELINE_STATE_COUNT);

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

    bool requested = request_renderer(RenderingAPI_Vulkan, &engine->renderer);
    if (!requested)
    {
        return false;
    }

    Renderer *renderer = &engine->renderer;
    bool renderer_inited = renderer->init(&engine->renderer_state,
                                          engine,
                                          &engine->memory.permanent_arena);
    if (!renderer_inited)
    {
        return false;
    }

    bool renderer_state_inited = init_renderer_state(engine,
                                                     renderer_state,
                                                     &engine->memory.transient_arena);
    if (!renderer_state_inited)
    {
        return false;
    }

    Scene_Data *scene_data = &renderer_state->scene_data;
    scene_data->directional_light.direction = { 0.0f, -1.0f, 0.0f };
    scene_data->directional_light.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    scene_data->directional_light.intensity = 1.0f;

    Platform_API *api = &engine->platform_api;
    api->allocate_memory = &platform_allocate_memory;
    api->deallocate_memory = &platform_deallocate_memory;
    api->debug_printf = &platform_debug_printf;

    bool game_initialized = game_code->init_game(engine);
    return game_initialized;
}

void game_loop(Engine *engine, F32 delta_time)
{
    imgui_new_frame(engine);

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

    ImGui::DragFloat("##Intensity", &directional_light->intensity, 0.1f, 0.0f, HOPE_MAX_F32);

    ImGui::End();

    Game_Code *game_code = &engine->game_code;
    game_code->on_update(engine, delta_time);
}

void shutdown(Engine *engine)
{
    HOPE_CVarGetInt(window_mode, "platform");
    HOPE_CVarGetInt(window_width, "platform");
    HOPE_CVarGetInt(window_height, "platform");

    Window *window = &engine->window;
    *window_mode = (S64)window->mode;
    *window_width = window->width;
    *window_height = window->height;

    Renderer *renderer = &engine->renderer;
    Renderer_State *renderer_state = &engine->renderer_state;
    renderer->wait_for_gpu_to_finish_all_work(renderer_state);

    deinit_renderer_state(renderer, renderer_state);
    renderer->deinit(renderer_state);

    deinit_job_system();

    platform_shutdown_imgui();
    ImGui::DestroyContext();

#ifndef HOPE_SHIPPING
    Logger *logger = &global_debug_state.main_logger;
    deinit_logger(logger);
#endif

    deinit_cvars(engine);
}