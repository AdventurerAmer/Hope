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
                        Inspector_Panel::inspect_asset(asset_handle);
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
                        Inspector_Panel::inspect_asset(embeded_asset);
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

                Memory_Context memory_context = get_memory_context();

                String title = HE_STRING_LITERAL("Save Skybox Asset");
                String filter = HE_STRING_LITERAL("Skybox (.haskybox)");
                String absolute_path = save_file_dialog(title, filter, to_array_view(extensions));
                HE_DEFER { HE_ALLOCATOR_DEALLOCATE(memory_context.general, (void *)absolute_path.data); };
                if (absolute_path.count)
                {
                    String path = absolute_path;

                    String ext = get_extension(absolute_path);
                    if (ext != extensions[0])
                    {
                        path = format_string(memory_context.temprary_memory.arena, "%.*s.haskybox", HE_EXPAND_STRING(absolute_path));
                    }

                    String_Builder builder = {};
                    begin_string_builder(&builder, memory_context.temprary_memory.arena);
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

static void create_material_asset_modal(bool open)
{
    static Asset_Handle shader_asset = {};
    static Material_Type type = Material_Type::OPAQUE;

    if (open)
    {
        ImGui::OpenPopup("Create Material Popup Model");
    }

    if (ImGui::BeginPopupModal("Create Material Popup Model", NULL, ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse))
    {
        select_asset(HE_STRING_LITERAL("Shader"), HE_STRING_LITERAL("shader"), &shader_asset);

        static constexpr const char *types[] = { "opaque", "alpha_cutoff", "transparent"};

        ImGui::Text("Type");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##Type", types[(U32)type]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(types); i++)
            {
                bool is_selected = i == (U32)type;
                if (ImGui::Selectable(types[i], is_selected))
                {
                    type = (Material_Type)i;
                }

                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (is_asset_handle_valid(shader_asset))
        {
            if (!is_asset_loaded(shader_asset))
            {
                aquire_asset(shader_asset);
            }
            else
            {
                if (ImGui::Button("Ok", ImVec2(120, 0)))
                {
                    Memory_Context memory_context = get_memory_context();
                    
                    String extensions[] =
                    {
                        HE_STRING_LITERAL("hamaterial")
                    };

                    String title = HE_STRING_LITERAL("Save Material Asset");
                    String filter = HE_STRING_LITERAL("Material (.hamaterial)");

                    String absolute_path = save_file_dialog(title, filter, to_array_view(extensions));
                    HE_DEFER { HE_ALLOCATOR_DEALLOCATE(memory_context.general, (void *)absolute_path.data); };

                    if (absolute_path.count)
                    {
                        String path = absolute_path;

                        String ext = get_extension(absolute_path);
                        if (ext != extensions[0])
                        {
                            path = format_string(memory_context.temprary_memory.arena, "%.*s.hamaterial", HE_EXPAND_STRING(absolute_path));
                        }

                        Material_Descriptor material_desc =
                        {
                            .name = get_name(path),
                            .type = type,
                            .shader = get_asset_handle_as<Shader>(shader_asset),
                            .settings = {},
                        };
                        Material_Handle material = renderer_create_material(material_desc);
                        bool success = serialize_material(material, shader_asset.uuid, path);
                        if (success)
                        {
                            path = sub_string(path, get_asset_path().count + 1);
                            Asset_Handle asset_handle = import_asset(path);
                            set_parent(asset_handle, shader_asset);
                        }
                        renderer_destroy_material(material);
                    }

                    ImGui::CloseCurrentPopup();
                }

                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
            }
        }

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

}