#include "vulkan_utils.h"

S32 find_memory_type_index(VkMemoryRequirements memory_requirements, VkMemoryPropertyFlags memory_property_flags, Vulkan_Context *context)
{
    S32 result = -1;

    for (U32 memory_type_index = 0; memory_type_index < context->physical_device_memory_properties.memoryTypeCount; memory_type_index++)
    {
        if (((1 << memory_type_index) & memory_requirements.memoryTypeBits))
        {
            // todo(amer): we should track how much memory we allocated from heaps so allocations don't fail
            const VkMemoryType *memory_type = &context->physical_device_memory_properties.memoryTypes[memory_type_index];
            if ((memory_type->propertyFlags & memory_property_flags) == memory_property_flags)
            {
                result = (S32)memory_type_index;
                break;
            }
        }
    }

    return result;
}

VkSampleCountFlagBits get_sample_count(U32 sample_count)
{
    switch (sample_count)
    {
        case 1: return VK_SAMPLE_COUNT_1_BIT;
        case 2: return VK_SAMPLE_COUNT_2_BIT;
        case 4: return VK_SAMPLE_COUNT_4_BIT;
        case 8: return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;

        default:
        {
            HE_ASSERT(false);
        }
    }

    return VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
}