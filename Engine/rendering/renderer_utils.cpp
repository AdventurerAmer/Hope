#include "renderer_utils.h"

bool is_color_format(Texture_Format format)
{
    switch (format)
    {
        case Texture_Format::R8G8B8A8_UNORM:
        case Texture_Format::R8G8B8_UNORM:
        case Texture_Format::B8G8R8A8_UNORM:
        case Texture_Format::R8G8B8A8_SRGB:
        case Texture_Format::B8G8R8A8_SRGB:
        case Texture_Format::R32G32B32A32_SFLOAT:
        case Texture_Format::R32G32B32_SFLOAT:
        case Texture_Format::R32_SINT:
        case Texture_Format::R32_UINT:
        {
            return true;
        } break;
    }

    return false;
}

bool is_color_format_int(Texture_Format format)
{
    switch (format)
    {
        case Texture_Format::R32_SINT: return true;
    }

    return false;
}

bool is_color_format_uint(Texture_Format format)
{
    switch (format)
    {
        case Texture_Format::R32_UINT: return true;
    }

    return false;
}

U32 get_size_of_shader_data_type(Shader_Data_Type data_type)
{
    switch (data_type)
    {
        case Shader_Data_Type::S8: return 1;
        case Shader_Data_Type::S16: return 2;
        case Shader_Data_Type::S32: return 4;
        case Shader_Data_Type::S64: return 8;

        case Shader_Data_Type::U8: return 1;
        case Shader_Data_Type::U16: return 2;
        case Shader_Data_Type::U32: return 4;
        case Shader_Data_Type::U64: return 8;

        case Shader_Data_Type::F16: return 2;
        case Shader_Data_Type::F32: return 4;
        case Shader_Data_Type::F64: return 8;

        case Shader_Data_Type::VECTOR2F: return 2 * 4;
        case Shader_Data_Type::VECTOR3F: return 3 * 4;
        case Shader_Data_Type::VECTOR4F: return 4 * 4;

        case Shader_Data_Type::MATRIX3F: return 9 * 4;
        case Shader_Data_Type::MATRIX4F: return 16 * 4;

        default:
        {
            HE_ASSERT(!"unsupported type");
        } break;
    }

    return 0;
}

U32 get_sample_count(MSAA_Setting msaa_setting)
{
    switch (msaa_setting)
    {
        case MSAA_Setting::NONE: return 1;
        case MSAA_Setting::X2: return 2;
        case MSAA_Setting::X4: return 4;
        case MSAA_Setting::X8: return 8;

        default:
        {
            HE_ASSERT(!"invalid msaa setting");
        } break;
    }

    return 0;
}

U32 get_anisotropic_filtering_value(Anisotropic_Filtering_Setting anisotropic_filtering_setting)
{
    switch (anisotropic_filtering_setting)
    {
        case Anisotropic_Filtering_Setting::NONE: return 0;
        case Anisotropic_Filtering_Setting::X2: return 2;
        case Anisotropic_Filtering_Setting::X4: return 4;
        case Anisotropic_Filtering_Setting::X8: return 8;
        case Anisotropic_Filtering_Setting::X16: return 16;

        default:
        {
            HE_ASSERT(!"invalid anisotropic filtering setting");
        } break; 
    }

    return 0;
}

glm::vec3 srgb_to_linear(const glm::vec3 &color, F32 gamma)
{
    return glm::pow(color, glm::vec3(gamma));
}

glm::vec4 srgb_to_linear(const glm::vec4 &color, F32 gamma)
{
    return glm::pow(color, glm::vec4(gamma));
}

glm::vec3 linear_to_srgb(const glm::vec3 &color, F32 gamma)
{
    return glm::pow(color, glm::vec3(1.0f / gamma));
}

glm::vec4 linear_to_srgb(const glm::vec4 &color, F32 gamma)
{
    return glm::pow(color, glm::vec4(1.0f / gamma));
}

String shader_data_type_to_str(Shader_Data_Type type)
{
    switch (type)
    {
        case Shader_Data_Type::S8: return HE_STRING_LITERAL("s8");
        case Shader_Data_Type::S16: return HE_STRING_LITERAL("s16");
        case Shader_Data_Type::S32: return HE_STRING_LITERAL("s32");
        case Shader_Data_Type::S64: return HE_STRING_LITERAL("s64");

        case Shader_Data_Type::U8: return HE_STRING_LITERAL("u8");
        case Shader_Data_Type::U16: return HE_STRING_LITERAL("u16");
        case Shader_Data_Type::U32: return HE_STRING_LITERAL("u32");
        case Shader_Data_Type::U64: return HE_STRING_LITERAL("u64");

        case Shader_Data_Type::F16: return HE_STRING_LITERAL("f16");
        case Shader_Data_Type::F32: return HE_STRING_LITERAL("f32");
        case Shader_Data_Type::F64: return HE_STRING_LITERAL("f64");

        case Shader_Data_Type::VECTOR2F: return HE_STRING_LITERAL("v2f");
        case Shader_Data_Type::VECTOR3F: return HE_STRING_LITERAL("v3f");
        case Shader_Data_Type::VECTOR4F: return HE_STRING_LITERAL("v4f");

        case Shader_Data_Type::VECTOR2S: return HE_STRING_LITERAL("v2s");
        case Shader_Data_Type::VECTOR3S: return HE_STRING_LITERAL("v3s");
        case Shader_Data_Type::VECTOR4S: return HE_STRING_LITERAL("v4s");

        case Shader_Data_Type::VECTOR2U: return HE_STRING_LITERAL("v2u");
        case Shader_Data_Type::VECTOR3U: return HE_STRING_LITERAL("v3u");
        case Shader_Data_Type::VECTOR4U: return HE_STRING_LITERAL("v4u");

        case Shader_Data_Type::MATRIX3F: return HE_STRING_LITERAL("mat3f");
        case Shader_Data_Type::MATRIX4F: return HE_STRING_LITERAL("mat4f");

        case Shader_Data_Type::STRUCT: return HE_STRING_LITERAL("struct");

        default:
        {
            HE_ASSERT(!"unsupported shader data type");
        } break;
    }

    return HE_STRING_LITERAL("");
}

Shader_Data_Type str_to_shader_data_type(String str)
{
    if (str == "s8")
    {
        return Shader_Data_Type::S8;
    }
    else if (str == "s16")
    {
        return Shader_Data_Type::S16;
    }
    else if (str == "s32")
    {
        return Shader_Data_Type::S32;
    }
    else if (str == "s64")
    {
        return Shader_Data_Type::S64;
    }
    else if (str == "u8")
    {
        return Shader_Data_Type::U8;
    }
    else if (str == "u16")
    {
        return Shader_Data_Type::U16;
    }
    else if (str == "u32")
    {
        return Shader_Data_Type::U32;
    }
    else if (str == "u64")
    {
        return Shader_Data_Type::U64;
    }
    else if (str == "f16")
    {
        return Shader_Data_Type::F16;
    }
    else if (str == "f32")
    {
        return Shader_Data_Type::F32;
    }
    else if (str == "f64")
    {
        return Shader_Data_Type::F64;
    }
    else if (str == "v2f")
    {
        return Shader_Data_Type::VECTOR2F;
    }
    else if (str == "v3f")
    {
        return Shader_Data_Type::VECTOR3F;
    }
    else if (str == "v4f")
    {
        return Shader_Data_Type::VECTOR4F;
    }
    else if (str == "v2s")
    {
        return Shader_Data_Type::VECTOR2S;
    }
    else if (str == "v3s")
    {
        return Shader_Data_Type::VECTOR3S;
    }
    else if (str == "v4s")
    {
        return Shader_Data_Type::VECTOR4S;
    }
    else if (str == "v2u")
    {
        return Shader_Data_Type::VECTOR2U;
    }
    else if (str == "v3u")
    {
        return Shader_Data_Type::VECTOR3U;
    }
    else if (str == "v4u")
    {
        return Shader_Data_Type::VECTOR4U;
    }
    else if (str == "mat3f")
    {
        return Shader_Data_Type::MATRIX3F;
    }
    else if (str == "mat4f")
    {
        return Shader_Data_Type::MATRIX4F;
    }
    else if (str == "struct")
    {
        return Shader_Data_Type::STRUCT;
    }
    else
    {
        HE_ASSERT(!"unsupported shader data type");
    }

    return Shader_Data_Type::NONE;
}