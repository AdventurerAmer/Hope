#include "editor_utils.h"

#include <assets/asset_manager.h>
#include <rendering/renderer.h>
#include <core/file_system.h>

#include <imgui/imgui.h>

bool select_asset(String name, String type, Asset_Handle *asset_handle)
{
    Render_Context render_context = get_render_context();
    Renderer *renderer = render_context.renderer;
    Renderer_State *renderer_state = render_context.renderer_state;

    const Asset_Info *info = get_asset_info(type);
    HE_ASSERT(info);

    if (ImGui::Button(name.data))
    {
        Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
        String title = HE_STRING_LITERAL("Pick Asset");
        String filter = name;
        String absolute_path = open_file_dialog(title, filter, { .count = info->extension_count, .data = info->extensions });
        if (absolute_path.count)
        {
            HE_DEFER { deallocate(get_general_purpose_allocator(), (void *)absolute_path.data); };

            String asset_path = get_asset_path();
            String path = sub_string(absolute_path, asset_path.count + 1);

            if (path.count)
            {
                *asset_handle = import_asset(path);
            }
        }
    }

    if (ImGui::BeginDragDropTarget())
    {
        ImGuiDragDropFlags target_flags = 0;
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ASSET", target_flags))
        {
            Asset_Handle asset = *(const Asset_Handle *)payload->Data;
            if (is_asset_handle_valid(asset) && get_asset_info(asset) == info)
            {
                *asset_handle = asset;
            }
        }
        ImGui::EndDragDropTarget();
    }

    bool result = false;

    if (asset_handle->uuid == 0)
    {
        ImGui::SameLine();
        ImGui::Text("None");
    }
    else if (is_asset_handle_valid(*asset_handle))
    {
        const Asset_Registry_Entry &entry = get_asset_registry_entry(*asset_handle);
        ImGui::SameLine();
        ImGui::Text(entry.path.data);

        ImGui::SameLine();
        if (ImGui::Button("X"))
        {
            *asset_handle = {};
        }

        result = true;
    }
    else
    {
        ImGui::SameLine();
        ImGui::Text("Invalid");
    }

    return result;
}