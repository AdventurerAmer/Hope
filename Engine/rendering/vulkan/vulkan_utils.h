#pragma once

#include "vulkan_types.h"

S32 find_memory_type_index(VkMemoryRequirements memory_requirements, VkMemoryPropertyFlags memory_property_flags, Vulkan_Context *context);
VkSampleCountFlagBits get_sample_count(U32 sample_count);
