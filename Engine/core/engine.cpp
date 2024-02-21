#include "engine.h"
#include "platform.h"
#include "rendering/renderer.h"
#include "logging.h"
#include "cvars.h"
#include "job_system.h"
#include "file_system.h"
#include "resources/resource_system.h"

#include <chrono>
#include <imgui.h>

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
    api->get_render_context = &get_render_context;
}

void finalize_asset_loads(Renderer *renderer, Renderer_State *renderer_state)
{
    platform_lock_mutex(&renderer_state->allocation_groups_mutex);

    for (U32 allocation_group_index = 0; allocation_group_index < renderer_state->allocation_groups.count; allocation_group_index++)
    {
        Allocation_Group &allocation_group = renderer_state->allocation_groups[allocation_group_index];

        U64 semaphore_value = renderer_get_semaphore_value(allocation_group.semaphore);

        if (allocation_group.target_value == semaphore_value)
        {
            if (allocation_group.resource_index != -1)
            {
                Resource *resource = get_resource((U32)allocation_group.resource_index);
                platform_lock_mutex(&resource->mutex);
                HE_ASSERT(resource->state == Resource_State::PENDING);
                resource->state = Resource_State::LOADED;
                platform_unlock_mutex(&resource->mutex);

                HE_LOG(Resource, Trace, "resource loaded: %.*s\n", HE_EXPAND_STRING(resource->relative_path));
            }
            else
            {
                HE_LOG(Resource, Trace, "resource loaded: %.*s\n", HE_EXPAND_STRING(allocation_group.resource_name));
            }

            renderer_destroy_semaphore(allocation_group.semaphore);

            for (void *memory : allocation_group.allocations)
            {
                deallocate(&renderer_state->transfer_allocator, memory);
            }

            if (allocation_group.uploaded)
            {
                *allocation_group.uploaded = true;
            }

            remove_and_swap_back(&renderer_state->allocation_groups, allocation_group_index);
        }
    }

    platform_unlock_mutex(&renderer_state->allocation_groups_mutex);
}

bool startup(Engine *engine, void *platform_state)
{
    hock_engine_api(&engine->api);

    bool inited = init_memory_system();
    if (!inited)
    {
        return false;
    }

    init_logging_system();
    init_cvars("config.cvars");

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

    bool job_system_inited = init_job_system();
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
    Renderer_State *renderer_state = render_context.renderer_state;
    Renderer *renderer = render_context.renderer;
    Scene_Data *scene_data = &renderer_state->scene_data;

    scene_data->directional_light.direction = { 0.0f, -1.0f, 0.0f };
    scene_data->directional_light.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    scene_data->directional_light.intensity = 1.0f;

    bool game_initialized = game_code->init_game(engine);

    wait_for_all_jobs_to_finish();
    renderer_wait_for_gpu_to_finish_all_work();
    while (renderer_state->allocation_groups.count)
    {
        finalize_asset_loads(renderer, renderer_state);
    }

    Read_Entire_File_Result result = read_entire_file("shaders/bin/skybox.vert.spv", &renderer_state->transfer_allocator);
    Shader_Descriptor skybox_vertex_shader_descriptor =
    {
        .data = result.data,
        .size = result.size
    };
    renderer_state->skybox_vertex_shader = renderer_create_shader(skybox_vertex_shader_descriptor);

    result = read_entire_file("shaders/bin/skybox.frag.spv", &renderer_state->transfer_allocator);
    Shader_Descriptor skybox_fragment_shader_descriptor =
    {
        .data = result.data,
        .size = result.size
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
        .settings =
        {
            .cull_mode = Cull_Mode::NONE,
            .front_face = Front_Face::COUNTER_CLOCKWISE,
            .fill_mode = Fill_Mode::SOLID,
            .depth_testing = false,
            .sample_shading = true,
        },
        .shader_group = renderer_state->skybox_shader_group,
        .render_pass = get_render_pass(&renderer_state->render_graph, "opaque"),
    };
    renderer_state->skybox_pipeline = renderer_create_pipeline_state(skybox_pipeline_state_descriptor);

    Material_Descriptor skybox_material_descriptor =
    {
        .pipeline_state_handle = renderer_state->skybox_pipeline
    };
    renderer_state->skybox_material_handle = renderer_create_material(skybox_material_descriptor);
    set_property(renderer_state->skybox_material_handle, "skybox", { .u32 = (U32)renderer_state->skybox.index });

    U64 cube_uuid = aquire_resource("Cube/Cube.hres").uuid;
    render_context.renderer_state->cube_static_mesh_uuid = find_resource("Cube/static_mesh_Cube.hres").uuid;
    aquire_resource("Corset/Corset.hres");

    wait_for_all_jobs_to_finish();
    renderer_wait_for_gpu_to_finish_all_work();
    while (render_context.renderer_state->allocation_groups.count)
    {
        finalize_asset_loads(render_context.renderer, render_context.renderer_state);
    }

    return game_initialized;
}

void on_resize(Engine *engine, U32 window_width, U32 window_height, U32 client_width, U32 client_height)
{
    Window *window = &engine->window;
    window->width = window_width;
    window->height = window_height;
    renderer_on_resize(client_width, client_height);
}

static void draw_node(Scene_Node *node)
{
    ImGui::Text("Node: %.*s", HE_EXPAND_STRING(node->name));
    ImGui::Separator();

    ImGui::Text("");
    ImGui::Text("Transfom");
    ImGui::Separator();

    Transform &transform = node->transform;

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
    Render_Context render_context = get_render_context();
    Scene_Data *scene_data = &render_context.renderer_state->scene_data;
    Directional_Light *directional_light = &scene_data->directional_light;

    Renderer *renderer = render_context.renderer;
    Renderer_State *renderer_state = render_context.renderer_state;

    static float reload_timer = 0;
    float reload_time = 1.0f;

    finalize_asset_loads(renderer, renderer_state);

    reload_timer += delta_time;
    while (reload_timer >= reload_time)
    {
        reload_resources();
        reload_timer -= reload_time;
    }

    Temprary_Memory_Arena temprary_memory = begin_scratch_memory();

    Game_Code *game_code = &engine->game_code;
    game_code->on_update(engine, delta_time);

    if (!engine->is_minimized)
    {
        imgui_new_frame();

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
                draw_node(selected_node);
            }

            ImGui::End();
        }

        // Resource System
        {
            imgui_draw_resource_system();
        }

        // Memory System
        {
            imgui_draw_memory_system();
        }

        U32 frame_index = renderer_state->current_frame_in_flight_index;
        Buffer *object_data_storage_buffer = get(&renderer_state->buffers, renderer_state->object_data_storage_buffers[frame_index]);
        renderer_state->object_data_base = (Object_Data *)object_data_storage_buffer->data;
        renderer_state->object_data_count = 0;

        renderer_state->opaque_packet_count = 0;
        renderer_state->opaque_packets = HE_ALLOCATE_ARRAY(temprary_memory.arena, Render_Packet, 4069); // todo(amer): @Hardcoding
        renderer_state->current_pipeline_state_handle = Resource_Pool<Pipeline_State>::invalid_handle;

        renderer_parse_scene_tree(renderer_state->root_scene_node);

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

        renderer->set_vertex_buffers(to_array_view(vertex_buffers), to_array_view(offsets));
        renderer->set_index_buffer(renderer_state->index_buffer, 0);

        U32 texture_count = renderer_state->textures.capacity;
        Texture_Handle *textures = HE_ALLOCATE_ARRAY(temprary_memory.arena, Texture_Handle, texture_count);
        Sampler_Handle *samplers = HE_ALLOCATE_ARRAY(temprary_memory.arena, Sampler_Handle, texture_count);

        for (auto it = iterator(&renderer_state->textures); next(&renderer_state->textures, it);)
        {
            Texture *texture = get(&renderer_state->textures, it);

            if (texture->is_attachment)
            {
                textures[it.index] = renderer_state->white_pixel_texture;
            }
            else if (!texture->is_uploaded_to_gpu)
            {
                textures[it.index] = renderer_state->white_pixel_texture;
            }
            else
            {
                textures[it.index] = it;
            }

            samplers[it.index] = texture->is_cubemap ? renderer_state->default_cubemap_sampler : renderer_state->default_texture_sampler;
        }

        Update_Binding_Descriptor update_textures_binding_descriptors[] =
        {
            {
                .binding_number = 0,
                .element_index = 0,
                .count = texture_count,
                .textures = textures,
                .samplers = samplers
            },
        };

        // todo(amer): renderer_update_bind_group
        platform_lock_mutex(&renderer_state->render_commands_mutex);
        renderer->update_bind_group(renderer_state->per_render_pass_bind_groups[renderer_state->current_frame_in_flight_index], to_array_view(update_textures_binding_descriptors));
        platform_unlock_mutex(&renderer_state->render_commands_mutex);

        Bind_Group_Handle bind_groups[] =
        {
            renderer_state->per_frame_bind_groups[renderer_state->current_frame_in_flight_index],
            renderer_state->per_render_pass_bind_groups[renderer_state->current_frame_in_flight_index]
        };
        renderer->set_bind_groups(0, to_array_view(bind_groups));

        render(&renderer_state->render_graph, renderer, renderer_state);

        renderer->end_frame();

        renderer_state->current_frame_in_flight_index++;
        if (renderer_state->current_frame_in_flight_index >= renderer_state->frames_in_flight)
        {
            renderer_state->current_frame_in_flight_index = 0;
        }
    }

    end_temprary_memory(&temprary_memory);
}

void shutdown(Engine *engine)
{
    deinit_resource_system();

    deinit_renderer_state();

    deinit_job_system();

    deinit_cvars();

    deinit_logging_system();

    deinit_memory_system();
}