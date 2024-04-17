#include "assets/material_importer.h"

#include "core/logging.h"
#include "core/file_system.h"

#include "rendering/renderer.h"

static constexpr const char* cull_modes[] = { "none", "front", "back" };

static Cull_Mode str_to_cull_mode(String str)
{
    for (U32 i = 0; i < HE_ARRAYCOUNT(cull_modes); i++)
    {
        if (str == cull_modes[i])
        {
            return (Cull_Mode)i;
        }
    }
    HE_ASSERT(!"unsupported cull mode");
    return Cull_Mode::NONE;
}

static constexpr const char* front_faces[] = { "clockwise", "counterclockwise"};

static Front_Face str_to_front_face(String str)
{
    for (U32 i = 0; i < HE_ARRAYCOUNT(front_faces); i++)
    {
        if (str == front_faces[i])
        {
            return (Front_Face)i;
        }
    }

    HE_ASSERT(!"unsupported front face");
    return Front_Face::CLOCKWISE;
}

Load_Asset_Result load_material(String path, const Embeded_Asset_Params *params)
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    Read_Entire_File_Result file_result = read_entire_file(path, to_allocator(scratch_memory.arena));

    if (!file_result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to read file: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    String white_space = HE_STRING_LITERAL(" \n\t\r\v\f");
    String str = { .count = file_result.size, .data = (const char *)file_result.data };

    Parse_Name_Value_Result result = parse_name_value(&str, HE_STRING_LITERAL("version"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }
    String version = result.value;
    U64 version_value = str_to_u64(version);

    result = parse_name_value(&str, HE_STRING_LITERAL("shader"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }
    
    Asset_Handle shader_asset = { .uuid = str_to_u64(result.value) };

    result = parse_name_value(&str, HE_STRING_LITERAL("render_pass"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    String render_pass = result.value;

    result = parse_name_value(&str, HE_STRING_LITERAL("cull_mode"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    Cull_Mode cull_mode = str_to_cull_mode(result.value);

    result = parse_name_value(&str, HE_STRING_LITERAL("front_face"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }
    
    Front_Face front_face = str_to_front_face(result.value);

    result = parse_name_value(&str, HE_STRING_LITERAL("depth_testing"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }
    
    bool depth_testing = result.value == "enabled";

    result = parse_name_value(&str, HE_STRING_LITERAL("property_count"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    U32 property_count = u64_to_u32(str_to_u64(result.value));

    Material_Property *material_properties = nullptr;

    if (property_count)
    {
         material_properties = HE_ALLOCATE_ARRAY(scratch_memory.arena, Material_Property, property_count);

        for (U32 i = 0; i < property_count; i++)
        {
            Material_Property *property = &material_properties[i];

            str = eat_chars(str, white_space);
            S64 index = find_first_char_from_left(str, white_space);
            if (index == -1)
            {
                HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
                return {};
            }
            String name = sub_string(str, 0, index);
            str = advance(str, name.count);
            str = eat_chars(str, white_space);

            index = find_first_char_from_left(str, white_space);
            if (index == -1)
            {
                HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
                return {};
            }
            
            String type = sub_string(str, 0, index);
            str = advance(str, type.count);
            str = eat_chars(str, white_space);

            Shader_Data_Type data_type = (Shader_Data_Type)u64_to_u32(str_to_u64(type));
            
            bool is_texture_asset = (ends_with(name, HE_STRING_LITERAL("texture")) || ends_with(name, HE_STRING_LITERAL("cubemap"))) && data_type == Shader_Data_Type::U32;

            Material_Property_Data data = {};

            switch (data_type)
            {
                case Shader_Data_Type::U32:
                {
                    index = find_first_char_from_left(str, white_space);
                    if (index == -1)
                    {
                        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
                        return {};
                    }
                    String value = sub_string(str, 0, index);
                    str = advance(str, value.count);
                    str = eat_chars(str, white_space);
                    U64 value_u64 = str_to_u64(value);
                    if (is_texture_asset)
                    {
                        data.u64 = value_u64;
                    }
                    else
                    {
                        data.u32 = u64_to_u32(value_u64);
                    }
                } break;

                case Shader_Data_Type::F32:
                {
                    index = find_first_char_from_left(str, white_space);
                    if (index == -1)
                    {
                        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
                        return {};
                    }
                    String value = sub_string(str, 0, index);
                    str = advance(str, value.count);
                    str = eat_chars(str, white_space);
                    F32 value_f32 = str_to_f32(value);
                    data.f32 = value_f32;
                } break;

                case Shader_Data_Type::VECTOR2F:
                {
                    for (U32 i = 0; i < 2; i++)
                    {
                        index = find_first_char_from_left(str, white_space);
                        if (index == -1)
                        {
                            HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
                            return {};
                        }
                        String value = sub_string(str, 0, index);
                        str = advance(str, value.count);
                        str = eat_chars(str, white_space);
                        F32 value_f32 = str_to_f32(value);
                        data.v2[i] = value_f32;
                    }
                } break;

                case Shader_Data_Type::VECTOR3F:
                {
                    for (U32 i = 0; i < 3; i++)
                    {
                        index = find_first_char_from_left(str, white_space);
                        if (index == -1)
                        {
                            HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
                            return {};
                        }
                        String value = sub_string(str, 0, index);
                        str = advance(str, value.count);
                        str = eat_chars(str, white_space);
                        F32 value_f32 = str_to_f32(value);
                        data.v3[i] = value_f32;
                    }
                } break;

                case Shader_Data_Type::VECTOR4F:
                {
                    for (U32 i = 0; i < 4; i++)
                    {
                        index = find_first_char_from_left(str, white_space);
                        if (index == -1)
                        {
                            HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
                            return {};
                        }
                        String value = sub_string(str, 0, index);
                        str = advance(str, value.count);
                        str = eat_chars(str, white_space);
                        F32 value_f32 = str_to_f32(value);
                        data.v4[i] = value_f32;
                    }
                } break;
            }

            property->name = name;
            property->data = data;
            property->data_type = data_type;
            property->is_texture_asset = is_texture_asset;
        }
    }

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;

    Shader_Handle shader_handle = get_asset_handle_as<Shader>(shader_asset);
    
    Pipeline_State_Descriptor pipeline_state_descriptor
    {
        .settings = 
        {
            .cull_mode = cull_mode,
            .front_face = front_face,
            .fill_mode = Fill_Mode::SOLID,
            .depth_func = Depth_Func::LESS_OR_EQUAL,
            .depth_testing = depth_testing,
            .depth_writing = false,
            .sample_shading = true,
        },
        .shader = shader_handle,
        .render_pass = get_render_pass(&renderer_state->render_graph, render_pass),
    };

    Pipeline_State_Handle pipeline_state_handle = renderer_create_pipeline_state(pipeline_state_descriptor);

    Material_Descriptor material_descriptor =
    {
        .name = copy_string(path, to_allocator(get_general_purpose_allocator())),
        .pipeline_state_handle = pipeline_state_handle
    };

    Material_Handle material_handle = renderer_create_material(material_descriptor);
    Material *material = renderer_get_material(material_handle);

    for (U32 i = 0; i < property_count; i++)
    {
        Material_Property *property = &material_properties[i];
        S32 index = find_property(material_handle, property->name);
        if (index != -1)
        {
            if (material->properties[index].data_type == property->data_type)
            {
                set_property(material_handle, index, property->data);
            }
            else
            {
                // todo(amer): invalidate material asset because it contains old properties because the shader got reloaded or something...
            }
        }
    }

    return { .success = true, .index = material_handle.index, .generation = material_handle.generation };
}

void unload_material(Load_Asset_Result load_result)
{
    Material_Handle material_handle = { .index = load_result.index, .generation = load_result.generation };
    renderer_destroy_material(material_handle);
}