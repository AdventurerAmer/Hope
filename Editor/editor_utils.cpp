#include "editor_utils.h"

#include <assets/asset_manager.h>
#include <rendering/renderer.h>
#include <core/file_system.h>

#include <imgui/imgui.h>

bool select_asset(String name, String type, Asset_Handle* asset_handle, const Select_Asset_Config &config)
{
    Render_Context render_context = get_render_context();
    Renderer *renderer = render_context.renderer;
    Renderer_State *renderer_state = render_context.renderer_state;

    const Asset_Info *info = get_asset_info(type);
    HE_ASSERT(info);

    bool result = false;

    if (ImGui::Button(name.data))
    {
        Memory_Context memory_context = grab_memory_context();
        
        String title = HE_STRING_LITERAL("Pick Asset");
        String filter = name;
        String absolute_path = open_file_dialog(title, filter, { .count = info->extension_count, .data = info->extensions }, memory_context.temp_allocator);
        if (absolute_path.count)
        {
            // HE_DEFER { HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, (void *)absolute_path.data); };

            String asset_path = get_asset_path();
            String path = sub_string(absolute_path, asset_path.count + 1);

            if (path.count)
            {
                *asset_handle = import_asset(path);
                if (config.load_on_select)
                {
                    aquire_asset(*asset_handle);
                }
                result = true;
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
                if (config.load_on_select)
                {
                    aquire_asset(*asset_handle);
                }
                result = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (asset_handle->uuid == 0)
    {
        ImGui::SameLine();
        ImGui::Text("None");
    }
    else if (is_asset_handle_valid(*asset_handle))
    {
        const Asset_Registry_Entry &entry = get_asset_registry_entry(*asset_handle);
        ImGui::SameLine();
        ImGui::Text(get_name(entry.path).data);

        if (config.nullify)
        {
            ImGui::SameLine();
            if (ImGui::Button("X"))
            {
                if (config.load_on_select)
                {
                    release_asset(*asset_handle);
                }
                *asset_handle = {};
                result = true;
            }
        }
    }
    else
    {
        ImGui::SameLine();
        ImGui::Text("Invalid");
    }

    return result;
}