#pragma once

#include "core/defines.h"
#include "rendering/renderer_types.h"

bool is_color_format(Texture_Format format);

U32 get_size_of_shader_data_type(Shader_Data_Type data_type);

U32 get_anisotropic_filtering_value(Anisotropic_Filtering_Setting anisotropic_filtering_setting);
U32 get_sample_count(MSAA_Setting msaa_setting);

glm::vec3 srgb_to_linear(const glm::vec3 &color, F32 gamma);