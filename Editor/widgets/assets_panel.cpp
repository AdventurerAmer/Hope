#include "assets_panel.h"
#include "inspector_panel.h"
#include "../editor.h"
#include "../editor_utils.h"

#include <core/logging.h>

#include <rendering/renderer.h>
#include <rendering/renderer_utils.h>
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
            HE_ASSERT(!"unsupported cull mode");
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
            return "counter_clockwise";
        } break;

        default:
        {
            HE_ASSERT(!"unsupported front face");
        } break;
    }

    return "";
}

const char* compare_operation_to_str(Compare_Operation op)
{
    switch (op)
    {
        case Compare_Operation::NEVER: return "never";
        case Compare_Operation::LESS: return "less";
        case Compare_Operation::EQUAL: return "equal";
        case Compare_Operation::LESS_OR_EQUAL: return "less_or_equal";
        case Compare_Operation::GREATER: return "greater";
        case Compare_Operation::NOT_EQUAL: return "not_equal";
        case Compare_Operation::GREATER_OR_EQUAL: return "greater_or_equal";
        case Compare_Operation::ALWAYS: return "always";

        default:
        {
            HE_ASSERT(!"unsupported compare operation");
        } break;
    }

    return "";
}

const char* stencil_operation_to_str(Stencil_Operation op)
{
    switch (op)
    {
        case Stencil_Operation::KEEP: return "keep";
        case Stencil_Operation::ZERO: return "zero";
        case Stencil_Operation::REPLACE: return "replace";
        case Stencil_Operation::INCREMENT_AND_CLAMP: return "increment_and_clamp";
        case Stencil_Operation::DECREMENT_AND_CLAMP: return "decrement_and_clamp";
        case Stencil_Operation::INVERT: return "invert";
        case Stencil_Operation::INCREMENT_AND_WRAP: return "increment_and_wrap";
        case Stencil_Operation::DECREMENT_AND_WRAP: return "decrement_and_wrap";

        default:
        {
            HE_ASSERT(!"unsupported stencil operation");
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
    Pipeline_State_Settings pipeline_state_settings;
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
            asset_data.pipeline_state_settings = {};
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

        Pipeline_State_Settings &settings = asset_data.pipeline_state_settings;

        if (ImGui::BeginCombo("##Cull Mode", cull_modes[(U32)settings.cull_mode]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(cull_modes); i++)
            {
                bool is_selected = i == (U32)settings.cull_mode;
                if (ImGui::Selectable(cull_modes[i], is_selected))
                {
                    settings.cull_mode = (Cull_Mode)i;
                }

                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        static constexpr const char *front_faces[] = { "clockwise", "counter clockwise" };

        ImGui::Text("Front Face");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##Front Face", front_faces[(U32)settings.front_face]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(front_faces); i++)
            {
                bool is_selected = i == (U32)settings.front_face;
                if (ImGui::Selectable(front_faces[i], is_selected))
                {
                    settings.front_face = (Front_Face)i;
                }

                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        static constexpr const char *compare_ops[] = { "never", "less", "equal", "less or equal", "greater", "not equal", "greater or equal", "always" };

        ImGui::Text("Depth Operation");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##Depth Operation", compare_ops[(U32)settings.depth_operation]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(compare_ops); i++)
            {
                bool is_selected = i == (U32)settings.depth_operation;
                if (ImGui::Selectable(compare_ops[i], is_selected))
                {
                    settings.depth_operation = (Compare_Operation)i;
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
        ImGui::Checkbox("##Depth Testing", &settings.depth_testing);

        ImGui::Text("Depth Writing");
        ImGui::SameLine();
        ImGui::Checkbox("##Depth Writing", &settings.depth_writing);

        ImGui::Text("Stencil Operation");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##Stencil Operation", compare_ops[(U32)settings.stencil_operation]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(compare_ops); i++)
            {
                bool is_selected = i == (U32)settings.stencil_operation;
                if (ImGui::Selectable(compare_ops[i], is_selected))
                {
                    settings.stencil_operation = (Compare_Operation)i;
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::Text("Stencil Testing");
        ImGui::SameLine();
        ImGui::Checkbox("##Stencil Testing", &settings.stencil_testing);

        static constexpr const char *stencil_ops[] = { "keep", "zero", "replace", "increment and clamp", "decrement and clamp", "invert", "increment and wrap", "decrement and wrap" };

        ImGui::Text("Stencil Fail");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##Stencil Fail", stencil_ops[(U32)settings.stencil_fail]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(stencil_ops); i++)
            {
                bool is_selected = i == (U32)settings.stencil_fail;
                if (ImGui::Selectable(stencil_ops[i], is_selected))
                {
                    settings.stencil_fail = (Stencil_Operation)i;
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::Text("Stencil Pass");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##Stencil Pass", stencil_ops[(U32)settings.stencil_pass]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(stencil_ops); i++)
            {
                bool is_selected = i == (U32)settings.stencil_pass;
                if (ImGui::Selectable(stencil_ops[i], is_selected))
                {
                    settings.stencil_pass = (Stencil_Operation)i;
                }

                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::Text("Depth Fail");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##Depth Fail", stencil_ops[(U32)settings.depth_fail]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(stencil_ops); i++)
            {
                bool is_selected = i == (U32)settings.depth_fail;
                if (ImGui::Selectable(stencil_ops[i], is_selected))
                {
                    settings.depth_fail = (Stencil_Operation)i;
                }

                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::Text("Stencil Compare Mask");
        ImGui::SameLine();
        ImGui::DragInt("##Stencil Compare Mask", (int *)&settings.stencil_compare_mask, 1.0f, 0, 255);

        ImGui::Text("Stencil Write Mask");
        ImGui::SameLine();
        ImGui::DragInt("##Stencil Write Mask", (int *)&settings.stencil_write_mask, 1.0f, 0, 255);

        ImGui::Text("Stencil Reference Value");
        ImGui::SameLine();
        ImGui::DragInt("##Stencil Reference Value", (int *)&settings.stencil_reference_value, 1.0f, 0, 255);

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
                            ImGui::DragFloat2("##Property", (F32 *)&property->data.v2f);
                        } break;

                        case Shader_Data_Type::VECTOR3F:
                        {
                            if (is_color)
                            {
                                ImGui::ColorEdit3("##Property", (F32 *)&property->data.v3f);
                            }
                            else
                            {
                                ImGui::DragFloat3("##Property", (F32 *)&property->data.v3f);
                            }
                        } break;

                        case Shader_Data_Type::VECTOR4F:
                        {
                            if (property->is_color)
                            {
                                ImGui::ColorEdit4("##Property", (F32 *)&property->data.v4f);
                            }
                            else
                            {
                                ImGui::DragFloat4("##Property", (F32 *)&property->data.v4f);
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
                    append(&builder, "cull_mode %s\n", cull_mode_to_string(settings.cull_mode));
                    append(&builder, "front_face %s\n", front_face_to_string(settings.front_face));

                    append(&builder, "depth_operation %s\n", compare_operation_to_str(settings.depth_operation));
                    append(&builder, "depth_testing %s\n", settings.depth_testing ? "true" : "false");
                    append(&builder, "depth_writing %s\n", settings.depth_writing ? "true" : "false");

                    append(&builder, "stencil_operation %s\n", compare_operation_to_str(settings.stencil_operation));
                    append(&builder, "stencil_testing %s\n", settings.stencil_testing ? "true" : "false");
                    append(&builder, "stencil_pass %s\n", stencil_operation_to_str(settings.stencil_pass));
                    append(&builder, "stencil_fail %s\n", stencil_operation_to_str(settings.stencil_fail));
                    append(&builder, "depth_fail %s\n", stencil_operation_to_str(settings.depth_fail));

                    append(&builder, "stencil_compare_mask %u\n", settings.stencil_compare_mask);
                    append(&builder, "stencil_write_mask %u\n", settings.stencil_write_mask);
                    append(&builder, "stencil_reference_value %u\n", settings.stencil_reference_value);

                    append(&builder, "property_count %u\n", asset_data.property_count);

                    for (U32 i = 0; i < asset_data.property_count; i++)
                    {
                        Material_Property *property = &asset_data.properties[i];
                        bool is_texture_asset = ends_with(property->name, HE_STRING_LITERAL("texture")) || ends_with(property->name, HE_STRING_LITERAL("cubemap"));
                        bool is_color = ends_with(property->name, HE_STRING_LITERAL("color"));
                        append(&builder, "%.*s %.*s ", HE_EXPAND_STRING(property->name), HE_EXPAND_STRING(shader_data_type_to_str(property->data_type)));
                        switch (property->data_type)
                        {
                            case Shader_Data_Type::U8:
                            case Shader_Data_Type::U16:
                            case Shader_Data_Type::U64:
                            {
                                append(&builder, "%llu\n", property->data.u64);
                            } break;

                            case Shader_Data_Type::U32:
                            {
                                append(&builder, "%llu\n", is_texture_asset ? property->data.u64 : property->data.u32);
                            } break;

                            case Shader_Data_Type::S8:
                            {
                                append(&builder, "%ll\n", property->data.s8);
                            } break;

                            case Shader_Data_Type::S16:
                            {
                                append(&builder, "%ll\n", property->data.s16);
                            } break;

                            case Shader_Data_Type::S32:
                            {
                                append(&builder, "%ll\n", property->data.s32);
                            } break;

                            case Shader_Data_Type::S64:
                            {
                                append(&builder, "%ll\n", property->data.s64);
                            } break;

                            case Shader_Data_Type::F16:
                            case Shader_Data_Type::F32:
                            case Shader_Data_Type::F64:
                            {
                                append(&builder, "%f\n", property->data.f64);
                            } break;

                            case Shader_Data_Type::VECTOR2F:
                            {
                                append(&builder, "%f %f\n", property->data.v2f.x, property->data.v2f.y);
                            } break;

                            case Shader_Data_Type::VECTOR2S:
                            {
                                append(&builder, "%ll %ll\n", property->data.v2s.x, property->data.v2s.y);
                            } break;

                            case Shader_Data_Type::VECTOR2U:
                            {
                                append(&builder, "%llu %llu\n", property->data.v2u.x, property->data.v2u.y);
                            } break;

                            case Shader_Data_Type::VECTOR3F:
                            {
                                append(&builder, "%f %f %f\n", property->data.v3f.x, property->data.v3f.y, property->data.v3f.z);
                            } break;

                            case Shader_Data_Type::VECTOR3S:
                            {
                                append(&builder, "%ll %ll %ll\n", property->data.v3s.x, property->data.v3s.y, property->data.v3s.z);
                            } break;

                            case Shader_Data_Type::VECTOR3U:
                            {
                                append(&builder, "%llu %llu %llu\n", property->data.v3u.x, property->data.v3u.y, property->data.v3u.z);
                            } break;

                            case Shader_Data_Type::VECTOR4F:
                            {
                                append(&builder, "%f %f %f %f\n", property->data.v4f.x, property->data.v4f.y, property->data.v4f.z, property->data.v4f.w);
                            } break;

                            case Shader_Data_Type::VECTOR4S:
                            {
                                append(&builder, "%ll %ll %ll %ll\n", property->data.v4s.x, property->data.v4s.y, property->data.v4s.z, property->data.v4s.w);
                            } break;

                            case Shader_Data_Type::VECTOR4U:
                            {
                                append(&builder, "%llu %llu %llu %llu\n", property->data.v4u.x, property->data.v4u.y, property->data.v4u.z, property->data.v4u.w);
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