#include "assets/scene_importer.h"

#include <rendering/renderer.h>
#include <core/file_system.h>
#include <core/logging.h>

bool deserialize_transform(String *str, Transform *t);
bool deserialize_light(String *str, Light_Component *light);

enum class Handle_Assets_Method
{
    ACQUIRE,
    RELEASE
};

static void handle_scene_assets(Scene *scene, S32 node_index, Handle_Assets_Method method)
{
    Scene_Node *node = get_node(scene, node_index);

    if (node->has_mesh)
    {
        Static_Mesh_Component *static_mesh_comp = &node->mesh;
        Asset_Handle static_mesh_asset = { .uuid = static_mesh_comp->static_mesh_asset };

        if (method == Handle_Assets_Method::ACQUIRE)
        {
            acquire_asset(static_mesh_asset);

            for (U32 material_index = 0; material_index < static_mesh_comp->materials.count; material_index++)
            {
                Asset_Handle material_asset = { .uuid = static_mesh_comp->materials[material_index] };
                acquire_asset(material_asset);
            }
        }
        else
        {
            release_asset(static_mesh_asset);

            for (U32 material_index = 0; material_index < static_mesh_comp->materials.count; material_index++)
            {
                Asset_Handle material_asset = { .uuid = static_mesh_comp->materials[material_index] };
                release_asset(material_asset);
            }
        }
    }

    for (S32 child_node_index = node->first_child_index; child_node_index != -1; child_node_index = get_node(scene, child_node_index)->next_sibling_index)
    {
        handle_scene_assets(scene, child_node_index, method);
    }
}

Load_Asset_Result load_scene(String path, const Embeded_Asset_Params *params)
{
    Memory_Context memory_context = grab_memory_context();

    Read_Entire_File_Result read_result = read_entire_file(path, memory_context.temp_allocator);
    if (!read_result.success)
    {
        HE_LOG(Assets, Error, "failed to parse scene asset\n");
        return {};
    }
    
    String contents = { .count = read_result.size, .data = (const char *)read_result.data };
    String str = eat_white_space(contents);
    Parse_Name_Value_Result result = parse_name_value(&str, HE_STRING_LITERAL("version"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "failed to parse scene asset\n");
        return {};
    }
    U64 version = str_to_u64(result.value);

    glm::vec3 ambient_color = {};

    {
        Parse_Name_Float3_Result result = parse_name_float3(&str, HE_STRING_LITERAL("ambient_color"));
        if (!result.success)
        {
            HE_LOG(Assets, Error, "failed to parse scene asset\n");
            return {};
        }
        ambient_color = { result.values[0], result.values[1], result.values[2] };
    }

    result = parse_name_value(&str, HE_STRING_LITERAL("skybox_material_asset"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "failed to parse scene asset\n");
        return {};
    }

    Asset_Handle skybox_material = { .uuid = str_to_u64(result.value) };
    
    result = parse_name_value(&str, HE_STRING_LITERAL("node_count"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "failed to parse scene asset\n");
        return {};
    }

    U32 node_count = u64_to_u32(str_to_u64(result.value));
    Scene_Handle scene_handle = renderer_create_scene(node_count);
    Scene *scene = renderer_get_scene(scene_handle);
    Skybox *skybox = &scene->skybox;
    skybox->ambient_color = ambient_color;
    skybox->skybox_material_asset = skybox_material.uuid;

    for (U32 node_index = 0; node_index < node_count; node_index++)
    {
        result = parse_name_value(&str, HE_STRING_LITERAL("node_name"));
        if (!result.success)
        {
            renderer_destroy_scene(scene_handle);
            HE_LOG(Assets, Error, "failed to parse scene asset\n");
            return {};
        }

        U64 name_count = str_to_u64(result.value);
        String name = sub_string(str, 0, name_count);
        str = advance(str, name_count);

        result = parse_name_value(&str, HE_STRING_LITERAL("parent"));
        if (!result.success)
        {
            renderer_destroy_scene(scene_handle);
            HE_LOG(Assets, Error, "failed to parse scene asset\n");
            return {};
        }

        S32 parent_index = (S32)str_to_s64(result.value);

        result = parse_name_value(&str, HE_STRING_LITERAL("component_count"));
        if (!result.success)
        {
            renderer_destroy_scene(scene_handle);
            HE_LOG(Assets, Error, "failed to parse scene asset\n");
            return {};
        }

        allocate_node(scene, name);
        Scene_Node *node = get_node(scene, node_index);

        if (parent_index != -1)
        {
            add_child_last(scene, parent_index, node_index);
        }

        U32 component_count = u64_to_u32(str_to_u64(result.value));

        for (U32 component_index = 0; component_index < component_count; component_index++)
        {
            result = parse_name_value(&str, HE_STRING_LITERAL("component"));
            if (!result.success)
            {
                renderer_destroy_scene(scene_handle);
                HE_LOG(Assets, Error, "failed to parse scene asset\n");
                return {};
            }

            String type = result.value;
            if (type == "transform")
            {
                if (!deserialize_transform(&str, &node->transform))
                {
                    renderer_destroy_scene(scene_handle);
                    HE_LOG(Assets, Error, "failed to parse scene asset\n");
                    return {};
                }
            }
            else if (type == "mesh")
            {
                result = parse_name_value(&str, HE_STRING_LITERAL("static_mesh_asset"));
                if (!result.success)
                {
                    renderer_destroy_scene(scene_handle);
                    HE_LOG(Assets, Error, "failed to parse scene asset\n");
                    return {};
                }

                node->has_mesh = true;
                U64 static_mesh_asset = str_to_u64(result.value);
                Static_Mesh_Component *static_mesh_comp = &node->mesh;
                static_mesh_comp->static_mesh_asset = static_mesh_asset;

                result = parse_name_value(&str, HE_STRING_LITERAL("material_count"));
                if (!result.success)
                {
                    renderer_destroy_scene(scene_handle);
                    HE_LOG(Assets, Error, "failed to parse scene asset\n");
                    return {};
                }

                U32 material_count = u64_to_u32(str_to_u64(result.value));

                if (!static_mesh_comp->materials.data)
                {
                    init(&static_mesh_comp->materials);
                }

                set_count(&static_mesh_comp->materials, material_count);
                
                for (U32 i = 0; i < material_count; i++)
                {
                    result = parse_name_value(&str, HE_STRING_LITERAL("material_asset"));
                    if (!result.success)
                    {
                        renderer_destroy_scene(scene_handle);
                        HE_LOG(Assets, Error, "failed to parse scene asset\n");
                        return {};
                    }
                    U64 material_asset_uuid = str_to_u64(result.value);
                    static_mesh_comp->materials[i] = material_asset_uuid;

                }
            }
            else if (type == "light")
            {
                node->has_light = true;
                if (!deserialize_light(&str, &node->light))
                {
                    renderer_destroy_scene(scene_handle);
                    HE_LOG(Assets, Error, "failed to parse scene asset\n");
                    return {};
                }
            }
        }
    }

    acquire_asset(skybox_material);
    handle_scene_assets(scene, 0, Handle_Assets_Method::ACQUIRE);

    return { .success = true, .index = scene_handle.index, .generation = scene_handle.generation };
}

void unload_scene(Load_Asset_Result load_result)
{
    Scene_Handle scene_handle = { .index = load_result.index, .generation = load_result.generation };

    Scene *scene = renderer_get_scene(scene_handle);
    Asset_Handle skybox_material_asset = { .uuid = scene->skybox.skybox_material_asset };
    release_asset(skybox_material_asset);
    handle_scene_assets(scene, 0, Handle_Assets_Method::RELEASE);

    renderer_destroy_scene(scene_handle);
}

static bool deserialize_transform(String *str, Transform *t)
{
    glm::vec3 &p = t->position;
    glm::quat &r = t->rotation;
    glm::vec3 &s = t->scale;

    String position_lit = HE_STRING_LITERAL("position");
    if (!starts_with(*str, position_lit))
    {
        return false;
    }

    *str = advance(*str, position_lit.count);
    *str = eat_white_space(*str);

    for (U32 i = 0; i < 3; i++)
    {
        String value = eat_none_white_space(str);
        p[i] = str_to_f32(value);
        *str = eat_white_space(*str);
    }

    String rotation_lit = HE_STRING_LITERAL("rotation");
    if (!starts_with(*str, rotation_lit))
    {
        return false;
    }

    *str = advance(*str, rotation_lit.count);
    *str = eat_white_space(*str);

    glm::vec4 rv = {};

    for (U32 i = 0; i < 4; i++)
    {
        String value = eat_none_white_space(str);
        rv[i] = str_to_f32(value);
        *str = eat_white_space(*str);
    }

    r = glm::quat(rv.w, rv.x, rv.y, rv.z);
    t->euler_angles = glm::degrees(glm::eulerAngles(r));

    String scale_lit = HE_STRING_LITERAL("scale");
    if (!starts_with(*str, scale_lit))
    {
        return false;
    }

    *str = advance(*str, scale_lit.count);
    *str = eat_white_space(*str);

    for (U32 i = 0; i < 3; i++)
    {
        String value = eat_none_white_space(str);
        s[i] = str_to_f32(value);
        *str = eat_white_space(*str);
    }

    return true;
}

static Light_Type str_to_light_type(String str)
{
    if (str == "directional")
    {
        return Light_Type::DIRECTIONAL;
    }
    else if (str == "point")
    {
        return Light_Type::POINT;
    }
    else if (str == "spot")
    {
        return Light_Type::SPOT;
    }

    HE_ASSERT(!"unsupported light type");
    return (Light_Type)0;
}

static bool deserialize_light(String *str, Light_Component *light)
{
    Parse_Name_Value_Result result = parse_name_value(str, HE_STRING_LITERAL("type"));
    if (!result.success)
    {
        return false;
    }

    Light_Type type = str_to_light_type(result.value);

    String color_lit = HE_STRING_LITERAL("color");
    if (!starts_with(*str, color_lit))
    {
        return false;
    }
    *str = advance(*str, color_lit.count);

    glm::vec3 color = {};

    for (U32 i = 0; i < 3; i++)
    {
        *str = eat_white_space(*str);
        String value = eat_none_white_space(str);
        color[i] = str_to_f32(value);
        *str = eat_white_space(*str);
    }

    result = parse_name_value(str, HE_STRING_LITERAL("intensity"));
    if (!result.success)
    {
        return false;
    }
    F32 intensity = str_to_f32(result.value);

    result = parse_name_value(str, HE_STRING_LITERAL("radius"));
    if (!result.success)
    {
        return false;
    }

    F32 radius = str_to_f32(result.value);

    result = parse_name_value(str, HE_STRING_LITERAL("inner_angle"));
    if (!result.success)
    {
        return false;
    }

    F32 inner_angle = str_to_f32(result.value);

    result = parse_name_value(str, HE_STRING_LITERAL("outer_angle"));
    if (!result.success)
    {
        return false;
    }

    F32 outer_angle = str_to_f32(result.value);

    light->type = type;
    light->color = color;
    light->intensity = intensity;
    light->radius = radius;
    light->inner_angle = inner_angle;
    light->outer_angle = outer_angle;
    return true;
}