#include <core/engine.h>
#include <core/platform.h>
#include <core/job_system.h>
#include <resources/resource_system.h>

#include <imgui/imgui.h>

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN 1
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h> // todo(amer): temprary

enum class Selection_Kind
{
    NONE,
    SCENE_NODE,
    ASSET,
};

struct Selection_Context
{
    Selection_Kind kind;

    union
    {
        Scene_Node *node;
        U64 asset_uuid;
    };
};

struct Editor_State
{
    Engine *engine;
	Camera camera;
	FPS_Camera_Controller camera_controller;
    Selection_Context selection;
};

static Editor_State editor_state;

Job_Result create_skybox_material_job(const Job_Parameters &params);
void draw_graphics_window();
void draw_node(Scene_Node *node);
void draw_tree(Scene_Node *node);
void draw_resource_system();
void draw_asset(Asset *asset);

bool hope_app_init(Engine *engine)
{
    Editor_State *state = &editor_state;
    state->engine = engine;
    Selection_Context *selection_context = &state->selection;
    selection_context->kind = Selection_Kind::NONE;

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

    aquire_resource("Sponza/Sponza.hres");
    // aquire_resource("FlightHelmet/FlightHelmet.hres");
    // aquire_resource("Corset/Corset.hres");

    // skybox
    {
        renderer_state->scene_data.skybox_material_handle = renderer_state->default_material;
        Resource_Ref skybox_shader_ref = aquire_resource("skybox.hres");
        Resource *skybox_shader = get_resource(skybox_shader_ref);

        Job_Data job_data =
        {
            .proc = &create_skybox_material_job
        };

        Resource *right = get_resource(find_resource("skybox/right.hres"));
        Resource *left = get_resource(find_resource("skybox/left.hres"));
        Resource *top = get_resource(find_resource("skybox/top.hres"));
        Resource *bottom = get_resource(find_resource("skybox/bottom.hres"));
        Resource *front = get_resource(find_resource("skybox/front.hres"));
        Resource *back = get_resource(find_resource("skybox/back.hres"));

        Job_Handle wait_for_jobs[] =
        {
            skybox_shader->job_handle,
            right->job_handle,
            left->job_handle,
            top->job_handle,
            bottom->job_handle,
            front->job_handle,
            back->job_handle
        };
        execute_job(job_data, to_array_view(wait_for_jobs));
    }

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

        static bool show_imgui_window = false;
        ImGui::ShowDemoWindow(&show_imgui_window);

        draw_graphics_window();

        // Scene
        {
            ImGui::Begin("Scene");

            for (Scene_Node *node = renderer_state->root_scene_node.first_child; node; node = node->next_sibling)
            {
                draw_tree(node);
            }

            ImGui::End();
        }

        // Inspector
        {
            ImGui::Begin("Inspector");

            switch (editor_state.selection.kind)
            {
                case Selection_Kind::SCENE_NODE:
                {
                    if (editor_state.selection.node)
                    {
                        draw_node(editor_state.selection.node);
                    }
                } break;

                case Selection_Kind::ASSET:
                {
                    Asset *asset = get_asset(editor_state.selection.asset_uuid);
                    if (asset)
                    {
                        draw_asset(asset);
                    }
                } break;
            }

            ImGui::End();
        }

        // Resource System
        {
            draw_resource_system();
        }

        // Memory System
        {
            imgui_draw_memory_system();
        }
    }
}

void hope_app_shutdown(Engine *engine)
{
    (void)engine;
}

static Job_Result create_skybox_material_job(const Job_Parameters &params)
{
    Resource_Ref skybox_shader_ref = find_resource("skybox.hres"); // todo(amer): @Hardcoding
    Resource *resource = get_resource(skybox_shader_ref);
    if (resource->state != Resource_State::LOADED)
    {
        HE_LOG(Rendering, Fetal, "failed to create skybox material shader failed to load...\n");
        return Job_Result::FAILED;
    }

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;
    Shader_Handle skybox_shader = get_resource_handle_as<Shader>(skybox_shader_ref);

    Resource_Ref texute_refs[] =
    {
        find_resource("skybox/right.hres"),
        find_resource("skybox/left.hres"),
        find_resource("skybox/top.hres"),
        find_resource("skybox/bottom.hres"),
        find_resource("skybox/front.hres"),
        find_resource("skybox/back.hres"),
    };

    void *data_array[6] = {};

    U32 width = 0;
    U32 height = 0;
    Texture_Format format = Texture_Format::R8G8B8A8_UNORM;

    for (U32 i = 0; i < HE_ARRAYCOUNT(texute_refs); i++)
    {
        Resource *texture_resoruce = get_resource(texute_refs[i]);
        Open_File_Result open_file_result = platform_open_file(texture_resoruce->absolute_path.data, OpenFileFlag_Read);
        if (!open_file_result.success)
        {
            return Job_Result::FAILED;
        }

        HE_DEFER
        {
            platform_close_file(&open_file_result);
        };

        Texture_Resource_Info info;
        platform_read_data_from_file(&open_file_result, sizeof(Resource_Header), &info, sizeof(Texture_Resource_Info));


        width = info.width;
        height = info.height;
        format = info.format;

        U64 data_size = sizeof(U32) * info.width * info.height;
        U32 *data = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, U32, info.width * info.height);
        bool success = platform_read_data_from_file(&open_file_result, info.data_offset, data, data_size);

        data_array[i] = data;
    }

    Texture_Descriptor cubmap_texture_descriptor =
    {
        .width = width,
        .height = height,
        .format = format,
        .layer_count = HE_ARRAYCOUNT(texute_refs),
        .data_array = to_array_view(data_array),
        .mipmapping = true,
        .is_cubemap = true
    };
    renderer_state->scene_data.skybox = renderer_create_texture(cubmap_texture_descriptor);

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
        .shader = skybox_shader,
        .render_pass = get_render_pass(&renderer_state->render_graph, "opaque"),
    };
    Pipeline_State_Handle skybox_pipeline = renderer_create_pipeline_state(skybox_pipeline_state_descriptor);

    Material_Descriptor skybox_material_descriptor =
    {
        .pipeline_state_handle = skybox_pipeline,
    };

    renderer_state->scene_data.skybox_material_handle = renderer_create_material(skybox_material_descriptor);
    set_property(renderer_state->scene_data.skybox_material_handle, "skybox_texture_index", { .u32 = (U32)renderer_state->scene_data.skybox.index });
    set_property(renderer_state->scene_data.skybox_material_handle, "sky_color", { .v3 = { 1.0f, 1.0f, 1.0f } });

    return Job_Result::SUCCEEDED;
}

static void draw_node(Scene_Node *node)
{
    ImGui::Text("Node: %.*s", HE_EXPAND_STRING(node->name));
    ImGui::Separator();

    ImGui::Text("");
    ImGui::Text("Transfom");
    ImGui::Separator();

    Transform &transform = node->local_transform;

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

    if (node->static_mesh_uuid != HE_MAX_U64)
    {
        Resource_Ref ref = { node->static_mesh_uuid };
        Resource *static_mesh_resource = get_resource(ref);

        Static_Mesh_Handle static_mesh_handle = get_resource_handle_as<Static_Mesh>(ref);
        Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

        ImGui::Text("");
        ImGui::Text("Static Mesh: %.*s (%#x)", HE_EXPAND_STRING(get_name(static_mesh_resource->relative_path)), node->static_mesh_uuid);
        ImGui::Separator();

        ImGui::Text("vertex count: %llu", static_mesh->vertex_count);
        ImGui::Text("index count: %llu", static_mesh->index_count);

        U32 sub_mesh_index = 0;
        for (Sub_Mesh &sub_mesh : static_mesh->sub_meshes)
        {
            ImGui::Text("");
            ImGui::Text("Sub Mesh: %d", sub_mesh_index++);
            ImGui::Text("vertex count: %llu", sub_mesh.vertex_count);
            ImGui::Text("index count: %llu", sub_mesh.index_count);

            Resource_Ref material_ref = { sub_mesh.material_uuid };
            Resource *material_resource = get_resource(material_ref);
            Material_Handle material_handle = get_resource_handle_as<Material>(material_ref);
            Material *material = renderer_get_material(material_handle);

            ImGui::Text("");
            ImGui::Text("Material: %.*s (%#x)", HE_EXPAND_STRING(get_name(material_resource->relative_path)), sub_mesh.material_uuid);

            for (U32 property_index = 0; property_index < material->properties.count; property_index++)
            {
                ImGui::PushID(property_index);

                Material_Property *property = &material->properties[property_index];
                ImGui::Text("%.*s", HE_EXPAND_STRING(property->name));
                ImGui::SameLine();

                bool changed = true;

                switch (property->data_type)
                {
                    case Shader_Data_Type::U32:
                    {
                        if (property->is_texture_resource)
                        {
                            if (property->data.u64 != HE_MAX_U64)
                            {
                                Resource_Ref texture_ref = { property->data.u64 };
                                Resource *texture_resource = get_resource(texture_ref);
                                ImGui::Text("%.*s (%#x)", HE_EXPAND_STRING(get_name(texture_resource->relative_path)), texture_ref.uuid);
                            }
                            else
                            {
                                ImGui::Text("None");
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("Edit"))
                            {
                                ImGui::OpenPopup("Select Texture");
                            }

                            if (ImGui::BeginPopupModal("Select Texture", NULL, ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse))
                            {
                                static S32 selected_index = -1;
                                const Dynamic_Array< Resource > &resources = get_resources();

                                if (property->data.u64 != HE_MAX_U64)
                                {
                                    Resource_Ref texture_ref = { property->data.u64 };
                                    Resource *texture_resource = get_resource(texture_ref);
                                    selected_index = (S32)(texture_resource - resources.data);
                                }
                                else
                                {
                                    selected_index = -1;
                                }

                                if (ImGui::BeginListBox("Texture"))
                                {
                                    const bool is_selected = selected_index == -1;
                                    if (ImGui::Selectable("None", is_selected))
                                    {
                                        selected_index = -1;
                                        set_property(material_handle, property_index, { .u64 = HE_MAX_U64 });
                                    }

                                    if (is_selected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }

                                    for (U32 resource_index = 0; resource_index < resources.count; resource_index++)
                                    {
                                        const Resource &resource = resources[resource_index];
                                        if (resource.type != Asset_Type::TEXTURE)
                                        {
                                            continue;
                                        }

                                        const bool is_selected = selected_index == (S32)resource_index;
                                        ImGui::PushID(resource_index);

                                        if (ImGui::Selectable(resource.relative_path.data, is_selected))
                                        {
                                            selected_index = (S32)resource_index;
                                            set_property(material_handle, property_index, { .u64 = resource.uuid });
                                        }

                                        if (is_selected)
                                        {
                                            ImGui::SetItemDefaultFocus();
                                        }

                                        ImGui::PopID();
                                    }

                                    ImGui::EndListBox();
                                }

                                if (ImGui::Button("OK"))
                                {
                                    ImGui::CloseCurrentPopup();
                                }

                                ImGui::EndPopup();
                            }
                        }
                        else
                        {
                            ImGui::DragInt("##Property", &property->data.s32);
                        }
                    } break;

                    case Shader_Data_Type::F32:
                    {
                        changed &= ImGui::DragFloat("##Property", &property->data.f32);
                    } break;

                    case Shader_Data_Type::VECTOR2F:
                    {
                        changed &= ImGui::DragFloat2("##Property", (F32 *)&property->data.v2);
                    } break;

                    case Shader_Data_Type::VECTOR3F:
                    {
                        if (property->is_color)
                        {
                            changed &= ImGui::ColorEdit3("##Property", (F32*)&property->data.v3);
                        }
                        else
                        {
                            changed &= ImGui::DragFloat3("##Property", (F32 *)&property->data.v3);
                        }
                    } break;

                    case Shader_Data_Type::VECTOR4F:
                    {
                        if (property->is_color)
                        {
                            changed &= ImGui::ColorEdit4("##Property", (F32*)&property->data.v4);
                        }
                        else
                        {
                            changed &= ImGui::DragFloat4("##Property", (F32 *)&property->data.v4);
                        }
                    } break;
                };

                if (changed)
                {
                    set_property(material_handle, property_index, property->data);
                }

                ImGui::PopID();
            }
        }
    }
}

static void draw_tree(Scene_Node *node)
{
    ImGui::PushID(node);

    ImGuiTreeNodeFlags flags = 0;

    if (editor_state.selection.kind == Selection_Kind::SCENE_NODE && node == editor_state.selection.node)
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
        editor_state.selection.kind = Selection_Kind::SCENE_NODE;
        editor_state.selection.node = node;
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

// =============================== Editor ==========================================

static String get_resource_state_string(Resource_State resource_state)
{
    switch (resource_state)
    {
        case Resource_State::UNLOADED:
            return HE_STRING_LITERAL("Unloaded");

        case Resource_State::PENDING:
            return HE_STRING_LITERAL("Pending");

        case Resource_State::LOADED:
            return HE_STRING_LITERAL("Loaded");

        default:
            HE_ASSERT("unsupported resource state");
            break;
    }

    return HE_STRING_LITERAL("");
}

static String get_asset_state_string(Asset_State state)
{
    switch (state)
    {
        case Asset_State::UNCONDITIONED:
            return HE_STRING_LITERAL("Unconditioned");

        case Asset_State::PENDING:
            return HE_STRING_LITERAL("Pending");

        case Asset_State::CONDITIONED:
            return HE_STRING_LITERAL("Conditioned");

        default:
            HE_ASSERT("unsupported asset state");
            break;
    }

    return HE_STRING_LITERAL("");
}

void draw_resource_system()
{
    const Dynamic_Array< Resource > &resources = get_resources();
    const Dynamic_Array< Asset > &assets = get_assets();

    // Assets
    {
        ImGui::Begin("Assets");

        if (ImGui::Button("Create Skybox"))
        {
            ImGui::OpenPopup("Create Skybox");
        }

        if (ImGui::BeginPopupModal("Create Skybox", NULL, ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse))
        {
            ImGui::Text("Name");
            ImGui::SameLine();

            static char buf[512] = {};
            ImGui::InputText("##Name", buf, 512);

            ImGui::Text("Tint Color");
            ImGui::SameLine();

            static glm::vec3 tint_color = { 1.0f, 1.0f, 1.0f };
            ImGui::ColorEdit3("##Tint Color", &tint_color.r);

            static U64 right_texture = HE_MAX_U64;

            if (ImGui::Button("Right Texture"))
            {
                String texture_extensions[] =
                {
                    HE_STRING_LITERAL("png"),
                    HE_STRING_LITERAL("jpeg"),
                    HE_STRING_LITERAL("jpg")
                };

                Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
                String path = open_file_dialog(HE_STRING_LITERAL("Choose Texture"), HE_STRING_LITERAL("Texture"), to_array_view(texture_extensions), scratch_memory.arena);
                if (path.count)
                {
                    String resoruce_path = get_resource_path();
                    String absolute_path = path;
                    String relative_path = sub_string(absolute_path, resoruce_path.count + 1);
                    Asset *asset = find_asset(relative_path);
                    if (asset)
                    {
                        right_texture = asset->uuid;
                    }
                }
            }

            ImGui::SameLine();

            if (right_texture == HE_MAX_U64)
            {
                ImGui::Text("None");
            }
            else
            {
                Asset *asset = get_asset(right_texture);
                ImGui::Text("%.*s", HE_EXPAND_STRING(asset->relative_path));
            }

            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                zero_memory(buf, sizeof(buf));
                tint_color = { 1.0f, 1.0f, 1.0f };
                right_texture = HE_MAX_U64;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                zero_memory(buf, sizeof(buf));
                tint_color = { 1.0f, 1.0f, 1.0f };
                right_texture = HE_MAX_U64;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        const char* coloum_names[] =
        {
            "No.",
            "UUID",
            "Type",
            "Asset",
            "State",
            "Resource Refs"
        };

        ImGuiTableFlags flags = ImGuiTableFlags_Borders|ImGuiTableFlags_Resizable;

        if (ImGui::BeginTable("Table", HE_ARRAYCOUNT(coloum_names), flags))
        {
            for (U32 col = 0; col < HE_ARRAYCOUNT(coloum_names); col++)
            {
                ImGui::TableSetupColumn(coloum_names[col], ImGuiTableColumnFlags_WidthStretch);
            }

            ImGui::TableHeadersRow();

            for (U32 row = 0; row < assets.count; row++)
            {
                const Asset &asset = assets[row];
                const Resource_Type_Info &info = get_info(asset);

                ImGui::PushID(row + 1);

                ImGui::TableNextRow();

                ImGui::TableNextColumn();

                char label[32];
                sprintf(label, "%04d", row + 1);

                bool selected = editor_state.selection.kind == Selection_Kind::ASSET && editor_state.selection.asset_uuid == asset.uuid;
                if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowOverlap))
                {
                    editor_state.selection.kind = Selection_Kind::ASSET;
                    editor_state.selection.asset_uuid = asset.uuid;
                }

                ImGui::TableNextColumn();
                ImGui::Text("%#x", asset.uuid);

                ImGui::TableNextColumn();
                ImGui::Text("%.*s", HE_EXPAND_STRING(info.name));

                ImGui::TableNextColumn();
                ImGui::Text("%.*s", HE_EXPAND_STRING(asset.relative_path));

                ImGui::TableNextColumn();
                ImGui::Text("%.*s", HE_EXPAND_STRING(get_asset_state_string(asset.state)));

                ImGui::TableNextColumn();
                if (asset.resource_refs.count)
                {
                    for (U64 uuid : asset.resource_refs)
                    {
                        ImGui::Text("%#x", uuid);
                    }
                }
                else
                {
                    ImGui::Text("None");
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::End();
    }

    // Resources
    {
        ImGui::Begin("Resources");

        const char* coloum_names[] =
        {
            "No.",
            "UUID",
            "Asset UUID",
            "Type",
            "Resource",
            "State",
            "Ref Count",
            "Refs",
            "Children"
        };

        ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTable("Table", HE_ARRAYCOUNT(coloum_names), flags))
        {
            for (U32 col = 0; col < HE_ARRAYCOUNT(coloum_names); col++)
            {
                ImGui::TableSetupColumn(coloum_names[col], ImGuiTableColumnFlags_WidthStretch);
            }

            ImGui::TableHeadersRow();

            for (U32 row = 0; row < resources.count; row++)
            {
                const Resource &resource = resources[row];
                const Resource_Type_Info &info = get_info(resource);

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("%d", row + 1);

                ImGui::TableNextColumn();
                ImGui::Text("%#x", resource.uuid);

                ImGui::TableNextColumn();
                ImGui::Text("%#x", resource.asset_uuid);

                ImGui::TableNextColumn();
                ImGui::Text("%.*s", HE_EXPAND_STRING(info.name));

                ImGui::TableNextColumn();
                ImGui::Text("%.*s", HE_EXPAND_STRING(resource.relative_path));

                ImGui::TableNextColumn();
                ImGui::Text("%.*s", HE_EXPAND_STRING(get_resource_state_string(resource.state)));

                ImGui::TableNextColumn();
                ImGui::Text("%u", resource.ref_count);

                ImGui::TableNextColumn();
                if (resource.resource_refs.count)
                {
                    for (U64 uuid : resource.resource_refs)
                    {
                        ImGui::Text("%#x", uuid);
                    }
                }
                else
                {
                    ImGui::Text("None");
                }

                ImGui::TableNextColumn();
                if (resource.children.count)
                {
                    for (U64 uuid : resource.children)
                    {
                        ImGui::Text("%#x", uuid);
                    }
                }
                else
                {
                    ImGui::Text("None");
                }
            }

            ImGui::EndTable();
        }

        ImGui::End();
    }
}

void draw_asset(Asset *asset)
{
    const Resource_Type_Info &info = get_info(*asset);
    ImGui::Text("Path: %.*s", HE_EXPAND_STRING(asset->relative_path));
    ImGui::Text("Type: %.*s", HE_EXPAND_STRING(info.name));
    ImGui::Text("UUID: %llu", asset->uuid);
    ImGui::Text("State: %.*s", HE_EXPAND_STRING(get_asset_state_string(asset->state)));
}