#include "inspector_panel.h"

#include <core/defines.h>
#include <rendering/renderer.h>
#include <core/memory.h>
#include <core/logging.h>

#include <assets/asset_manager.h>

#include <imgui/imgui.h>

#include "../editor_utils.h"
#include "../editor.h"

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
    struct
    {
        Asset_Handle scene_asset;
        S32 scene_node_index;
    };

    Asset_Handle asset_handle;
};

struct Inspector_State
{
    Inspection_Type type = Inspection_Type::NONE;
    Inspection_Data data = {};
    char rename_node_buffer[128];
};

static Inspector_State inspector_state;

void internal_inspect_scene_node(Scene_Node *scene_node);
void internal_inspect_asset(Asset_Handle asset_handle);

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
            Asset_Handle scene_asset = inspector_state.data.scene_asset;
            if (is_asset_handle_valid(scene_asset) && is_asset_loaded(scene_asset))
            {
                Scene_Handle scene_handle = get_asset_handle_as<Scene>(scene_asset);
                Scene *scene = renderer_get_scene(scene_handle);
                U32 node_index = inspector_state.data.scene_node_index;
                if (node_index >= 0 && node_index < scene->node_count)
                {
                    Scene_Node *node = get_node(scene, node_index);
                    internal_inspect_scene_node(node);
                }
            }
        } break;

        case Inspection_Type::ASSET:
        {
            internal_inspect_asset(inspector_state.data.asset_handle);
        } break;
    }

    ImGui::End();
}

void inspect_scene_node(Asset_Handle scene_asset, S32 scene_node_index)
{
    HE_ASSERT(is_asset_handle_valid(scene_asset));
    HE_ASSERT(scene_node_index != -1);

    Editor::reset_selection();

    inspector_state.type = Inspection_Type::SCENE_NODE;
    inspector_state.data.scene_asset = scene_asset;
    inspector_state.data.scene_node_index = scene_node_index;

    Scene_Handle scene_handle = get_asset_handle_as<Scene>(scene_asset);
    Scene *scene = renderer_get_scene(scene_handle);
    Scene_Node *node = get_node(scene, scene_node_index);
    memset(inspector_state.rename_node_buffer, 0, sizeof(inspector_state.rename_node_buffer));
    memcpy(inspector_state.rename_node_buffer, node->name.data, node->name.count);
}

void inspect_asset(Asset_Handle asset_handle)
{
    Editor::reset_selection();

    if (is_asset_handle_valid(asset_handle))
    {
        inspector_state.type = Inspection_Type::ASSET;
        release_asset(inspector_state.data.asset_handle);
        acquire_asset(asset_handle);
        inspector_state.data.asset_handle = asset_handle;
    }
    else
    {
        inspector_state.type = Inspection_Type::NONE;
    }
}

static void draw_transform(Transform *transform)
{
    ImGui::PushID(transform);

    ImGui::Text("Position");
    ImGui::SameLine();
    ImGui::DragFloat3("###Position Drag Float3", &transform->position.x, 0.1f);

    ImGui::Text("Rotation");
    ImGui::SameLine();
    if (ImGui::DragFloat3("###Rotation Drag Float3", &transform->euler_angles.x, 1, 0.0f, 360.0f))
    {
        transform->rotation = glm::quat(glm::radians(transform->euler_angles));
    }

    ImGui::Text("Scale");
    ImGui::SameLine();
    ImGui::DragFloat3("###Scale Drag Float3", &transform->scale.x, 0.1f);

    ImGui::PopID();
}

static void internal_inspect_scene_node(Scene_Node *scene_node)
{
    Memory_Context memory_context = grab_memory_context();

    ImGui::PushID(scene_node);

    ImGui::Text("Node");
    ImGui::SameLine();

    if (ImGui::InputText("###EditNodeTextInput", inspector_state.rename_node_buffer, sizeof(inspector_state.rename_node_buffer), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemDeactivated())
        {
            String new_name = HE_STRING(inspector_state.rename_node_buffer);
            if (scene_node->name.data && new_name.count)
            {
                HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, (void *)scene_node->name.data);
                scene_node->name = copy_string(new_name, memory_context.general_allocator);
            }
        }
    }
    else
    {
        memset(inspector_state.rename_node_buffer, 0, sizeof(inspector_state.rename_node_buffer));
        memcpy(inspector_state.rename_node_buffer, scene_node->name.data, scene_node->name.count);
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Spacing();
        draw_transform(&scene_node->transform);
    }

    ImGui::Separator();

    if (scene_node->has_mesh)
    {
        Static_Mesh_Component *mesh_comp = &scene_node->mesh;

        if (ImGui::Button("X##Static Mesh Component"))
        {
            scene_node->has_mesh = false;
        }

        ImGui::SameLine();

        if (ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Spacing();

            Render_Context render_context = get_render_context();
            Renderer_State *renderer_state = render_context.renderer_state;

            Asset_Handle static_mesh_asset = { .uuid = mesh_comp->static_mesh_asset };
            select_asset(HE_STRING_LITERAL("Static Mesh"), HE_STRING_LITERAL("static_mesh"), (Asset_Handle *)&mesh_comp->static_mesh_asset, { .load_on_select = true });

            if (is_asset_loaded(static_mesh_asset))
            {
                Static_Mesh_Handle static_mesh_handle = get_asset_handle_as<Static_Mesh>(static_mesh_asset);
                Static_Mesh *static_mesh = renderer_get_static_mesh(static_mesh_handle);

                ImGui::Spacing();

                if (ImGui::TreeNode("Materials"))
                {
                    ImGui::Spacing();

                    for (U32 i = 0; i < mesh_comp->materials.count; i++)
                    {
                        ImGui::PushID(i);
                        select_asset(HE_STRING_LITERAL("Material"), HE_STRING_LITERAL("material"), (Asset_Handle*)&mesh_comp->materials[i], { .load_on_select = true });
                        ImGui::PopID();
                    }

                    ImGui::TreePop();
                }
            }
        }
    }

    ImGui::Separator();

    if (scene_node->has_light)
    {
        Light_Component *light = &scene_node->light;
        
        if (ImGui::Button("X##Light Component"))
        {
            scene_node->has_light = false;
        }
        ImGui::SameLine();

        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Spacing();

            const char* light_types[] = { "Directional", "Point", "Spot" };

            ImGui::Text("Type");
            ImGui::SameLine();

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
            ImGui::SameLine();
            ImGui::ColorEdit3("##ColorEdit3", &light->color.r);

            ImGui::Text("Intensity");
            ImGui::SameLine();
            ImGui::DragFloat("##IntensityDragFloat", &light->intensity, 0.1f, 0.0f);

            if (light->type != Light_Type::DIRECTIONAL)
            {
                ImGui::Text("Radius");
                ImGui::SameLine();
                ImGui::DragFloat("##RadiusDragFloat", &light->radius, 0.1f, 0.0f);
            }

            if (light->type == Light_Type::SPOT)
            {
                ImGui::Text("Outer Angle");
                ImGui::SameLine();

                ImGui::DragFloat("##Outer Angle Drag Float", &light->outer_angle, 1.0f, 0.0f, 360.0f);

                ImGui::Text("Inner Angle");
                ImGui::SameLine();

                ImGui::DragFloat("##Inner Angle Drag Float", &light->inner_angle, 1.0f, 0.0f, light->outer_angle);
            }
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

    ImGui::PopID();
}

static void inspect_material(Asset_Handle material_asset)
{
    ImGui::PushID((void *)material_asset.uuid);
    HE_DEFER { ImGui::PopID(); };

    const Asset_Registry_Entry &entry = get_asset_registry_entry(material_asset);
    Asset_Handle shader_asset = entry.parent;

    Material_Handle material_handle = get_asset_handle_as<Material>(material_asset);
    Material *material = renderer_get_material(material_handle);

    Pipeline_State *pipeline_state = renderer_get_pipeline_state(material->pipeline_state_handle);
    Material_Type &type = material->type;

    Pipeline_State_Settings &settings = pipeline_state->settings;

    bool shader_changed = select_asset(HE_STRING_LITERAL("Shader"), HE_STRING_LITERAL("shader"), &shader_asset, { .nullify = false, .load_on_select = true });
    if (shader_changed)
    {
        set_parent(material_asset, shader_asset);
    }

    bool pipeline_changed = false;

    static constexpr const char *types[] = { "opaque", "alpha cutoff", "transparent" };

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
                pipeline_changed = true;
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

    if (ImGui::BeginCombo("##Cull Mode", cull_modes[(U32)settings.cull_mode]))
    {
        for (U32 i = 0; i < HE_ARRAYCOUNT(cull_modes); i++)
        {
            bool is_selected = i == (U32)settings.cull_mode;
            if (ImGui::Selectable(cull_modes[i], is_selected))
            {
                settings.cull_mode = (Cull_Mode)i;
                pipeline_changed = true;
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
                pipeline_changed = true;
            }

            if (is_selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    static constexpr const char *compare_ops[] = { "never", "less", "equal", "less or equal", "greater", "not equal", "greater or equal", "always" };

    if (ImGui::CollapsingHeader("Depth Settings"))
    {
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
                    pipeline_changed = true;
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
        pipeline_changed |= ImGui::Checkbox("##Depth Testing", &settings.depth_testing);

        ImGui::Text("Depth Writing");
        ImGui::SameLine();
        pipeline_changed |= ImGui::Checkbox("##Depth Writing", &settings.depth_writing);
    }

    if (ImGui::CollapsingHeader("Stencil Settings"))
    {
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
                    pipeline_changed = true;
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
        pipeline_changed |= ImGui::Checkbox("##Stencil Testing", &settings.stencil_testing);

        static constexpr const char* stencil_ops[] = { "keep", "zero", "replace", "increment and clamp", "decrement and clamp", "invert", "increment and wrap", "decrement and wrap" };

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
                    pipeline_changed = true;
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
                    pipeline_changed = true;
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
                    pipeline_changed = true;
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
        pipeline_changed |= ImGui::DragInt("##Stencil Compare Mask", (int*)&settings.stencil_compare_mask, 1.0f, 0, 255);

        ImGui::Text("Stencil Write Mask");
        ImGui::SameLine();
        pipeline_changed |= ImGui::DragInt("##Stencil Write Mask", (int*)&settings.stencil_write_mask, 1.0f, 0, 255);

        ImGui::Text("Stencil Reference Value");
        ImGui::SameLine();
        pipeline_changed |= ImGui::DragInt("##Stencil Reference Value", (int*)&settings.stencil_reference_value, 1.0f, 0, 255);
    }

    if (ImGui::CollapsingHeader("Blending"))
    {
        ImGui::Text("Write Color Mask");
        ImGui::SameLine();

        bool r = settings.color_mask & COLOR_MASK_R;
        bool g = settings.color_mask & COLOR_MASK_G;
        bool b = settings.color_mask & COLOR_MASK_B;
        bool a = settings.color_mask & COLOR_MASK_A;

        ImGui::Text("R");
        ImGui::SameLine();
        pipeline_changed |= ImGui::Checkbox("##R", &r);
        ImGui::SameLine();

        ImGui::Text("G");
        ImGui::SameLine();
        pipeline_changed |= ImGui::Checkbox("##G", &g);
        ImGui::SameLine();

        ImGui::Text("B");
        ImGui::SameLine();
        pipeline_changed |= ImGui::Checkbox("##B", &b);
        ImGui::SameLine();

        ImGui::Text("A");
        ImGui::SameLine();
        pipeline_changed |= ImGui::Checkbox("##A", &a);

        settings.color_mask = (Color_Write_Mask)0;

        if (r)
        {
            settings.color_mask = (Color_Write_Mask)(settings.color_mask|COLOR_MASK_R);
        }

        if (g)
        {
            settings.color_mask = (Color_Write_Mask)(settings.color_mask|COLOR_MASK_G);
        }

        if (b)
        {
            settings.color_mask = (Color_Write_Mask)(settings.color_mask|COLOR_MASK_B);
        }

        if (a)
        {
            settings.color_mask = (Color_Write_Mask)(settings.color_mask|COLOR_MASK_A);
        }

        ImGui::Text("Alpha Blending");
        ImGui::SameLine();

        pipeline_changed |= ImGui::Checkbox("##Alpha Blending", &settings.alpha_blending);
    }

    bool property_changed = false;

    if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (U32 i = 0; i < material->properties.count; i++)
        {
            ImGui::PushID(i);
            Material_Property* property = &material->properties[i];

            ImGui::Text(property->name.data);
            ImGui::SameLine();

            Material_Property_Data data = property->data;

            switch (property->data_type)
            {
                case Shader_Data_Type::U32:
                {
                    if (property->is_texture_asset)
                    {
                        String title = HE_STRING_LITERAL("Texture");
                        String type = HE_STRING_LITERAL("texture");
                        bool is_hdr_texture_asset = ends_with(property->name, HE_STRING_LITERAL("cubemap"));
                        if (is_hdr_texture_asset)
                        {
                            title = HE_STRING_LITERAL("HDR Map");
                            type = HE_STRING_LITERAL("environment_map");
                        }
                        property_changed |= select_asset(title, type, (Asset_Handle*)&data.u64);
                    }
                    else
                    {
                        property_changed |= ImGui::DragInt("##MaterialPropertyDragInt", (S32*)&data.u32, 0, HE_MAX_S32);
                    }
                } break;

                case Shader_Data_Type::F32:
                {
                    property_changed |= ImGui::DragFloat("##MaterialPropertyDragFloat", &data.f32, 0.1f);
                } break;

                case Shader_Data_Type::VECTOR2F:
                {
                    property_changed |= ImGui::DragFloat2("##MaterialPropertyDragFloat2", &data.v2f.x, 0.1f);
                } break;

                case Shader_Data_Type::VECTOR3F:
                {
                    if (property->is_color)
                    {
                        property_changed |= ImGui::ColorEdit3("##MaterialPropertyColorEdit3", &data.v3f.x);
                    }
                    else
                    {
                        property_changed |= ImGui::DragFloat3("##MaterialPropertyDragFloat3", &data.v3f.x, 0.1f);
                    }
                } break;

                case Shader_Data_Type::VECTOR4F:
                {
                    if (property->is_color)
                    {
                        property_changed |= ImGui::ColorEdit4("##MaterialPropertyColorEdit4", &data.v4f.x);
                    }
                    else
                    {
                        property_changed |= ImGui::DragFloat4("##MaterialPropertyDragFloat4", &data.v4f.x, 0.1f);
                    }
                } break;
            }

            if (property_changed)
            {
                set_property(material_handle, property->name, data);
            }

            ImGui::PopID();
        }
    }

    if (shader_changed || pipeline_changed || property_changed)
    {
        Memory_Context memory_context = grab_memory_context();

        const Asset_Registry_Entry &entry = get_asset_registry_entry(material_asset);
        String path = format_string(memory_context.temp_allocator, "%.*s/%.*s", HE_EXPAND_STRING(get_asset_path()), HE_EXPAND_STRING(entry.path));
        bool success = serialize_material(material_handle, shader_asset.uuid, path);
        if (!success)
        {
            HE_LOG(Assets, Error, "failed to save material asset: %.*s\n", HE_EXPAND_STRING(entry.path));
        }
    }
}

static void internal_inspect_asset(Asset_Handle asset_handle)
{
    if (!is_asset_loaded(asset_handle))
    {
        return;
    }

    ImGui::BeginDisabled(is_asset_embeded(asset_handle));

    const Asset_Info *info = get_asset_info(asset_handle);
    const Asset_Registry_Entry &entry = get_asset_registry_entry(asset_handle);

    ImGui::Text("%.*s", HE_EXPAND_STRING(entry.path));

    String material_lit = HE_STRING_LITERAL("material");
    if (info->name == material_lit)
    {
        inspect_material(asset_handle);
    }

    ImGui::EndDisabled();
}

} // namespace Inspector_Panel