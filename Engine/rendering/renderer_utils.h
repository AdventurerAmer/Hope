#pragma once

#include "core/defines.h"
#include "rendering/renderer_types.h"

bool is_color_format(Texture_Format format);
bool is_color_format_int(Texture_Format format);
bool is_color_format_uint(Texture_Format format);

U32 get_size_of_shader_data_type(Shader_Data_Type data_type);

U32 get_anisotropic_filtering_value(Anisotropic_Filtering_Setting anisotropic_filtering_setting);
U32 get_sample_count(MSAA_Setting msaa_setting);

glm::vec3 srgb_to_linear(const glm::vec3 &color, F32 gamma);
glm::vec4 srgb_to_linear(const glm::vec4 &color, F32 gamma);

glm::vec3 linear_to_srgb(const glm::vec3 &color, F32 gamma);
glm::vec4 linear_to_srgb(const glm::vec4 &color, F32 gamma);

String shader_data_type_to_str(Shader_Data_Type type);
Shader_Data_Type str_to_shader_data_type(String str);