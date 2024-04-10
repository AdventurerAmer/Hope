#include "inspector_panel.h"

#include <core/defines.h>
#include <rendering/renderer.h>
#include <core/memory.h>

#include <imgui/imgui.h>

#include "../editor_utils.h"

namespace Inspector_Panel
{

enum class Inspection_Type
{
    NONE,
    SCENE_NODE,
    ASSET,
};

union Inspection_Data
{
    Scene_Node *scene_node;
    Asset_Handle asset_handle;
};

struct Inspector_State
{
    Inspection_Type type = Inspection_Type::NONE;
    Inspection_Data data = {};
    char input_buffer[128];
};

static Inspector_State inspector_state;

void inspect_scene_node(Scene_Node *scene_node);
void inspect_asset(Asset_Handle asset_handle);

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

        case Inspection_Type::ASSET:
        {
            inspect_asset(inspector_state.data.asset_handle);
        } break;
    }

    ImGui::End();
}

void inspect(Scene_Node *scene_node)
{
    inspector_state.type = Inspection_Type::SCENE_NODE;
    inspector_state.data.scene_node = scene_node;
    memset(inspector_state.input_buffer, 0, sizeof(inspector_state.input_buffer));
    memcpy(inspector_state.input_buffer, scene_node->name.data, scene_node->name.count);
}

void inspect(Asset_Handle asset_handle)
{
    if (is_asset_handle_valid(asset_handle))
    {
        inspector_state.type = Inspection_Type::ASSET;
        inspector_state.data.asset_handle = asset_handle;
    }
    else
    {
        inspector_state.type = Inspection_Type::NONE;
    }
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
        transform->rotation = glm::quat(glm::radians(transform->euler_angles));
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
    else
    {
        memset(inspector_state.input_buffer, 0, sizeof(inspector_state.input_buffer));
        memcpy(inspector_state.input_buffer, scene_node->name.data, scene_node->name.count);
    }

    ImGui::Separator();

    ImGui::Text("Transform");
    ImGui::Spacing();

    draw_transform(&scene_node->transform);

    ImGui::Separator();

    if (scene_node->has_mesh)
    {
        Static_Mesh_Component *mesh_comp = &scene_node->mesh;
        ImGui::Text("Static Mesh Component");
        ImGui::SameLine();

        if (ImGui::Button("X##Static Mesh Component"))
        {
            scene_node->has_mesh = false;
        }

        ImGui::Spacing();

        Render_Context render_context = get_render_context();
        Renderer_State *renderer_state = render_context.renderer_state;

        Asset_Handle static_mesh_asset = { .uuid = mesh_comp->static_mesh_asset };
        select_asset(HE_STRING_LITERAL("Static Mesh"), HE_STRING_LITERAL("static_mesh"), (Asset_Handle *)&mesh_comp->static_mesh_asset);

        if (is_asset_handle_valid(static_mesh_asset))
        {
            if (!is_asset_loaded(static_mesh_asset))
            {
                aquire_asset(static_mesh_asset);
            }
            else
            {
                Static_Mesh_Handle static_mesh_handle = get_asset_handle_as<Static_Mesh>(static_mesh_asset);
                Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

                ImGui::Spacing();

                ImGui::Text("Materials");
                ImGui::Spacing();

                for (U32 i = 0; i < static_mesh->sub_meshes.count; i++)
                {
                    ImGui::PushID(i);
                    Sub_Mesh *sub_mesh = &static_mesh->sub_meshes[i];
                    select_asset(HE_STRING_LITERAL("Material"), HE_STRING_LITERAL("material"), (Asset_Handle *)&sub_mesh->material_asset);
                    ImGui::PopID();
                }
            }
        }
    }

    ImGui::Separator();

    if (scene_node->has_light)
    {
        Light_Component *light = &scene_node->light;
        ImGui::Text("Light Component");
        ImGui::SameLine();

        if (ImGui::Button("X##Light Component"))
        {
            scene_node->has_light = false;
        }

        ImGui::Spacing();

        const char *light_types[] = { "Directional", "Point", "Spot" };

        if (ImGui::BeginCombo("##LightType", light_types[(U8)light->type]))
        {
            for (U32 i = 0; i < HE_ARRAYCOUNT(light_types); i++)
            {
                bool is_selected = i == (U8)light->type;
                if (ImGui::Selectable(light_types[i], is_selected))
                {
                    light->type = (Light_Type)i;
                }

                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Text("Color");
        ImGui::ColorEdit3("##ColorEdit3", &light->color.r);

        ImGui::Text("Intensity");
        ImGui::DragFloat("##IntensityDragFloat", &light->intensity, 0.1f, 0.0f);

        if (light->type != Light_Type::DIRECTIONAL)
        {
            ImGui::Text("Radius");
            ImGui::DragFloat("##RadiusDragFloat", &light->radius, 0.1f, 0.0f);
        }

        if (light->type == Light_Type::SPOT)
        {
            ImGui::Text("Outer Angle");
            ImGui::SameLine();

            ImGui::DragFloat("##Outer Angle Drag Float", &light->outer_angle, 1.0f, 0.0f, 360.0f);

            ImGui::Text("Inner Angle");
            ImGui::SameLine();

            ImGui::DragFloat("##Inner Angle Drag Float", &light->inner_angle, 1.0f, 0.0f, 360.0f);
        }
    }

    if (ImGui::BeginPopupContextWindow())
    {
        if (!scene_node->has_mesh)
        {
            if (ImGui::MenuItem("Add Mesh"))
            {
                scene_node->has_mesh = true;

                Static_Mesh_Component *mesh_comp = &scene_node->mesh;
                mesh_comp->static_mesh_asset = {};
            }
        }

        if (!scene_node->has_light)
        {
            if (ImGui::MenuItem("Add Light"))
            {
                scene_node->has_light = true;
                Light_Component *light = &scene_node->light;
                light->type = Light_Type::DIRECTIONAL;
                light->radius = 1.0f;
                light->outer_angle = 45.0f;
                light->inner_angle = 30.0f;
                light->color = { 1.0f, 1.0f, 1.0f };
                light->intensity = 1.0f;
            }
        }

        ImGui::EndPopup();
    }
}

static void inspect_material(Material_Handle material_handle, Asset_Handle shader_asset)
{
    select_asset(HE_STRING_LITERAL("Shader"), HE_STRING_LITERAL("shader"), &shader_asset, { .nullify = false });

    Material *material = renderer_get_material(material_handle);
    for (U32 i = 0; i < material->properties.count; i++)
    {
        ImGui::PushID(i);
        Material_Property *property = &material->properties[i];

        ImGui::Text(property->name.data);
        ImGui::SameLine();

        Material_Property_Data data = property->data;
        bool changed = true;

        switch (property->data_type)
        {
            case Shader_Data_Type::U32:
            {
                if (property->is_texture_asset)
                {
                    bool is_skybox_asset = ends_with(property->name, HE_STRING_LITERAL("cubemap"));
                    if (is_skybox_asset)
                    {
                        changed &= select_asset(HE_STRING_LITERAL("Skybox"), HE_STRING_LITERAL("skybox"), (Asset_Handle *)&data.u64);
                    }
                    else
                    {
                        changed &= select_asset(HE_STRING_LITERAL("Texture"), HE_STRING_LITERAL("texture"), (Asset_Handle *)&data.u64);
                    }
                }
                else
                {
                    changed &= ImGui::DragInt("##MaterialPropertyDragInt", (S32 *) &data.u32, 0, HE_MAX_S32);
                }
            } break;

            case Shader_Data_Type::F32:
            {
                changed &= ImGui::DragFloat("##MaterialPropertyDragFloat", &data.f32);
            } break;

            case Shader_Data_Type::VECTOR2F:
            {
                changed &= ImGui::DragFloat2("##MaterialPropertyDragFloat2", &data.v2.x);
            } break;

            case Shader_Data_Type::VECTOR3F:
            {
                if (property->is_color)
                {
                    changed &= ImGui::ColorEdit3("##MaterialPropertyColorEdit3", &data.v3.x);
                }
                else
                {
                    changed &= ImGui::DragFloat3("##MaterialPropertyDragFloat3", &data.v3.x);
                }
            } break;

            case Shader_Data_Type::VECTOR4F:
            {
                if (property->is_color)
                {
                    changed &= ImGui::ColorEdit3("##MaterialPropertyColorEdit3", &data.v4.x);
                }
                else
                {
                    changed &= ImGui::DragFloat4("##MaterialPropertyDragFloat4", &data.v4.x);
                }
            } break;
        }

        if (changed)
        {
            set_property(material_handle, property->name, data);
        }

        ImGui::PopID();
    }
}

static void inspect_asset(Asset_Handle asset_handle)
{
    if (!is_asset_loaded(asset_handle))
    {
        aquire_asset(asset_handle);
        return;
    }

    ImGui::BeginDisabled(is_asset_embeded(asset_handle));

    const Asset_Info *info = get_asset_info(asset_handle);
    const Asset_Registry_Entry &entry = get_asset_registry_entry(asset_handle);

    ImGui::Text("%.*s", HE_EXPAND_STRING(entry.path));

    String material_lit = HE_STRING_LITERAL("material");
    if (info->name == material_lit)
    {
        Material_Handle material_handle = get_asset_handle_as<Material>(asset_handle);
        inspect_material(material_handle, entry.parent);
    }

    ImGui::EndDisabled();
}

} // namespace Inspector_Panel