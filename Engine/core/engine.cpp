#include "engine.h"
#include "platform.h"
#include "rendering/renderer.h"
#include "debugging.h"

#include <imgui.h>

extern Debug_State global_debug_state;

internal_function void init_imgui(Engine *engine)
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

internal_function void imgui_new_frame(Engine *engine)
{
    platform_imgui_new_frame();
    engine->renderer.imgui_new_frame();
    ImGui::NewFrame();

    if (engine->show_imgui)
    {
        if (engine->imgui_docking)
        {
            local_presist bool opt_fullscreen_persistant = true;
            bool opt_fullscreen = opt_fullscreen_persistant;
            local_presist ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

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

bool startup(Engine *engine, const Engine_Configuration &configuration, void *platform_state)
{

#ifndef HE_SHIPPING

    Logger *logger = &global_debug_state.main_logger;
    U64 channel_mask = 0xFFFFFFFFFFFFFFFF;
    bool logger_initied = init_logger(logger, "all", Verbosity_Trace, channel_mask);
    if (!logger_initied)
    {
        return false;
    }

#endif

    Mem_Size required_memory_size =
        configuration.permanent_memory_size + configuration.transient_memory_size;

    void *memory = platform_allocate_memory(required_memory_size);
    if (!memory)
    {
        return false;
    }

    U8 *permanent_memory = (U8 *)memory;
    engine->memory.permanent_memory_size = configuration.permanent_memory_size;
    engine->memory.permanent_arena = create_memory_arena(permanent_memory,
                                                         configuration.permanent_memory_size);

    U8 *transient_memory = (U8 *)memory + configuration.permanent_memory_size;
    engine->memory.transient_memory_size = configuration.transient_memory_size;
    engine->memory.transient_arena = create_memory_arena(transient_memory,
                                                         configuration.transient_memory_size);

    init_free_list_allocator(&engine->memory.free_list_allocator,
                             &engine->memory.transient_arena,
                             HE_MegaBytes(512));

    engine->show_cursor = configuration.show_cursor;
    engine->lock_cursor = configuration.lock_cursor;
    engine->window_mode = configuration.window_mode;
    engine->platform_state = platform_state;

    if (engine->window_mode == WindowMode_Fullscreen)
    {
        platform_toggle_fullscreen(engine);
    }

    bool input_inited = init_input(&engine->input);
    if (!input_inited)
    {
        return false;
    }

    init_imgui(engine);

    Renderer_State *renderer_state = &engine->renderer_state;
    renderer_state->engine = engine;
    renderer_state->textures = AllocateArray(&engine->memory.transient_arena, Texture, MAX_TEXTURE_COUNT);
    renderer_state->materials = AllocateArray(&engine->memory.transient_arena, Material, MAX_MATERIAL_COUNT);
    renderer_state->static_meshes = AllocateArray(&engine->memory.transient_arena, Static_Mesh, MAX_STATIC_MESH_COUNT);
    renderer_state->scene_nodes = AllocateArray(&engine->memory.transient_arena, Scene_Node, MAX_SCENE_NODE_COUNT);
    renderer_state->shaders = AllocateArray(&engine->memory.transient_arena, Shader, MAX_SHADER_COUNT);
    renderer_state->pipeline_states = AllocateArray(&engine->memory.transient_arena, Pipeline_State, MAX_PIPELINE_STATE_COUNT);

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

    renderer_state->back_buffer_width = configuration.back_buffer_width;
    renderer_state->back_buffer_height = configuration.back_buffer_height;

    Platform_API *api = &engine->platform_api;
    api->allocate_memory = &platform_allocate_memory;
    api->deallocate_memory = &platform_deallocate_memory;
    api->open_file = &platform_open_file;
    api->is_file_handle_valid = &platform_is_file_handle_valid;
    api->read_data_from_file = &platform_read_data_from_file;
    api->write_data_to_file = &platform_write_data_to_file;
    api->close_file = &platform_close_file;
    api->debug_printf = &platform_debug_printf;
    api->toggle_fullscreen = &platform_toggle_fullscreen;

    engine->api.init_camera = &init_camera;
    engine->api.init_fps_camera_controller = &init_fps_camera_controller;
    engine->api.control_camera = &control_camera;
    engine->api.update_camera = &update_camera;
    engine->api.load_model = &load_model;
    engine->api.render_scene_node = &render_scene_node;

    Game_Code *game_code = &engine->game_code;
    bool game_initialized = game_code->init_game(engine);
    renderer->wait_for_gpu_to_finish_all_work(renderer_state);
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

    ImGui::DragFloat("##Intensity", &directional_light->intensity, 0.1f, 0.0f, MAX_F32);

    ImGui::End();

    Game_Code *game_code = &engine->game_code;
    game_code->on_update(engine, delta_time);
}

void shutdown(Engine *engine)
{
    (void)engine;

    Renderer *renderer = &engine->renderer;
    Renderer_State *renderer_state = &engine->renderer_state;
    renderer->wait_for_gpu_to_finish_all_work(renderer_state);

    deinit_renderer_state(renderer, renderer_state);
    renderer->deinit(renderer_state);

    platform_shutdown_imgui();
    ImGui::DestroyContext();

#ifndef HE_SHIPPING
    Logger *logger = &global_debug_state.main_logger;
    deinit_logger(logger);
#endif
}

void set_game_code_to_stubs(Game_Code *game_code)
{
    game_code->init_game = &init_game_stub;
    game_code->on_event = &on_event_stub;
    game_code->on_update = &on_update_stub;
}

// todo(amer): maybe we should program a small game in the stubs...
bool init_game_stub(Engine *engine)
{
    (void)engine;
    return true;
}

void on_event_stub(Engine *engine, Event event)
{
    (void)engine;
    (void)event;
}

void on_update_stub(Engine *engine, F32 delta_time)
{
    (void)engine;
    (void)delta_time;
}