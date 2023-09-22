#pragma once

#include "vulkan_types.h"

bool init_swapchain_support(Vulkan_Context *context, VkFormat *image_formats, U32 image_format_count, VkFormat *depth_stencil_formats, U32 depth_stencil_format_count,
                            VkColorSpaceKHR color_space, Memory_Arena *arena, Vulkan_Swapchain_Support *swapchain_support);

bool create_swapchain(Vulkan_Context *context, U32 width, U32 height, U32 min_image_count, VkPresentModeKHR present_mode, Vulkan_Swapchain *swapchain);
void destroy_swapchain(Vulkan_Context *context, Vulkan_Swapchain *swapchain);

void recreate_swapchain(Vulkan_Context *context, Vulkan_Swapchain *swapchain, U32 width, U32 height, VkPresentModeKHR present_mode);