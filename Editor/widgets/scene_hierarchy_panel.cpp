#include "scene_hierarchy_panel.h"
#include "inspector_panel.h"
#include "../editor.h"

#include <core/logging.h>

#include <rendering/renderer.h>
#include <assets/asset_manager.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace Scene_Hierarchy_Panel {

struct Scene_Hierarchy_State
{
    S32 context_menu_node_index = -1;
    S32 selected_node_index = -1;
    S32 dragging_node_index = -1;

    S32 rename_node_index = -1;
    char rename_buffer[128];
};

static Scene_Hierarchy_State scene_hierarchy_state;

enum class Add_Scene_Node_Operation
{
    FIRST,
    LAST,
    AFTER,
};

void draw_scene_node(Asset_Handle asset_handle, Scene_Handle scene_handle, Scene *scene, S32 node_index);
void add_model_to_scene(Scene *scene, U32 node_index, Asset_Handle asset_handle, Add_Scene_Node_Operation op);

void new_node(Scene *scene, U32 parent_index)
{
    U32 node_index = allocate_node(scene, HE_STRING_LITERAL("Node"));
    add_child_last(scene, parent_index, node_index);
}

void rename_node(Scene *scene, U32 node_index)
{
    Scene_Node *node = get_node(scene, node_index);
    memset(scene_hierarchy_state.rename_buffer, 0, sizeof(scene_hierarchy_state.rename_buffer));
    memcpy(scene_hierarchy_state.rename_buffer, node->name.data, node->name.count);
    scene_hierarchy_state.rename_node_index = node_index;
}

void delete_node(Scene *scene, U32 node_index)
{
    Scene_Node *node = get_node(scene, node_index);
    remove_node(scene, node_index);
    if (node_index == scene_hierarchy_state.selected_node_index)
    {
        Editor::reset_selection();
    }
}

void duplicate_node(Scene *scene, U32 node_index)
{
    Memory_Context memory_context = get_memory_context();
    
    Scene_Node *node = get_node(scene, node_index);
    U32 duplicated_node_index = allocate_node(scene, format_string(memory_context.temprary_memory.arena, "%.*s_", HE_EXPAND_STRING(node->name)));
    add_child_after(scene, node_index, duplicated_node_index);

    node = get_node(scene, node_index);

    Scene_Node *duplicated_node = get_node(scene, duplicated_node_index);
    duplicated_node->transform = node->transform;

    duplicated_node->has_mesh = node->has_mesh;
    duplicated_node->mesh = node->mesh;

    duplicated_node->has_light = node->has_light;
    duplicated_node->light = node->light;
}

void draw(U64 scene_asset_uuid)
{
    Asset_Handle scene_asset = { .uuid = scene_asset_uuid };

    ImGui::Begin("Hierarchy");
    
    static bool is_focused_last_frame = false;
    bool is_focused = ImGui::IsWindowFocused();
    
    if (!is_focused && is_focused_last_frame)
    {
        scene_hierarchy_state.rename_node_index = -1;
    }

    is_focused_last_frame = is_focused;

    if (is_asset_handle_valid(scene_asset))
    {
        if (!is_asset_loaded(scene_asset))
        {
            aquire_asset(scene_asset);
        }
        else
        {
            Scene_Handle scene_handle = get_asset_handle_as<Scene>(scene_asset);
            Scene *scene = renderer_get_scene(scene_handle);
            draw_scene_node(scene_asset, scene_handle, scene, 0);

            static bool is_context_window_open = false;

            if (ImGui::BeginPopupContextWindow())
            {
                is_context_window_open = true;

                const char *label = "Create Child Node";
                if (scene_hierarchy_state.context_menu_node_index == -1)
                {
                    label = "Create Node";
                }

                if (ImGui::MenuItem(label, "Ctrl+N"))
                {
                    U32 parent = scene_hierarchy_state.context_menu_node_index == -1 ? 0 : scene_hierarchy_state.context_menu_node_index;
                    new_node(scene, parent);
                }

                if (scene_hierarchy_state.context_menu_node_index != -1)
                {
                    if (ImGui::MenuItem("Rename", "F2"))
                    {
                        rename_node(scene, (U32)scene_hierarchy_state.context_menu_node_index);
                    }

                    if (ImGui::MenuItem("Delete", "Del"))
                    {
                        delete_node(scene, (U32)scene_hierarchy_state.context_menu_node_index);
                    }

                    if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                    {
                        duplicate_node(scene, (U32)scene_hierarchy_state.context_menu_node_index);
                    }
                }

                ImGui::EndPopup();
            }
            else
            {
                if (is_context_window_open)
                {
                    scene_hierarchy_state.context_menu_node_index = -1;
                    is_context_window_open = false;
                }
            }
        }
    }
    ImGui::End();
}

void select(S32 node_index)
{
     scene_hierarchy_state.selected_node_index = node_index;
}

S32 get_selected_node()
{
    return scene_hierarchy_state.selected_node_index;
}

void reset_selection()
{
    scene_hierarchy_state.context_menu_node_index = -1;
    scene_hierarchy_state.selected_node_index = -1;
    scene_hierarchy_state.rename_node_index = -1;
    scene_hierarchy_state.dragging_node_index = -1;
}

static void add_model_to_scene(Scene *scene, U32 node_index, Asset_Handle asset_handle, Add_Scene_Node_Operation op)
{
    Memory_Context memory_context = get_memory_context();

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
        
        U32 sub_scene_parent = node_index;

        if (model->node_count != 1)
        {
            sub_scene_parent = allocate_node(scene, model->name);

            switch (op)
            {
                case Add_Scene_Node_Operation::FIRST:
                {
                    add_child_first(scene, node_index, sub_scene_parent);
                } break;

                case Add_Scene_Node_Operation::LAST:
                {
                    add_child_last(scene, node_index, sub_scene_parent);
                } break;

                case Add_Scene_Node_Operation::AFTER:
                {
                    add_child_after(scene, node_index, sub_scene_parent);
                } break;
            }
        }

        U32 *node_indices = HE_ALLOCATOR_ALLOCATE_ARRAY(memory_context.temp, U32, model->node_count);

        for (U32 i = 0; i < model->node_count; i++)
        {
            Scene_Node *model_node = &model->nodes[i];

            node_indices[i] = allocate_node(scene, model_node->name);
            Scene_Node *current_scene_node = get_node(scene, node_indices[i]);
            current_scene_node->transform = model_node->transform;

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
                            add_child_first(scene, sub_scene_parent, node_indices[i]);
                        } break;

                        case Add_Scene_Node_Operation::LAST:
                        {
                            add_child_last(scene, sub_scene_parent, node_indices[i]);
                        } break;

                        case Add_Scene_Node_Operation::AFTER:
                        {
                            add_child_after(scene, sub_scene_parent, node_indices[i]);
                        } break;
                    }
                }
                else
                {
                    add_child_last(scene, sub_scene_parent, node_indices[i]);
                }
            }
            else
            {
                add_child_last(scene, node_indices[model_node->parent_index], node_indices[i]);
            }
        }
    }
}

static void draw_scene_node(Asset_Handle scene_asset, Scene_Handle scene_handle, Scene *scene, S32 node_index)
{
    Memory_Context memory_context = get_memory_context();

    HE_ASSERT(node_index != -1);

    ImGui::PushID(node_index);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth|ImGuiTreeNodeFlags_FramePadding|ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_OpenOnDoubleClick|ImGuiTreeNodeFlags_OpenOnArrow;

    Scene_Node *node = get_node(scene, node_index);
    bool is_leaf = node->first_child_index == -1;
    if (is_leaf)
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    if (node_index == scene_hierarchy_state.selected_node_index)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const char *label = node->name.data;

    bool should_edit_node_name = node_index == scene_hierarchy_state.rename_node_index;
    if (should_edit_node_name)
    {
        label = "##EditNodeName";
    }

    bool is_open = ImGui::TreeNodeEx(label, flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Inspector_Panel::inspect_scene_node(scene_asset, node_index);
        scene_hierarchy_state.selected_node_index = node_index;
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        scene_hierarchy_state.context_menu_node_index = node_index;
    }

    ImGuiDragDropFlags src_flags = 0;
    src_flags |= ImGuiDragDropFlags_SourceNoDisableHover;
    src_flags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers;

    if (ImGui::BeginDragDropSource(src_flags))
    {
        scene_hierarchy_state.dragging_node_index = node_index;
        ImGui::SetDragDropPayload("DND_SCENE_NODE", &node_index, sizeof(U32));
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_SCENE_NODE"))
        {
            U32 child_node_index = *(const U32*)payload->Data;
            Scene_Node *child = get_node(scene, child_node_index);
            remove_child(scene, child->parent_index, child_node_index);
            add_child_last(scene, node_index, child_node_index);
        }

        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ASSET"))
        {
            Asset_Handle asset_handle = *(const Asset_Handle *)payload->Data;
            add_model_to_scene(scene, node_index, asset_handle, Add_Scene_Node_Operation::LAST);
        }

        ImGui::EndDragDropTarget();
    }

    if (should_edit_node_name)
    {
        ImGui::SameLine();
        
        ImGui::SetKeyboardFocusHere();
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
        if (ImGui::InputText("##EditNodeNameTextInput", scene_hierarchy_state.rename_buffer, sizeof(scene_hierarchy_state.rename_buffer), flags))
        {
            if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemDeactivated())
            {
                scene_hierarchy_state.rename_node_index = -1;
                String new_name = HE_STRING(scene_hierarchy_state.rename_buffer);
                Scene_Node *node = get_node(scene, node_index);
                if (node->name.data && new_name.count)
                {
                    HE_ALLOCATOR_DEALLOCATE(memory_context.general, (void *)node->name.data);
                    node->name = copy_string(new_name, memory_context.general);
                }
            }
        }
    }

    if (is_open)
    {
        if (ImGui::IsDragDropActive())
        {
            bool show_drag_widget = false;
            bool is_dragging_scene_node = strcmp(ImGui::GetDragDropPayload()->DataType, "DND_SCENE_NODE") == 0 && scene_hierarchy_state.dragging_node_index != (S32)node_index && node->first_child_index != scene_hierarchy_state.dragging_node_index;
            show_drag_widget |= is_dragging_scene_node;
            
            bool is_dragging_asset = strcmp(ImGui::GetDragDropPayload()->DataType, "DND_ASSET") == 0;
            if (is_dragging_asset)
            {
                const ImGuiPayload* payload = ImGui::GetDragDropPayload();
                Asset_Handle asset_handle = *(const Asset_Handle*)payload->Data;
                show_drag_widget = get_asset_info(asset_handle)->name == "model";
            }
            
            node = get_node(scene, node_index);
            if (!is_leaf && show_drag_widget)
            {
                ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAvailWidth|ImGuiSelectableFlags_NoPadWithHalfSpacing;

                ImGui::Selectable("##DragFirstChild", true, flags, ImVec2(0.0f, 4.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_SCENE_NODE"))
                    {
                        U32 child_node_index = *(const U32*)payload->Data;
                        Scene_Node *child = get_node(scene, child_node_index);
                        remove_child(scene, child->parent_index, child_node_index);
                        add_child_first(scene, node_index, child_node_index);
                    }

                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ASSET"))
                    {
                        Asset_Handle asset_handle = *(const Asset_Handle *)payload->Data;
                        add_model_to_scene(scene, node_index, asset_handle, Add_Scene_Node_Operation::FIRST);
                    }

                    ImGui::EndDragDropTarget();
                }
            }
        }

        Scene_Node *node = get_node(scene, node_index);
        for (S32 child_node_index = node->first_child_index; child_node_index != -1; child_node_index = get_node(scene, child_node_index)->next_sibling_index)
        {
            draw_scene_node(scene_asset, scene_handle, scene, child_node_index);
        }

        ImGui::TreePop();
    }

    if (ImGui::IsDragDropActive())
    {
        bool show_drag_widget = false;
        show_drag_widget |= strcmp(ImGui::GetDragDropPayload()->DataType, "DND_SCENE_NODE") == 0 && scene_hierarchy_state.dragging_node_index != (S32)node_index && node->next_sibling_index != scene_hierarchy_state.dragging_node_index;    
        bool is_dragging_asset = strcmp(ImGui::GetDragDropPayload()->DataType, "DND_ASSET") == 0;
        if (is_dragging_asset)
        {
            const ImGuiPayload *payload = ImGui::GetDragDropPayload();
            Asset_Handle asset_handle = *(const Asset_Handle*)payload->Data;
            show_drag_widget = get_asset_info(asset_handle)->name == "model";
        }

        node = get_node(scene, node_index);
        if (node_index != 0 && show_drag_widget)
        {
            ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAvailWidth|ImGuiSelectableFlags_NoPadWithHalfSpacing;
            ImGui::Selectable("##DragAfterNode", true, flags, ImVec2(0.0f, 4.0f));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_SCENE_NODE"))
                {
                    U32 child_node_index = *(const U32*)payload->Data;
                    Scene_Node *child = get_node(scene, child_node_index);
                    remove_child(scene, child->parent_index, child_node_index);
                    add_child_after(scene, node_index, child_node_index);
                }

                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ASSET"))
                {
                    Asset_Handle asset_handle = *(const Asset_Handle*)payload->Data;
                    add_model_to_scene(scene, node_index, asset_handle, Add_Scene_Node_Operation::AFTER);
                }

                ImGui::EndDragDropTarget();
            }
        }
    }

    ImGui::PopID();
}

}