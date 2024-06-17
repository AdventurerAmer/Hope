#include "editor.h"

#include <core/engine.h>
#include <core/platform.h>
#include <core/job_system.h>
#include <core/file_system.h>

#include <assets/asset_manager.h>

#include <imgui/imgui.h>
#include <ImGui/imgui_internal.h>
#include <ImGuizmo/ImGuizmo.h>

#include "editor_utils.h"
#include "widgets/inspector_panel.h"
#include "widgets/scene_hierarchy_panel.h"
#include "widgets/assets_panel.h"

struct Editor_State
{
    Engine *engine;
	Camera camera;
	FPS_Camera_Controller camera_controller;
    Asset_Handle scene_asset;

    ImGuizmo::OPERATION operation = ImGuizmo::OPERATION::TRANSLATE;
    ImGuizmo::MODE guizmo_mode = ImGuizmo::MODE::WORLD;
    bool show_ui_panels = false;
    bool show_stats_panel = true;
};

static Editor_State editor_state;

void draw_graphics_window();

bool hope_app_init(Engine *engine)
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    Editor_State *state = &editor_state;
    state->engine = engine;

    ImGuizmo::AllowAxisFlip(false);
    
    ImGuizmo::Style &style = ImGuizmo::GetStyle();
    style.CenterCircleSize = 10.0f;

    style.TranslationLineArrowSize = 10.0f;
    style.TranslationLineThickness = 5.0f;

    style.ScaleLineThickness = 5.0f;
    style.ScaleLineCircleSize = 10.0f;

    style.RotationLineThickness = 5.0f;
    style.RotationOuterLineThickness = 5.0f;
    
    style.HatchedAxisLineThickness = 10.0f;

    String asset_path = get_asset_path();
    Assets_Panel::set_path(asset_path);

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;
    glm::vec2 viewport = { render_context.renderer_state->back_buffer_width, render_context.renderer_state->back_buffer_height };

    F32 aspect_ratio = viewport.x / viewport.y;
    glm::quat camera_rotation = glm::quat();
    Camera *camera = &editor_state.camera;
	F32 fov = 70.0f;
	F32 _near = 0.1f;
	F32 _far = 1000.0f;
	init_camera(camera, { 0.0f, 0.3f, 1.0f }, camera_rotation, aspect_ratio, fov, _near, _far);

    FPS_Camera_Controller *camera_controller = &editor_state.camera_controller;
    F32 rotation_speed = 45.0f;
    F32 base_movement_speed = 3.0f;
    F32 max_movement_speed = 5.0f;
	F32 sensitivity_x = 1.0f;
	F32 sensitivity_y = 1.0f;

	init_fps_camera_controller(camera_controller, /*pitch=*/0.0f, /*yaw=*/0.0f, rotation_speed, base_movement_speed, max_movement_speed, sensitivity_x, sensitivity_y);

    Asset_Handle scene_asset = import_asset(HE_STRING_LITERAL("main.hascene"));

    if (!is_asset_handle_valid(scene_asset))
    {
        String scene_name = HE_STRING_LITERAL("main");
        Scene_Handle scene_handle = renderer_create_scene(scene_name, 1);
        String save_path = format_string(scratch_memory.arena, "%.*s/%.*s.hascene", HE_EXPAND_STRING(get_asset_path()), HE_EXPAND_STRING(scene_name));
        serialize_scene(scene_handle, save_path);
        renderer_destroy_scene(scene_handle);
    }

    scene_asset = import_asset(HE_STRING_LITERAL("main.hascene"));
    editor_state.scene_asset = scene_asset;
    
    return true;
}

void hope_app_on_event(Engine *engine, Event event)
{
	switch (event.type)
	{
        case Event_Type::KEY:
		{
			if (event.pressed)
			{
				if (event.key == HE_KEY_F11)
				{
                    Window *window = &engine->window;
                    if (window->mode == Window_Mode::WINDOWED)
                    {
                        platform_set_window_mode(window, Window_Mode::FULLSCREEN);
                    }
                    else
                    {
                        platform_set_window_mode(window, Window_Mode::WINDOWED);
                    }
                }
                else if (event.key == HE_KEY_F10)
                {
                    editor_state.show_ui_panels = !editor_state.show_ui_panels;
                }
                else if (event.key == HE_KEY_S)
                {
                    if (event.is_control_down)
                    {
                        if (is_asset_handle_valid(editor_state.scene_asset))
                        {
                            Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
                            const Asset_Registry_Entry &entry = get_asset_registry_entry(editor_state.scene_asset);
                            String scene_path = format_string(scratch_memory.arena, "%.*s/%.*s", HE_EXPAND_STRING(get_asset_path()), HE_EXPAND_STRING(entry.path));
                            serialize_scene(get_asset_handle_as<Scene>(editor_state.scene_asset), scene_path);
                        }
                    }
                }

                ImGuiHoveredFlags hover_flags = ImGuiHoveredFlags_AnyWindow|ImGuiHoveredFlags_AllowWhenBlockedByPopup;
                bool interacting_with_imgui = ImGui::IsWindowHovered(hover_flags) || ImGui::IsAnyItemHovered();
                if (engine->input.button_states[HE_BUTTON_RIGHT] == Input_State::RELEASED && !interacting_with_imgui)
                {
                    if (event.key == HE_KEY_Q)
                    {
                        Editor::reset_selection();
                    }
                    else if (event.key == HE_KEY_W)
                    {
                        editor_state.operation = ImGuizmo::OPERATION::TRANSLATE;
                    }
                    else if (event.key == HE_KEY_E)
                    {
                        editor_state.operation = ImGuizmo::OPERATION::ROTATE;
                    }
                    else if (event.key == HE_KEY_R)
                    {
                        editor_state.operation = ImGuizmo::OPERATION::SCALE;
                    }
                    else if (event.key == HE_KEY_T)
                    {
                        if (editor_state.guizmo_mode == ImGuizmo::MODE::WORLD)
                        {
                            editor_state.guizmo_mode = ImGuizmo::MODE::LOCAL;
                        }
                        else
                        {
                            editor_state.guizmo_mode = ImGuizmo::MODE::WORLD;
                        }
                    }
                }

                if (event.is_control_down && event.key == HE_KEY_N)
                {
                    Scene_Handle scene_handle = get_asset_handle_as<Scene>(editor_state.scene_asset);
                    Scene *scene = renderer_get_scene(scene_handle);
                    Scene_Hierarchy_Panel::new_node(scene);
                }

                S32 selected_node_index = Scene_Hierarchy_Panel::get_selected_node();
                if (selected_node_index != -1)
                {
                    Scene_Handle scene_handle = get_asset_handle_as<Scene>(editor_state.scene_asset);
                    Scene *scene = renderer_get_scene(scene_handle);

                    if (event.key == HE_KEY_F2)
                    {
                        Scene_Hierarchy_Panel::rename_node(scene, selected_node_index);
                    }
                    else if (event.key == HE_KEY_DELETE)
                    {
                        Scene_Hierarchy_Panel::delete_node(scene, (U32)selected_node_index);
                    }
                    else if (event.is_control_down && event.key == HE_KEY_D)
                    {
                        Scene_Hierarchy_Panel::duplicate_node(scene, (U32)selected_node_index);
                    }
                }

			}
		} break;

        case Event_Type::RESIZE:
        {
            if (event.client_width != 0 && event.client_height != 0)
            {
                editor_state.camera.aspect_ratio = (F32)event.client_width / (F32)event.client_height;
                update_camera(&editor_state.camera);
            }
        } break;

        case Event_Type::MOUSE:
        {
            if (event.pressed && event.button == HE_BUTTON_LEFT)
            {
                ImGuiHoveredFlags hover_flags = ImGuiHoveredFlags_AnyWindow|ImGuiHoveredFlags_AllowWhenBlockedByPopup;
                bool interacting_with_imgui = ImGui::IsWindowHovered(hover_flags) || ImGui::IsAnyItemHovered();

                if (Scene_Hierarchy_Panel::get_selected_node() != -1)
                {
                    interacting_with_imgui |= ImGuizmo::IsOver();
                }

                if (!interacting_with_imgui)
                {
                    Render_Context render_context = get_render_context();
                    Renderer_State *renderer_state = render_context.renderer_state;
                    Frame_Render_Data *render_data = &renderer_state->render_data;
                    Buffer *buffer = renderer_get_buffer(render_data->scene_buffers[renderer_state->current_frame_in_flight_index]);
                    S32 node_index = *(S32*)buffer->data;
                    Editor::reset_selection();
                    if (node_index != -1)
                    {
                        Inspector_Panel::inspect(get_asset_handle_as<Scene>(editor_state.scene_asset), node_index);
                        Scene_Hierarchy_Panel::select(node_index);
                    }
                }
            }
        } break;
	}
}

void hope_app_on_update(Engine *engine, F32 delta_time)
{
	Input *input = &engine->input;

    Camera *camera = &editor_state.camera;
    FPS_Camera_Controller *camera_controller = &editor_state.camera_controller;

    ImGuiHoveredFlags hover_flags = ImGuiHoveredFlags_AnyWindow|ImGuiHoveredFlags_AllowWhenBlockedByPopup;
    bool interacting_with_imgui = ImGui::IsWindowHovered(hover_flags) || ImGui::IsAnyItemHovered();
    
    FPS_Camera_Controller_Input camera_controller_input = {};
    camera_controller_input.can_control = input->button_states[HE_BUTTON_RIGHT] != Input_State::RELEASED && !interacting_with_imgui;
    camera_controller_input.move_fast = input->key_states[HE_KEY_LEFT_SHIFT] != Input_State::RELEASED;
    camera_controller_input.forward = input->key_states[HE_KEY_W] != Input_State::RELEASED;
    camera_controller_input.backward = input->key_states[HE_KEY_S] != Input_State::RELEASED;
    camera_controller_input.left = input->key_states[HE_KEY_A] != Input_State::RELEASED;
    camera_controller_input.right = input->key_states[HE_KEY_D] != Input_State::RELEASED;
    camera_controller_input.up = input->key_states[HE_KEY_E] != Input_State::RELEASED;
    camera_controller_input.down = input->key_states[HE_KEY_Q] != Input_State::RELEASED;
    camera_controller_input.delta_x = -input->mouse_delta_x;
    camera_controller_input.delta_y = -input->mouse_delta_y;

    if (camera_controller_input.can_control)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        engine->lock_cursor = true;
        engine->show_cursor = false;
        control_camera(camera_controller, camera, camera_controller_input, delta_time);
    }
    else
    {
        engine->lock_cursor = false;
        engine->show_cursor = true;
    }

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;

    if (!engine->is_minimized)
    {
        Render_Context render_context = get_render_context();
        
        if (editor_state.show_ui_panels)
        {
            draw_graphics_window();
            
            Assets_Panel::draw();
            
            if (is_asset_handle_valid(editor_state.scene_asset) && is_asset_loaded(editor_state.scene_asset))
            {                
                Scene_Hierarchy_Panel::draw(editor_state.scene_asset.uuid);
                Inspector_Panel::draw();
            }

            ImGui::Begin("Scene");

            if (is_asset_handle_valid(editor_state.scene_asset))
            {
                if (!is_asset_loaded(editor_state.scene_asset))
                {
                    aquire_asset(editor_state.scene_asset);
                }
                else
                {
                    Scene_Handle scene_handle = get_asset_handle_as<Scene>(editor_state.scene_asset);
                    Scene *scene = renderer_get_scene(scene_handle);

                    Skybox *skybox = &scene->skybox;

                    ImGui::Text("Ambient");
                    ImGui::SameLine();
                    ImGui::ColorEdit3("##EditAmbientColor", &skybox->ambient_color.r);

                    select_asset(HE_STRING_LITERAL("Skybox Material"), HE_STRING_LITERAL("material"), (Asset_Handle *)&skybox->skybox_material_asset);
                }
            }

            ImGui::End();
        }

        if (editor_state.show_stats_panel)
        {
            const Frame_Render_Data *rd = &renderer_state->render_data;
            ImGuiIO &io = ImGui::GetIO();
            ImGui::Begin("Stats");
            ImGui::Text("frame time: %f ms", io.DeltaTime * 1000.0f);
            ImGui::Text("FPS: %u", (U32)io.Framerate);
            ImGui::End();
        }

        S32 selected_node_index = Scene_Hierarchy_Panel::get_selected_node();
        Frame_Render_Data *rd = &renderer_state->render_data;
        rd->selected_node_index = selected_node_index;

        begin_rendering(camera);

        if (is_asset_handle_valid(editor_state.scene_asset))
        {
            if (!is_asset_loaded(editor_state.scene_asset))
            {
                aquire_asset(editor_state.scene_asset);
            }
            else
            {
                Scene_Handle scene_handle = get_asset_handle_as<Scene>(editor_state.scene_asset);

                S32 node_index = Scene_Hierarchy_Panel::get_selected_node();
                if (node_index != -1)
                {
                    Scene *scene = renderer_get_scene(scene_handle);
                    Scene_Node *node = get_node(scene, node_index);

                    ImGuiIO &io = ImGui::GetIO();
                    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

                    Transform &t = node->transform;

                    glm::mat4 world = get_world_matrix(t);
                    ImGuizmo::Manipulate(glm::value_ptr(camera->view), glm::value_ptr(camera->projection), editor_state.operation, editor_state.guizmo_mode, glm::value_ptr(world));

                    glm::vec3 position;
                    glm::vec3 rotation;
                    glm::vec3 scale;
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(world), glm::value_ptr(position), glm::value_ptr(rotation), glm::value_ptr(scale));

                    t.position = position;
                    t.rotation = glm::quat(glm::radians(rotation));
                    t.euler_angles = rotation;
                    t.scale = scale;
                }

                render_scene(scene_handle);
            }
        }

        end_rendering();
    }
}

void hope_app_shutdown(Engine *engine)
{
    (void)engine;
    if (is_asset_handle_valid(editor_state.scene_asset))
    {
        Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
        const Asset_Registry_Entry &entry = get_asset_registry_entry(editor_state.scene_asset);
        String scene_path = format_string(scratch_memory.arena, "%.*s/%.*s", HE_EXPAND_STRING(get_asset_path()), HE_EXPAND_STRING(entry.path));
        serialize_scene(get_asset_handle_as<Scene>(editor_state.scene_asset), scene_path);
    }
}

static void draw_graphics_window()
{
    Render_Context render_context = get_render_context();
    Renderer *renderer = render_context.renderer;
    Renderer_State *renderer_state = render_context.renderer_state;

    // ImGui Graphics Settings
    {
        ImGui::Begin("Graphics");

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

            static bool triple_buffering = renderer_state->triple_buffering;
            if (ImGui::Checkbox("##Triple Buffering", &triple_buffering))
            {
                renderer_set_triple_buffering(triple_buffering);
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
        
        ImGui::End();
    }
}

namespace Editor {

void reset_selection()
{
    Scene_Hierarchy_Panel::reset_selection();
    Assets_Panel::reset_selection();
}

}