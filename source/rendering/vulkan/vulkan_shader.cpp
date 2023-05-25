#include "vulkan_shader.h"
#include "core/platform.h"

bool
load_shader(Vulkan_Shader *shader, const char *path, Vulkan_Context *context, Memory_Arena *arena)
{
    Scoped_Temprary_Memory_Arena temp_arena(arena);

    Read_Entire_File_Result result =
        platform_begin_read_entire_file(path);

    if (result.success)
    {
        U8 *data = AllocateArray(&temp_arena, U8, result.size);
        if (platform_end_read_entire_file(&result, data))
        {
            VkShaderModuleCreateInfo shader_create_info =
                { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            shader_create_info.codeSize = result.size;
            shader_create_info.pCode = (U32 *)data;

            CheckVkResult(vkCreateShaderModule(context->logical_device,
                                               &shader_create_info,
                                               nullptr,
                                               &shader->handle));
            return true;
        }
    }

    return false;
}

void destroy_shader(Vulkan_Shader *shader, VkDevice logical_device)
{
    vkDestroyShaderModule(logical_device, shader->handle, nullptr);
}

bool
create_graphics_pipeline(Vulkan_Context *context,
                         VkShaderModule vertex_shader,
                         VkShaderModule fragment_shader,
                         VkRenderPass render_pass,
                         Vulkan_Graphics_Pipeline *pipeline)
{
    VkVertexInputBindingDescription vertex_input_binding_description = {};
    vertex_input_binding_description.binding = 0;
    vertex_input_binding_description.stride = sizeof(Vertex); // todo(amer): temprary
    vertex_input_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_input_attribute_descriptions[3] = {};
    vertex_input_attribute_descriptions[0].binding = 0;
    vertex_input_attribute_descriptions[0].location = 0;
    vertex_input_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_input_attribute_descriptions[0].offset = offsetof(Vertex, position);

    vertex_input_attribute_descriptions[1].binding = 0;
    vertex_input_attribute_descriptions[1].location = 1;
    vertex_input_attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_input_attribute_descriptions[1].offset = offsetof(Vertex, normal);

    vertex_input_attribute_descriptions[2].binding = 0;
    vertex_input_attribute_descriptions[2].location = 2;
    vertex_input_attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    vertex_input_attribute_descriptions[2].offset = offsetof(Vertex, uv);

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    vertex_input_state_create_info.vertexBindingDescriptionCount = 1;
    vertex_input_state_create_info.pVertexBindingDescriptions = &vertex_input_binding_description;

    vertex_input_state_create_info.vertexAttributeDescriptionCount = ArrayCount(vertex_input_attribute_descriptions);
    vertex_input_state_create_info.pVertexAttributeDescriptions = vertex_input_attribute_descriptions;

    VkPipelineShaderStageCreateInfo vertex_shader_stage_info
        = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vertex_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_shader_stage_info.module = vertex_shader;
    vertex_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo fragment_shader_stage_info
        = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    fragment_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragment_shader_stage_info.module = fragment_shader;
    fragment_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] =
    {
        vertex_shader_stage_info,
        fragment_shader_stage_info
    };

    VkDynamicState dynamic_states[] =
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic_state_create_info.dynamicStateCount = ArrayCount(dynamic_states);
    dynamic_state_create_info.pDynamicStates = dynamic_states;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state_create_info.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (F32)context->swapchain.width;
    viewport.height = (F32)context->swapchain.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D sissor = {};
    sissor.offset = { 0, 0 };
    sissor.extent = { context->swapchain.width, context->swapchain.height };

    VkPipelineViewportStateCreateInfo viewport_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports = &viewport;
    viewport_state_create_info.scissorCount = 1;
    viewport_state_create_info.pScissors = &sissor;

    VkPipelineRasterizationStateCreateInfo rasterization_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };

    rasterization_state_create_info.depthClampEnable = VK_FALSE;
    rasterization_state_create_info.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state_create_info.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state_create_info.lineWidth = 1.0f;
    rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterization_state_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state_create_info.depthBiasEnable = VK_FALSE;
    rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
    rasterization_state_create_info.depthBiasClamp = 0.0f;
    rasterization_state_create_info.depthBiasSlopeFactor = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling_state_create_info.sampleShadingEnable = VK_TRUE;
    multisampling_state_create_info.rasterizationSamples = context->msaa_samples;
    multisampling_state_create_info.minSampleShading = 0.2f;
    multisampling_state_create_info.pSampleMask = nullptr;
    multisampling_state_create_info.alphaToCoverageEnable = VK_FALSE;
    multisampling_state_create_info.alphaToOneEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
    color_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|
        VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment_state.blendEnable = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    color_blend_state_create_info.logicOpEnable = VK_FALSE;
    color_blend_state_create_info.logicOp = VK_LOGIC_OP_COPY;
    color_blend_state_create_info.attachmentCount = 1;
    color_blend_state_create_info.pAttachments = &color_blend_attachment_state;
    color_blend_state_create_info.blendConstants[0] = 0.0f;
    color_blend_state_create_info.blendConstants[1] = 0.0f;
    color_blend_state_create_info.blendConstants[2] = 0.0f;
    color_blend_state_create_info.blendConstants[3] = 0.0f;

    VkDescriptorSetLayoutBinding descriptor_layout_bindings[2] = {};
    descriptor_layout_bindings[0].binding = 0;
    descriptor_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_layout_bindings[0].descriptorCount = 1;
    descriptor_layout_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    descriptor_layout_bindings[1].binding = 1;
    descriptor_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_layout_bindings[1].descriptorCount = 1;
    descriptor_layout_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info =
        { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    descriptor_set_layout_create_info.bindingCount = ArrayCount(descriptor_layout_bindings);
    descriptor_set_layout_create_info.pBindings = descriptor_layout_bindings;

    CheckVkResult(vkCreateDescriptorSetLayout(context->logical_device,
                                              &descriptor_set_layout_create_info,
                                              nullptr,
                                              &context->graphics_pipeline.descriptor_set_layout));

    VkPipelineLayoutCreateInfo pipeline_layout_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipeline_layout_create_info.setLayoutCount = 1;
    pipeline_layout_create_info.pSetLayouts = &context->graphics_pipeline.descriptor_set_layout;
    pipeline_layout_create_info.pushConstantRangeCount = 0;
    pipeline_layout_create_info.pPushConstantRanges = nullptr;

    CheckVkResult(vkCreatePipelineLayout(context->logical_device,
                                         &pipeline_layout_create_info,
                                         nullptr, &pipeline->layout));

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info
        = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

    depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
    depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
    depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil_state_create_info.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state_create_info.minDepthBounds = 0.0f;
    depth_stencil_state_create_info.maxDepthBounds = 1.0f;
    depth_stencil_state_create_info.stencilTestEnable = VK_FALSE; // todo(amer): stencil test is disabled
    depth_stencil_state_create_info.front = {};
    depth_stencil_state_create_info.back = {};

    VkGraphicsPipelineCreateInfo graphics_pipeline_create_info =
        { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    graphics_pipeline_create_info.stageCount = 2;
    graphics_pipeline_create_info.pStages = shader_stages;
    graphics_pipeline_create_info.pVertexInputState = &vertex_input_state_create_info;
    graphics_pipeline_create_info.pInputAssemblyState = &input_assembly_state_create_info;
    graphics_pipeline_create_info.pViewportState = &viewport_state_create_info;
    graphics_pipeline_create_info.pRasterizationState = &rasterization_state_create_info;
    graphics_pipeline_create_info.pMultisampleState = &multisampling_state_create_info;
    graphics_pipeline_create_info.pDepthStencilState = &depth_stencil_state_create_info;
    graphics_pipeline_create_info.pColorBlendState = &color_blend_state_create_info;
    graphics_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    graphics_pipeline_create_info.layout = pipeline->layout;
    graphics_pipeline_create_info.renderPass = render_pass;
    graphics_pipeline_create_info.subpass = 0;
    graphics_pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    graphics_pipeline_create_info.basePipelineIndex = -1;

    CheckVkResult(vkCreateGraphicsPipelines(context->logical_device, VK_NULL_HANDLE,
                                            1, &graphics_pipeline_create_info,
                                            nullptr, &pipeline->handle));

    return true;
}

void destroy_graphics_pipeline(VkDevice logical_device, Vulkan_Graphics_Pipeline *graphics_pipeline)
{
    Assert(logical_device != VK_NULL_HANDLE);
    Assert(graphics_pipeline);

    vkDestroyDescriptorSetLayout(logical_device, graphics_pipeline->descriptor_set_layout, nullptr);
    vkDestroyPipelineLayout(logical_device, graphics_pipeline->layout, nullptr);
    vkDestroyPipeline(logical_device, graphics_pipeline->handle, nullptr);
}