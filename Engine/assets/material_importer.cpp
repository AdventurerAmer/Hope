#include "assets/material_importer.h"

#include "core/logging.h"
#include "core/file_system.h"

#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"

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

static constexpr const char* front_faces[] = { "clockwise", "counter_clockwise"};

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

static constexpr const char* compare_ops[] = { "never", "less", "equal", "less_or_equal", "greater", "not_equal", "greater_or_equal", "always" };

static Compare_Operation str_to_compare_op(String str)
{
    for (U32 i = 0; i < HE_ARRAYCOUNT(compare_ops); i++)
    {
        if (str == compare_ops[i])
        {
            return (Compare_Operation)i;
        }
    }
    HE_ASSERT(!"unsupported compare op");
    return Compare_Operation::ALWAYS;
}

static constexpr const char *stencil_ops[] = { "keep", "zero", "replace", "increment_and_clamp", "decrement_and_clamp", "invert", "increment_and_wrap", "decrement_and_wrap" };

static Stencil_Operation str_to_stencil_op(String str)
{
    for (U32 i = 0; i < HE_ARRAYCOUNT(stencil_ops); i++)
    {
        if (str == stencil_ops[i])
        {
            return (Stencil_Operation)i;
        }
    }
    HE_ASSERT(!"unsupported stencil op");
    return Stencil_Operation::KEEP;
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
    U64 version_value = str_to_u64(result.value);

    result = parse_name_value(&str, HE_STRING_LITERAL("type"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    Material_Type type = result.value == "opaque" ? Material_Type::opaque : Material_Type::transparent;

    result = parse_name_value(&str, HE_STRING_LITERAL("shader"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    Asset_Handle shader_asset = { .uuid = str_to_u64(result.value) };

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

    result = parse_name_value(&str, HE_STRING_LITERAL("depth_operation"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    Compare_Operation depth_op = str_to_compare_op(result.value);

    result = parse_name_value(&str, HE_STRING_LITERAL("depth_testing"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    bool depth_testing = result.value == "true";

    result = parse_name_value(&str, HE_STRING_LITERAL("depth_writing"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    bool depth_writing = result.value == "true";

    result = parse_name_value(&str, HE_STRING_LITERAL("stencil_operation"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    Compare_Operation stencil_op = str_to_compare_op(result.value);

    result = parse_name_value(&str, HE_STRING_LITERAL("stencil_testing"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    bool stencil_testing = result.value == "true";

    result = parse_name_value(&str, HE_STRING_LITERAL("stencil_pass"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    Stencil_Operation stencil_pass = str_to_stencil_op(result.value);

    result = parse_name_value(&str, HE_STRING_LITERAL("stencil_fail"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    Stencil_Operation stencil_fail = str_to_stencil_op(result.value);
    
    result = parse_name_value(&str, HE_STRING_LITERAL("depth_fail"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    Stencil_Operation depth_fail = str_to_stencil_op(result.value);

    result = parse_name_value(&str, HE_STRING_LITERAL("stencil_compare_mask"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    U32 stencil_compare_mask = u64_to_u32(str_to_u64(result.value));

    result = parse_name_value(&str, HE_STRING_LITERAL("stencil_write_mask"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    U32 stencil_write_mask = u64_to_u32(str_to_u64(result.value));

    result = parse_name_value(&str, HE_STRING_LITERAL("stencil_reference_value"));
    if (!result.success)
    {
        HE_LOG(Assets, Error, "load_material -- failed to parse material asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    U32 stencil_reference_value = u64_to_u32(str_to_u64(result.value));

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

            Shader_Data_Type data_type = (Shader_Data_Type)str_to_shader_data_type(type);
            
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
                        data.v2f[i] = value_f32;
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
                        data.v3f[i] = value_f32;
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
                        data.v4f[i] = value_f32;
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

    Pipeline_State_Settings settings =
    {
        .cull_mode = cull_mode,
        .front_face = front_face,
        .fill_mode = Fill_Mode::SOLID,
        .depth_operation = depth_op,
        .depth_testing = depth_testing,
        .depth_writing = depth_writing,
        .stencil_operation = stencil_op,
        .stencil_fail = stencil_fail,
        .stencil_pass = stencil_pass,
        .depth_fail = depth_fail,
        .stencil_compare_mask = stencil_compare_mask,
        .stencil_write_mask = stencil_write_mask,
        .stencil_reference_value = stencil_reference_value,
        .stencil_testing = stencil_testing,
        .sample_shading = true,
    };

    Material_Descriptor material_descriptor =
    {
        .name = path,
        .type = type,
        .shader = get_asset_handle_as<Shader>(shader_asset),
        .settings = settings
    };

    Material_Handle material_handle = renderer_create_material(material_descriptor);
    Material *material = renderer_get_material(material_handle);

    for (U32 i = 0; i < property_count; i++)
    {
        Material_Property *property = &material_properties[i];
        S32 index = find_property(material_handle, property->name);
        if (index != -1 && material->properties[index].data_type == property->data_type)
        {
            set_property(material_handle, index, property->data);
        }
    }

    return { .success = true, .index = material_handle.index, .generation = material_handle.generation };
}

void unload_material(Load_Asset_Result load_result)
{
    Material_Handle material_handle = { .index = load_result.index, .generation = load_result.generation };
    renderer_destroy_material(material_handle);
}