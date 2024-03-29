#include "inspector_panel.h"

#include <core/defines.h>
#include <rendering/renderer_types.h>
#include <core/memory.h>

#include <imgui/imgui.h>

#include "../editor_utils.h"

namespace Inspector_Panel
{

enum class Inspection_Type
{
    NONE,
    SCENE_NODE
};

union Inspection_Data
{
    Scene_Node *scene_node;
};

struct Inspector_State
{
    Inspection_Type type = Inspection_Type::NONE;
    Inspection_Data data = {};
    char input_buffer[128];
};

static Inspector_State inspector_state;

void inspect_scene_node(Scene_Node *scene_node);

void draw()
{
    ImGui::Begin("Inspector");

    switch (inspector_state.type)
    {
        case Inspection_Type::NONE:
        {

        } break;

        case Inspection_Type::SCENE_NODE:
        {
            inspect_scene_node(inspector_state.data.scene_node);
        } break;
    }

    ImGui::End();
}

void inspect(Scene_Node *scene_node)
{
    inspector_state.type = Inspection_Type::SCENE_NODE;
    inspector_state.data.scene_node = scene_node;
    memcpy(inspector_state.input_buffer, scene_node->name.data, scene_node->name.count);
}

static void draw_transform(Transform *transform)
{
    ImGui::Text("Position");
    ImGui::SameLine();
    ImGui::DragFloat3("###Position Drag Float3", &transform->position.x);

    ImGui::Text("Rotation");
    ImGui::SameLine();
    if (ImGui::DragFloat3("###Rotation Drag Float3", &transform->euler_angles.x))
    {
        transform->rotation = glm::quat(transform->euler_angles);
    }

    ImGui::Text("Scale");
    ImGui::SameLine();
    ImGui::DragFloat3("###Scale Drag Float3", &transform->scale.x);
}

static void inspect_scene_node(Scene_Node *scene_node)
{
    ImGui::Text("Node");
    ImGui::SameLine();

    if (ImGui::InputText("###EditNodeTextInput", inspector_state.input_buffer, sizeof(inspector_state.input_buffer), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemDeactivated())
        {
            String new_name = HE_STRING(inspector_state.input_buffer);
            if (scene_node->name.data && new_name.count)
            {
                deallocate(get_general_purpose_allocator(), (void *)scene_node->name.data);
                scene_node->name = copy_string(new_name, to_allocator(get_general_purpose_allocator()));
            }
        }
    }

    ImGui::Text("Transform");
    draw_transform(&scene_node->transform);

    select_asset(HE_STRING_LITERAL("Model Asset"), HE_STRING_LITERAL("model"), (Asset_Handle *)&scene_node->model_asset);
}

} // namespace Inspector_Panel