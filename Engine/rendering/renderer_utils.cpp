#include "renderer_utils.h"

bool is_color_format(Texture_Format format)
{
    switch (format)
    {
        case Texture_Format::R8G8B8A8_SRGB:
        case Texture_Format::B8G8R8A8_SRGB:
        {
            return true;
        } break;
    }

    return false;
}

U32 get_size_of_shader_data_type(Shader_Data_Type data_type)
{
    switch (data_type)
    {
        case Shader_Data_Type::BOOL: return 1;

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