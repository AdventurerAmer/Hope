#include <core/engine.h>
#include <core/platform.h>
#include <core/job_system.h>
#include <core/file_system.h>

#include <assets/asset_manager.h>

#include <imgui/imgui.h>
#include <ImGui/imgui_internal.h>

#include <filesystem>
namespace fs = std::filesystem;

#include "widgets/inspector_panel.h"
#include "editor_utils.h"

struct Editor_State
{
    Engine *engine;
	Camera camera;
	FPS_Camera_Controller camera_controller;
    Scene_Handle scene_handle;
    fs::path asset_path;
    S32 node_index = -1;
    S32 rename_node_index = -1;
    S32 dragging_node_index = -1;
    S32 selected_node_index = -1;
};

static Editor_State editor_state;

void draw_graphics_window();
void draw_assets_window();
void draw_scene_hierarchy_window();

bool hope_app_init(Engine *engine)
{
    Editor_State *state = &editor_state;
    state->engine = engine;

    editor_state.asset_path = fs::path(get_asset_path().data);

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;
    glm::vec2 viewport = { render_context.renderer_state->back_buffer_width, render_context.renderer_state->back_buffer_height };

    F32 aspect_ratio = viewport.x / viewport.y;
    glm::quat camera_rotation = glm::quat();
    Camera *camera = &editor_state.camera;
	F32 fov = 45.0f;
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

    Asset_Handle opaque_pbr = {};
    {
        opaque_pbr = import_asset(HE_STRING_LITERAL("opaque_pbr.glsl"));
        aquire_asset(opaque_pbr);
    }

    Asset_Handle skybox_shader_asset = {};
    {
        skybox_shader_asset = import_asset(HE_STRING_LITERAL("skybox.glsl"));
        aquire_asset(skybox_shader_asset);
    }

    {
        Asset_Handle asset_asset = import_asset(HE_STRING_LITERAL("Sponza/Sponza.gltf"));
        if (asset_asset.uuid)
        {
            aquire_asset(asset_asset);
            renderer_state->scene_data.model_asset = asset_asset.uuid;
        }
        else
        {
            renderer_state->scene_data.model_asset = 0;
        }
    }

    {
        Asset_Handle asset_handle = import_asset(HE_STRING_LITERAL("skybox/skybox_mat.hamaterial"));
        if (asset_handle.uuid)
        {
            aquire_asset(asset_handle);
            renderer_state->scene_data.skybox_material_asset = asset_handle.uuid;
        }
        else
        {
            renderer_state->scene_data.skybox_material_asset = 0;
        }
    }

    editor_state.scene_handle = renderer_create_scene(1024);
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
				if (event.key == HE_KEY_ESCAPE)
				{
					engine->is_running = false;
				}
				else if (event.key == HE_KEY_F11)
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
                    engine->show_imgui = !engine->show_imgui;
                    engine->show_cursor = !engine->show_cursor;
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
	}
}

void hope_app_on_update(Engine *engine, F32 delta_time)
{
	Input *input = &engine->input;

    Camera *camera = &editor_state.camera;
    FPS_Camera_Controller *camera_controller = &editor_state.camera_controller;

    FPS_Camera_Controller_Input camera_controller_input = {};
    camera_controller_input.can_control = input->button_states[HE_BUTTON_RIGHT] != Input_State::RELEASED && !engine->show_imgui;
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
        engine->lock_cursor = true;
        control_camera(camera_controller, camera, camera_controller_input, delta_time);
    }
    else
    {
        engine->lock_cursor = false;
    }

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;

    if (!engine->is_minimized)
    {
        Render_Context render_context = get_render_context();
        Scene_Data *scene_data = &render_context.renderer_state->scene_data;
        scene_data->view = camera->view;
        scene_data->projection = camera->projection;
        scene_data->eye = camera->position;

        // static bool show_imgui_window = false;
        // ImGui::ShowDemoWindow(&show_imgui_window);

        draw_graphics_window();
        draw_assets_window();
        draw_scene_hierarchy_window();
        imgui_draw_memory_system();
        Inspector_Panel::draw();
    }
}

void hope_app_shutdown(Engine *engine)
{
    (void)engine;
}

static void draw_graphics_window()
{
    Render_Context render_context = get_render_context();
    Renderer *renderer = render_context.renderer;
    Renderer_State *renderer_state = render_context.renderer_state;

    Scene_Data *scene_data = &render_context.renderer_state->scene_data;
    Directional_Light *directional_light = &scene_data->directional_light;
    Point_Light *point_light = &scene_data->point_light;
    Spot_Light *spot_light = &scene_data->spot_light;

    // ImGui Graphics Settings
    {
        ImGui::Begin("Graphics");

        ImGui::Text("Directional Light");
        ImGui::Separator();

        ImGui::Text("Directional Light Direction");
        ImGui::SameLine();

        ImGui::DragFloat3("##Directional Light Direction", &directional_light->direction.x, 0.1f, -1.0f, 1.0f);

        if (glm::length2(directional_light->direction) > 0.0f)
        {
            directional_light->direction = glm::normalize(directional_light->direction);
        }

        ImGui::Text("Directional Light Color");
        ImGui::SameLine();

        ImGui::ColorEdit3("##Directional Light Color", &directional_light->color.r);
        ImGui::DragFloat("##Directional Light Intensity", &directional_light->intensity);

        ImGui::Text("Point Light");
        ImGui::Separator();

        ImGui::Text("Point Light Position");
        ImGui::SameLine();

        ImGui::DragFloat3("##Point Light Position", &point_light->position.x, 0.1f);

        ImGui::Text("Point Light Radius");
        ImGui::SameLine();

        ImGui::DragFloat("##Point Light Radius", &point_light->radius);

        ImGui::Text("Point Light Color");
        ImGui::SameLine();

        ImGui::ColorEdit3("##Point Light Color", &point_light->color.r);

        ImGui::Text("Point Light Intensity");
        ImGui::SameLine();

        ImGui::DragFloat("##Point Light Intensity", &point_light->intensity);

        ImGui::Text("Spot Light");
        ImGui::Separator();

        ImGui::Text("Spot Light Position");
        ImGui::SameLine();

        ImGui::DragFloat3("##Spot Light Position", &spot_light->position.x, 0.1f);

        ImGui::Text("Spot Light Radius");
        ImGui::SameLine();

        ImGui::DragFloat("##Spot Light Radius", &spot_light->radius);

        ImGui::Text("Spot Light Direction");
        ImGui::SameLine();

        ImGui::DragFloat3("##Spot Light Direction", &spot_light->direction.x, 0.1f, -1.0f, 1.0f);

        if (glm::length2(spot_light->direction) > 0.0f)
        {
            spot_light->direction = glm::normalize(spot_light->direction);
        }

        ImGui::Text("Spot Light Outer Angle");
        ImGui::SameLine();

        ImGui::DragFloat("##Spot Light Outer Angle", &spot_light->outer_angle, 1.0f, 0.0f, 360.0f);

        ImGui::Text("Spot Light Inner Angle");
        ImGui::SameLine();

        ImGui::DragFloat("##Spot Light Inner Angle", &spot_light->inner_angle, 1.0f, 0.0f, 360.0f);

        ImGui::Text("Spot Light Color");
        ImGui::SameLine();

        ImGui::ColorEdit3("##Spot Light Color", &spot_light->color.r);

        ImGui::Text("Spot Light Intensity");
        ImGui::SameLine();

        ImGui::DragFloat("##Spot Light Intensity", &spot_light->intensity);

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
}

static void create_skybox_asset_modal(bool open)
{
    if (open)
    {
        ImGui::OpenPopup("Create Skybox Popup Model");
    }

    if (ImGui::BeginPopupModal("Create Skybox Popup Model", NULL, ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse))
    {
        struct Skybox_Texture_Face
        {
            String text;
            Asset_Handle asset_handle;
        };

        static Skybox_Texture_Face faces[(U32)Skybox_Face::COUNT]
        {
            {
                .text = HE_STRING_LITERAL("Select Right Texture"),
                .asset_handle = {}
            },
            {
                .text = HE_STRING_LITERAL("Select Left Texture"),
                .asset_handle = {}
            },
            {
                .text = HE_STRING_LITERAL("Select Top Texture"),
                .asset_handle = {}
            },
            {
                .text = HE_STRING_LITERAL("Select Bottom Texture"),
                .asset_handle = {}
            },
            {
                .text = HE_STRING_LITERAL("Select Front Texture"),
                .asset_handle = {}
            },
            {
                .text = HE_STRING_LITERAL("Select Back Texture"),
                .asset_handle = {}
            }
        };

        bool show_ok_button = true;

        for (U32 i = 0; i < (U32)Skybox_Face::COUNT; i++)
        {
            show_ok_button &= select_asset(faces[i].text, HE_STRING_LITERAL("texture"), &faces[i].asset_handle);
        }

        auto reset_selection = [&]()
        {
            for (U32 i = 0; i < (U32)Skybox_Face::COUNT; i++)
            {
                if (is_asset_handle_valid(faces[i].asset_handle))
                {
                    release_asset(faces[i].asset_handle);
                }

                faces[i].asset_handle = {};
            }
        };

        if (show_ok_button)
        {
            if (ImGui::Button("Ok", ImVec2(120, 0)))
            {
                String extensions[] =
                {
                    HE_STRING_LITERAL("haskybox")
                };

                Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

                String title = HE_STRING_LITERAL("Save Skybox Asset");
                String filter = HE_STRING_LITERAL("Skybox (.haskybox)");
                String absolute_path = save_file_dialog(title, filter, to_array_view(extensions));
                HE_DEFER { deallocate(get_general_purpose_allocator(), (void *)absolute_path.data); };
                if (absolute_path.count)
                {
                    String path = absolute_path;

                    String ext = get_extension(absolute_path);
                    if (ext != extensions[0])
                    {
                        path = format_string(scratch_memory.arena, "%.*s.haskybox", HE_EXPAND_STRING(absolute_path));
                    }

                    String_Builder builder = {};
                    begin_string_builder(&builder, scratch_memory.arena);
                    append(&builder, "version 1\n");
                    append(&builder, "right_texture_uuid %llu\n", faces[(U32)Skybox_Face::RIGHT].asset_handle.uuid);
                    append(&builder, "left_texture_uuid %llu\n", faces[(U32)Skybox_Face::LEFT].asset_handle.uuid);
                    append(&builder, "top_texture_uuid %llu\n", faces[(U32)Skybox_Face::TOP].asset_handle.uuid);
                    append(&builder, "bottom_texture_uuid %llu\n", faces[(U32)Skybox_Face::BOTTOM].asset_handle.uuid);
                    append(&builder, "front_texture_uuid %llu\n", faces[(U32)Skybox_Face::FRONT].asset_handle.uuid);
                    append(&builder, "back_texture_uuid %llu\n", faces[(U32)Skybox_Face::BACK].asset_handle.uuid);
                    String contents = end_string_builder(&builder);
                    bool written = write_entire_file(path, (void*)contents.data, contents.count);
                    if (written)
                    {
                        String asset_path = get_asset_path();
                        String import_path = sub_string(path, asset_path.count + 1);
                        import_asset(import_path);
                    }
                }

                reset_selection();
                ImGui::CloseCurrentPopup();
            }

            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
        }

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            reset_selection();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

const char* cull_mode_to_string(Cull_Mode mode)
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
            HE_ASSERT("unsupported cull mode");
        } break;
    }

    return "";
}

const char* front_face_to_string(Front_Face front_face)
{
    switch (front_face)
    {
        case Front_Face::CLOCKWISE:
        {
            return "clockwise";
        } break;

        case Front_Face::COUNTER_CLOCKWISE:
        {
            return "counterclockwise";
        } break;

        default:
        {
            HE_ASSERT("unsupported front face");
        } break;
    }

    return "";
}

struct Create_Material_Asset_Data
{
    Asset_Handle shader_asset = {};
    U32 property_count;
    Material_Property *properties = nullptr;
    U32 render_pass_index = 0;
    Cull_Mode cull_mode = Cull_Mode::BACK;
    Front_Face front_face = Front_Face::COUNTER_CLOCKWISE;
    bool depth_testing = true;
};

static void create_material_asset_modal(bool open)
{
    static Create_Material_Asset_Data asset_data = {};

    if (open)
    {
        ImGui::OpenPopup("Create Material Popup Model");
    }

    if (ImGui::BeginPopupModal("Create Material Popup Model", NULL, ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse))
    {
        auto reset_selection = [&]()
        {
            if (is_asset_handle_valid(asset_data.shader_asset))
            {
                release_asset(asset_data.shader_asset);
            }

            asset_data.shader_asset = {};

            if (asset_data.properties)
            {
                deallocate(get_general_purpose_allocator(), asset_data.properties);
                asset_data.properties = nullptr;
                asset_data.property_count = 0;
            }

            asset_data.render_pass_index = 0;
            asset_data.cull_mode = Cull_Mode::BACK;
            asset_data.front_face = Front_Face::COUNTER_CLOCKWISE;
            asset_data.depth_testing = true;
        };

        String extensions[] =
        {
            HE_STRING_LITERAL("glsl")
        };

        if (ImGui::Button("Select Shader"))
        {
            String title = HE_STRING_LITERAL("Select Shader Asset");
            String filter = HE_STRING_LITERAL("Shader (.glsl)");
            String absolute_path = open_file_dialog(title, filter, to_array_view(extensions));

            if (absolute_path.count)
            {
                HE_DEFER { deallocate(get_general_purpose_allocator(), (void *)absolute_path.data); };

                String asset_path = get_asset_path();
                String path = sub_string(absolute_path, asset_path.count + 1);

                if (path.count)
                {
                    reset_selection();
                    asset_data.shader_asset = import_asset(path);
                    if (is_asset_handle_valid(asset_data.shader_asset))
                    {
                        aquire_asset(asset_data.shader_asset);
                    }
                }
            }
        }

        String label = HE_STRING_LITERAL("None");

        if (asset_data.shader_asset.uuid != 0)
        {
            if (is_asset_handle_valid(asset_data.shader_asset))
            {
                const Asset_Registry_Entry &entry = get_asset_registry_entry(asset_data.shader_asset);
                label = entry.path;
            }
            else
            {
                label = HE_STRING_LITERAL("Invalid");
            }
        }

        ImGui::SameLine();
        ImGui::Text(label.data);

        static constexpr const char *render_passes[] = { "opaque", "transparent" };

        ImGui::Text("Render Pass");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##Render Pass", render_passes[asset_data.render_pass_index]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(render_passes); i++)
            {
                bool is_selected = i == asset_data.render_pass_index;
                if (ImGui::Selectable(render_passes[i], is_selected))
                {
                    asset_data.render_pass_index = i;
                }

                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        static constexpr const char *cull_modes[] = { "none", "front", "back" };

        ImGui::Text("Cull Mode");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##Cull Mode", cull_modes[(U32)asset_data.cull_mode]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(cull_modes); i++)
            {
                bool is_selected = i == (U32)asset_data.cull_mode;
                if (ImGui::Selectable(cull_modes[i], is_selected))
                {
                    asset_data.cull_mode = (Cull_Mode)i;
                }

                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        static constexpr const char *front_faces[] = { "clockwise", "counterclockwise" };

        ImGui::Text("Front Face");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##Front Face", front_faces[(U32)asset_data.front_face]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(front_faces); i++)
            {
                bool is_selected = i == (U32)asset_data.front_face;
                if (ImGui::Selectable(front_faces[i], is_selected))
                {
                    asset_data.front_face = (Front_Face)i;
                }

                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Text("Depth Testing");
        ImGui::SameLine();

        ImGui::Checkbox("##Depth Testing", &asset_data.depth_testing);

        if (is_asset_handle_valid(asset_data.shader_asset))
        {
            Shader_Handle shader_handle = get_asset_handle_as<Shader>(asset_data.shader_asset);
            Shader *shader = renderer_get_shader(shader_handle);
            Shader_Struct *material_struct = renderer_find_shader_struct(shader_handle, HE_STRING_LITERAL("Material"));

            if (material_struct)
            {
                ImGui::Text("Properties");

                if (!asset_data.properties)
                {
                    asset_data.properties = HE_ALLOCATE_ARRAY(get_general_purpose_allocator(), Material_Property, material_struct->member_count);
                    asset_data.property_count = material_struct->member_count;
                }

                for (U32 i = 0; i < material_struct->member_count; i++)
                {
                    ImGui::PushID(i);

                    Material_Property *property = &asset_data.properties[i];

                    Shader_Struct_Member *member = &material_struct->members[i];
                    property->name = member->name;
                    property->data_type = member->data_type;

                    bool is_texture_asset = ends_with(member->name, HE_STRING_LITERAL("texture"));
                    bool is_skybox_asset = ends_with(member->name, HE_STRING_LITERAL("cubemap"));
                    bool is_color = ends_with(member->name, HE_STRING_LITERAL("color"));

                    ImGui::Text(member->name.data);
                    ImGui::SameLine();

                    switch (member->data_type)
                    {
                        case Shader_Data_Type::U32:
                        {
                            if (is_texture_asset)
                            {
                                select_asset(HE_STRING_LITERAL("Select Texture"), HE_STRING_LITERAL("texture"), (Asset_Handle *)&property->data.u64);
                            }
                            else if (is_skybox_asset)
                            {
                                select_asset(HE_STRING_LITERAL("Select Skybox"), HE_STRING_LITERAL("skybox"), (Asset_Handle *)&property->data.u64);
                            }
                            else
                            {
                                ImGui::DragInt("##Property", &property->data.s32);
                            }
                        } break;

                        case Shader_Data_Type::F32:
                        {
                            ImGui::DragFloat("##Property", &property->data.f32);
                        } break;

                        case Shader_Data_Type::VECTOR2F:
                        {
                            ImGui::DragFloat2("##Property", (F32 *)&property->data.v2);
                        } break;

                        case Shader_Data_Type::VECTOR3F:
                        {
                            if (is_color)
                            {
                                ImGui::ColorEdit3("##Property", (F32*)&property->data.v3);
                            }
                            else
                            {
                                ImGui::DragFloat3("##Property", (F32 *)&property->data.v3);
                            }
                        } break;

                        case Shader_Data_Type::VECTOR4F:
                        {
                            if (property->is_color)
                            {
                                ImGui::ColorEdit4("##Property", (F32*)&property->data.v4);
                            }
                            else
                            {
                                ImGui::DragFloat4("##Property", (F32 *)&property->data.v4);
                            }
                        } break;
                    }

                    ImGui::PopID();
                }
            }
        }

        bool show_ok_button = true;
        if (show_ok_button)
        {
            if (ImGui::Button("Ok", ImVec2(120, 0)))
            {
                String extensions[] =
                {
                    HE_STRING_LITERAL("hamaterial")
                };

                Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

                String title = HE_STRING_LITERAL("Save Material Asset");
                String filter = HE_STRING_LITERAL("Material (.hamaterial)");
                String absolute_path = save_file_dialog(title, filter, to_array_view(extensions));
                HE_DEFER { deallocate(get_general_purpose_allocator(), (void *)absolute_path.data); };
                if (absolute_path.count)
                {
                    String path = absolute_path;

                    String ext = get_extension(absolute_path);
                    if (ext != extensions[0])
                    {
                        path = format_string(scratch_memory.arena, "%.*s.hamaterial", HE_EXPAND_STRING(absolute_path));
                    }

                    String_Builder builder = {};
                    begin_string_builder(&builder, scratch_memory.arena);
                    append(&builder, "version 1\n");
                    append(&builder, "shader %llu\n", asset_data.shader_asset.uuid);
                    append(&builder, "render_pass %s\n", render_passes[asset_data.render_pass_index]);
                    append(&builder, "cull_mode %s\n", cull_mode_to_string(asset_data.cull_mode));
                    append(&builder, "front_face %s\n", front_face_to_string(asset_data.front_face));
                    append(&builder, "depth_testing %s\n", asset_data.depth_testing ? "enabled" : "disabled");
                    append(&builder, "property_count %u\n", asset_data.property_count);
                    for (U32 i = 0; i < asset_data.property_count; i++)
                    {
                        Material_Property *property = &asset_data.properties[i];
                        bool is_texture_asset = ends_with(property->name, HE_STRING_LITERAL("texture")) || ends_with(property->name, HE_STRING_LITERAL("cubemap"));
                        bool is_color = ends_with(property->name, HE_STRING_LITERAL("color"));

                        // todo(amer): shader_data_type to string
                        append(&builder, "%.*s %u ", HE_EXPAND_STRING(property->name), (U32)property->data_type);
                        switch (property->data_type)
                        {
                            case Shader_Data_Type::U32:
                            {
                                append(&builder, "%llu\n", is_texture_asset ? property->data.u64 : property->data.u32);
                            } break;

                            case Shader_Data_Type::F32:
                            {
                                append(&builder, "%f\n", property->data.f32);
                            } break;

                            case Shader_Data_Type::VECTOR2F:
                            {
                                append(&builder, "%f %f\n", property->data.v2[0], property->data.v2[1]);
                            } break;

                            case Shader_Data_Type::VECTOR3F:
                            {
                                append(&builder, "%f %f %f\n", property->data.v3.x, property->data.v3.y, property->data.v3.z);
                            } break;

                            case Shader_Data_Type::VECTOR4F:
                            {
                                append(&builder, "%f %f %f %f\n", property->data.v4.x, property->data.v4.y, property->data.v4.z, property->data.v4.w);
                            } break;
                        }
                    }
                    String contents = end_string_builder(&builder);
                    bool success = write_entire_file(path, (void *)contents.data, contents.count);
                    if (success)
                    {
                        path = sub_string(path, get_asset_path().count + 1);
                        Asset_Handle asset_handle = import_asset(path);
                        set_parent(asset_handle, asset_data.shader_asset);
                    }
                }
                reset_selection();
                ImGui::CloseCurrentPopup();
            }

            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
        }

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            reset_selection();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static void draw_assets_window()
{
    static fs::path current_path;
    static fs::path selected_path;

    if (current_path.empty())
    {
        current_path = editor_state.asset_path;
    }

    ImGui::Begin("Assets");

    ImGui::BeginDisabled(current_path == editor_state.asset_path);
    if (ImGui::Button("Back"))
    {
        current_path = current_path.parent_path();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Text("Path %s", current_path.string().c_str());

    if (ImGui::BeginListBox("##Begin List Box", ImGui::GetContentRegionAvail()))
    {
        for (const auto &it : fs::directory_iterator(current_path))
        {
            const fs::path &path = it.path();
            const fs::path &relative = path.lexically_relative(current_path);

            bool is_asset_file = false;

            auto asset_path_string = path.lexically_relative(editor_state.asset_path).string();

            String asset_path = HE_STRING(asset_path_string.c_str());

            if (it.is_regular_file())
            {
                sanitize_path(asset_path);
                String extension = get_extension(asset_path);
                if (get_asset_info_from_extension(extension))
                {
                    is_asset_file = true;
                }
            }

            if (!is_asset_file && !it.is_directory())
            {
                continue;
            }

            Asset_Handle asset_handle = get_asset_handle(asset_path);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth|ImGuiTreeNodeFlags_FramePadding;

            if (selected_path == path)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            Array_View< U64 > embeded_assets = get_embeded_assets(asset_handle);
            if (embeded_assets.count == 0)
            {
                flags |= ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_DefaultOpen;
            }

            ImGui::PushID(asset_path_string.c_str());

            bool is_open = ImGui::TreeNodeEx(relative.string().c_str(), flags);
            if (ImGui::IsItemClicked())
            {
                if (it.is_directory())
                {
                    current_path = path;
                }
                else
                {
                    selected_path = path;
                    if (is_asset_file)
                    {
                        if (asset_handle.uuid == 0)
                        {
                            asset_handle = import_asset(asset_path);
                        }
                        Inspector_Panel::inspect(asset_handle);
                    }
                }
            }

            if (is_asset_file)
            {
                ImGuiDragDropFlags src_flags = 0;
                src_flags |= ImGuiDragDropFlags_SourceNoDisableHover;
                src_flags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers;
                if (ImGui::BeginDragDropSource(src_flags))
                {
                    if (asset_handle.uuid == 0)
                    {
                        asset_handle = import_asset(asset_path);
                    }
                    ImGui::SetDragDropPayload("DND_ASSET", &asset_handle, sizeof(Asset_Handle));
                    ImGui::EndDragDropSource();
                }
            }

            if (is_open)
            {
                if (is_asset_file && asset_handle.uuid == 0)
                {
                    asset_handle = import_asset(asset_path);
                }

                for (U32 i = 0; i < embeded_assets.count; i++)
                {
                    Asset_Handle embeded_asset = { .uuid = embeded_assets[i] };
                    const auto &entry = get_asset_registry_entry(embeded_asset);
                    String name = get_name(entry.path);
                    auto path = fs::path((const char *)entry.path.data);
                    bool is_selected = selected_path == path;
                    ImGui::Selectable(name.data, &is_selected);
                    if (ImGui::IsItemClicked())
                    {
                        selected_path = path;
                        Inspector_Panel::inspect(embeded_asset);
                    }
                    ImGuiDragDropFlags src_flags = 0;
                    src_flags |= ImGuiDragDropFlags_SourceNoDisableHover;
                    src_flags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers;
                    if (ImGui::BeginDragDropSource(src_flags))
                    {
                        ImGui::SetDragDropPayload("DND_ASSET", &embeded_asset, sizeof(Asset_Handle));
                        ImGui::EndDragDropSource();
                    }
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
        }
        
        ImGui::EndListBox();
    }
    
    bool open_material_asset_modal = false;
    bool open_create_asset_modal = false;

    if (ImGui::BeginPopupContextWindow())
    {
        if (ImGui::MenuItem("Create Material"))
        {
            open_material_asset_modal = true;
        }

        if (ImGui::MenuItem("Create Skybox"))
        {
            open_create_asset_modal = true;
        }

        ImGui::EndPopup();
    }

    create_material_asset_modal(open_material_asset_modal);
    create_skybox_asset_modal(open_create_asset_modal);

    ImGui::End();
}

enum class Add_Scene_Node_Operation
{
    FIRST,
    LAST,
    AFTER,
};

static void add_model_to_scene(Scene *scene, Scene_Node *node, Asset_Handle asset_handle, Add_Scene_Node_Operation op)
{
    const Asset_Info *info = get_asset_info(asset_handle);
    if (info && info->name == HE_STRING_LITERAL("model"))
    {
        if (!is_asset_loaded(asset_handle))
        {
            Job_Handle job_handle = aquire_asset(asset_handle);
            // todo(amer): should we make a progress bar here ?
            wait_for_job_to_finish(job_handle);
        }

        Model *model = get_asset_as<Model>(asset_handle);
        Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

        Scene_Node *sub_scene_parent = node;

        if (model->node_count != 1)
        {
            U32 sub_scene_parent_index = allocate_node(scene, model->name);
            sub_scene_parent = get_node(scene, sub_scene_parent_index);
            switch (op)
            {
                case Add_Scene_Node_Operation::FIRST:
                {
                    add_child_first(scene, node, sub_scene_parent);
                } break;

                case Add_Scene_Node_Operation::LAST:
                {
                    add_child_last(scene, node, sub_scene_parent);
                } break;

                case Add_Scene_Node_Operation::AFTER:
                {
                    add_child_after(scene, node, sub_scene_parent);
                } break;
            }
        }

        U32 *node_indices = HE_ALLOCATE_ARRAY(scratch_memory.arena, U32, model->node_count);

        for (U32 i = 0; i < model->node_count; i++)
        {
            Scene_Node *model_node = &model->nodes[i];

            node_indices[i] = allocate_node(scene, model_node->name);
            Scene_Node *current_scene_node = get_node(scene, node_indices[i]);

            current_scene_node->has_mesh = model_node->has_mesh;
            current_scene_node->mesh = model_node->mesh;

            current_scene_node->has_light = model_node->has_light;
            current_scene_node->light = model_node->light;

            if (model_node->parent_index == -1)
            {
                if (model->node_count == 1)
                {
                    switch (op)
                    {
                        case Add_Scene_Node_Operation::FIRST:
                        {
                            add_child_first(scene, sub_scene_parent, current_scene_node);
                        } break;

                        case Add_Scene_Node_Operation::LAST:
                        {
                            add_child_last(scene, sub_scene_parent, current_scene_node);
                        } break;

                        case Add_Scene_Node_Operation::AFTER:
                        {
                            add_child_after(scene, sub_scene_parent, current_scene_node);
                        } break;
                    }
                }
                else
                {
                    add_child_last(scene, sub_scene_parent, current_scene_node);
                }
            }
            else
            {
                add_child_last(scene, get_node(scene, node_indices[model_node->parent_index]), current_scene_node);
            }
        }
    }
}

static char buffer[128];

static void draw_scene_node(Scene *scene, Scene_Node *node)
{
    ImGui::PushID(node);
    U32 node_index = (S32)index_of(&scene->nodes, node);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth|ImGuiTreeNodeFlags_FramePadding|ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_OpenOnDoubleClick|ImGuiTreeNodeFlags_OpenOnArrow;

    bool is_leaf = node->first_child_index == -1;
    if (is_leaf)
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    if (node_index == editor_state.selected_node_index)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const char *label = node->name.data;

    bool should_edit_node = node_index == editor_state.rename_node_index;
    if (should_edit_node)
    {
        label = "##";
        flags |= ImGuiTreeNodeFlags_AllowOverlap;
    }

    bool is_open = ImGui::TreeNodeEx(label, flags);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        editor_state.selected_node_index = node_index;
        Inspector_Panel::inspect(get_node(scene, node_index));
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        editor_state.node_index = node_index;
    }

    ImGuiDragDropFlags src_flags = 0;
    src_flags |= ImGuiDragDropFlags_SourceNoDisableHover;
    src_flags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers;

    if (ImGui::BeginDragDropSource(src_flags))
    {
        editor_state.dragging_node_index = node_index;
        ImGui::SetDragDropPayload("DND_SCENE_NODE", &node_index, sizeof(U32));
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_SCENE_NODE"))
        {
            U32 child_node_index = *(const U32*)payload->Data;
            Scene_Node *child = get_node(scene, child_node_index);
            remove_child(scene, get_node(scene, child->parent_index), child);
            add_child_last(scene, node, child);
        }

        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ASSET"))
        {
            Asset_Handle asset_handle = *(const Asset_Handle *)payload->Data;
            add_model_to_scene(scene, node, asset_handle, Add_Scene_Node_Operation::LAST);
        }

        ImGui::EndDragDropTarget();
    }

    if (should_edit_node)
    {
        ImGui::SameLine();

        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##EditNodeTextInput", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemDeactivated())
            {
                editor_state.rename_node_index = -1;
                String new_name = HE_STRING(buffer);
                if (node->name.data && new_name.count)
                {
                    deallocate(get_general_purpose_allocator(), (void *)node->name.data);
                    node->name = copy_string(new_name, to_allocator(get_general_purpose_allocator()));
                }
            }
        }
    }

    if (is_open)
    {
        bool is_dragging = ImGui::IsDragDropActive() &&
        (strcmp(ImGui::GetDragDropPayload()->DataType, "DND_SCENE_NODE") == 0 || strcmp(ImGui::GetDragDropPayload()->DataType, "DND_ASSET") == 0);

        if (!is_leaf && is_dragging && editor_state.dragging_node_index != (S32)node_index && node->first_child_index != editor_state.dragging_node_index)
        {
            ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAvailWidth|ImGuiSelectableFlags_NoPadWithHalfSpacing;
            ImGui::Selectable("##DragFirstChild", true, flags, ImVec2(0.0f, 4.0f));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_SCENE_NODE"))
                {
                    U32 child_node_index = *(const U32*)payload->Data;
                    Scene_Node *child = get_node(scene, child_node_index);
                    remove_child(scene, get_node(scene, child->parent_index), child);
                    add_child_first(scene, node, child);
                }

                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ASSET"))
                {
                    Asset_Handle asset_handle = *(const Asset_Handle *)payload->Data;
                    add_model_to_scene(scene, node, asset_handle, Add_Scene_Node_Operation::FIRST);
                }

                ImGui::EndDragDropTarget();
            }   
        }

        for (S32 node_index = node->first_child_index; node_index != -1; node_index = scene->nodes[node_index].next_sibling_index)
        {
            Scene_Node *child = &scene->nodes[node_index];
            draw_scene_node(scene, child);
        }

        ImGui::TreePop();
    }

    bool is_dragging = ImGui::IsDragDropActive() && (strcmp(ImGui::GetDragDropPayload()->DataType, "DND_SCENE_NODE") == 0 || strcmp(ImGui::GetDragDropPayload()->DataType, "DND_ASSET") == 0);

    if (is_dragging && node_index != 0 && editor_state.dragging_node_index != (S32)node_index && node->next_sibling_index != editor_state.dragging_node_index)
    {
        ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAvailWidth|ImGuiSelectableFlags_NoPadWithHalfSpacing;
        ImGui::Selectable("##DragAfterNode", true, flags, ImVec2(0.0f, 4.0f));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_SCENE_NODE"))
            {
                U32 child_node_index = *(const U32*)payload->Data;
                Scene_Node *child = get_node(scene, child_node_index);
                remove_child(scene, get_node(scene, child->parent_index), child);
                add_child_after(scene, node, child);
            }

            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ASSET"))
            {
                Asset_Handle asset_handle = *(const Asset_Handle *)payload->Data;
                add_model_to_scene(scene, node, asset_handle, Add_Scene_Node_Operation::AFTER);
            }

            ImGui::EndDragDropTarget();
        }
    }

    ImGui::PopID();
}

static void draw_scene_hierarchy_window()
{
    ImGui::Begin("Hierarchy");

    Scene *scene = renderer_get_scene(editor_state.scene_handle);

    Scene_Node *root = get_root_node(scene);
    draw_scene_node(scene, root);

    static bool is_context_window_open = false;

    if (ImGui::BeginPopupContextWindow())
    {
        is_context_window_open = true;

        const char *label = "Create Child Node";
        if (editor_state.node_index == -1)
        {
            label = "Create Node";
        }

        if (ImGui::MenuItem(label))
        {
            U32 node_index = allocate_node(scene, HE_STRING_LITERAL("Node"));
            Scene_Node *parent = editor_state.node_index == -1 ? get_root_node(scene) : get_node(scene, editor_state.node_index);
            Scene_Node *node = get_node(scene, node_index);
            add_child_last(scene, parent, node);
            memcpy(buffer, node->name.data, node->name.count);
            editor_state.rename_node_index = node_index;
        }

        if (editor_state.node_index != -1)
        {
            Scene_Node *node = &scene->nodes[editor_state.node_index];

            if (ImGui::MenuItem("Rename"))
            {
                memset(buffer, 0, sizeof(buffer));
                memcpy(buffer, node->name.data, node->name.count);
                editor_state.rename_node_index = editor_state.node_index;
            }

            if (ImGui::MenuItem("Delete"))
            {
                remove_node(scene, node);
            }
        }

        ImGui::EndPopup();
    }
    else
    {
        if (is_context_window_open)
        {
            editor_state.node_index = -1;
            is_context_window_open = false;
        }
    }

    ImGui::End();
}