#include "assets_panel.h"
#include "inspector_panel.h"
#include "../editor.h"
#include "../editor_utils.h"

#include <core/logging.h>

#include <rendering/renderer.h>
#include <assets/asset_manager.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <core/file_system.h>

#include <filesystem>
namespace fs = std::filesystem;

namespace Assets_Panel {

struct Assets_Panel_State
{
    fs::path asset_path;
    fs::path selected_path;
};

static Assets_Panel_State assets_panel_state;

void set_path(String path)
{
    assets_panel_state.asset_path = fs::path(path.data);
}

void create_skybox_asset_modal(bool open);
void create_material_asset_modal(bool open);

void draw()
{
    static fs::path current_path;
    if (current_path.empty())
    {
        current_path = assets_panel_state.asset_path;
    }

    ImGui::Begin("Assets");

    ImGui::BeginDisabled(current_path == assets_panel_state.asset_path);
    if (ImGui::Button("Back"))
    {
        current_path = current_path.parent_path();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Text("%s", current_path.string().c_str());

    if (ImGui::BeginListBox("##Begin List Box", ImGui::GetContentRegionAvail()))
    {
        for (const auto &it : fs::directory_iterator(current_path))
        {
            const fs::path &path = it.path();
            const fs::path &relative = path.lexically_relative(current_path);

            bool is_asset_file = false;

            auto asset_path_string = path.lexically_relative(assets_panel_state.asset_path).string();

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

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth|ImGuiTreeNodeFlags_FramePadding|ImGuiTreeNodeFlags_OpenOnArrow;

            if (assets_panel_state.selected_path == path)
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

            static bool was_toggled = false;

            if (ImGui::IsItemClicked())
            {
                was_toggled = ImGui::IsItemToggledOpen();
            }

            if (ImGui::IsItemDeactivated() && !ImGui::IsDragDropActive() && !was_toggled)
            {
                if (it.is_directory())
                {
                    current_path = path;
                }
                else
                {
                    Editor::reset_selection();

                    if (is_asset_file)
                    {
                        if (asset_handle.uuid == 0)
                        {
                            asset_handle = import_asset(asset_path);
                        }
                        Inspector_Panel::inspect(asset_handle);
                    }

                    assets_panel_state.selected_path = path;
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
                    bool is_selected = assets_panel_state.selected_path == path;
                    ImGui::Selectable(name.data, &is_selected);
                    if (ImGui::IsItemDeactivated() && !ImGui::IsDragDropActive())
                    {
                        Editor::reset_selection();
                        Inspector_Panel::inspect(embeded_asset);
                        assets_panel_state.selected_path = path;
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

        ImGui::EndListBox();
    }

    ImGui::End();
}

void reset_selection()
{
    assets_panel_state.selected_path = "";
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

}