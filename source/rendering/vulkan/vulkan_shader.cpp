#include <spirv-headers/spirv.h>

#include "vulkan_shader.h"
#include "core/platform.h"
#include "core/debugging.h"

#include <initializer_list>
#include <vector>
#include <map>

enum ShaderEntityKind
{
    ShaderEntityKind_Unkown,
    ShaderEntityKind_Constant,
    ShaderEntityKind_Variable,
    ShaderEntityKind_Type,
};

enum ShaderEntityType
{
    ShaderEntityType_Unkown,

    ShaderEntityType_S8,
    ShaderEntityType_S16,
    ShaderEntityType_S32,
    ShaderEntityType_S64,

    ShaderEntityType_U8,
    ShaderEntityType_U16,
    ShaderEntityType_U32,
    ShaderEntityType_U64,

    ShaderEntityType_F16,
    ShaderEntityType_F32,
    ShaderEntityType_F64,

    ShaderEntityType_Vector,
    ShaderEntityType_Matrix,

    ShaderEntityType_Pointer,

    ShaderEntityType_Struct,

    ShaderEntityType_Array,
    ShaderEntityType_RuntimeArray,

    ShaderEntityType_SampledImage
};

struct Shader_Entity
{
    const char *name;
    U32 name_length;

    ShaderEntityKind kind = ShaderEntityKind_Unkown;

    ShaderEntityType type = ShaderEntityType_Unkown;
    S32 id_of_type = -1;

    SpvStorageClass storage_class = SpvStorageClassMax;
    SpvDecoration decoration = SpvDecorationMax;

    std::vector< S32 > members;

    S32 component_count = -1;
    S32 element_count = -1;

    S32 location = -1;

    U64 value = 0;

    S32 binding = -1;
    S32 set = -1;
};

void parse_int(Shader_Entity &entity, const U32 *instruction)
{
    entity.kind = ShaderEntityKind_Type;

    U32 width = instruction[2];
    U32 signedness = instruction[3];

    if (signedness == 0)
    {
        switch (width)
        {
            case 8:
            {
                entity.type = ShaderEntityType_U8;
            } break;

            case 16:
            {
                entity.type = ShaderEntityType_U16;
            } break;

            case 32:
            {
                entity.type = ShaderEntityType_U32;
            } break;

            case 64:
            {
                entity.type = ShaderEntityType_U64;
            } break;

            default:
            {
                Assert(!"invalid width");
            } break;
        }
    }
    else
    {
        switch (width)
        {
            case 8:
            {
                entity.type = ShaderEntityType_S8;
            } break;

            case 16:
            {
                entity.type = ShaderEntityType_S16;
            } break;

            case 32:
            {
                entity.type = ShaderEntityType_S32;
            } break;

            case 64:
            {
                entity.type = ShaderEntityType_S64;
            } break;

            default:
            {
                Assert(!"invalid width");
            } break;
        }
    }
}

void parse_float(Shader_Entity& entity, const U32* instruction)
{
    entity.kind = ShaderEntityKind_Type;
    U32 width = instruction[2];
    switch (width)
    {
        case 16:
        {
            entity.type = ShaderEntityType_F16;
        } break;

        case 32:
        {
            entity.type = ShaderEntityType_F32;
        } break;

        case 64:
        {
            entity.type = ShaderEntityType_F64;
        } break;

        default:
        {
            Assert(!"invalid width");
        } break;
    }
}

bool name_starts_with(const char *name, U32 length, const char *prefix)
{
    U32 prefix_length = (U32)strlen(prefix);

    for (U32 prefix_index = 0;
        prefix_index < prefix_length;
        prefix_index++)
    {
        if (name[prefix_index] != prefix[prefix_index])
        {
            return false;
        }
    }

    return true;
}

void set_descriptor_type(VkDescriptorSetLayoutBinding &binding, const Shader_Entity &shader_entity)
{
    switch (shader_entity.type)
    {
        case ShaderEntityType_Struct:
        {
            if (shader_entity.decoration == SpvDecorationBlock)
            {
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            }
            else if (shader_entity.decoration == SpvDecorationBufferBlock)
            {
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            }
        } break;

        case ShaderEntityType_SampledImage:
        {
            binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        } break;
    }
}

bool
load_shader(Vulkan_Shader *shader, const char *path, Vulkan_Context *context, Memory_Arena *arena)
{
    Temprary_Memory_Arena temp_arena = {};
    begin_temprary_memory_arena(&temp_arena, arena);

    Read_Entire_File_Result result =
        platform_begin_read_entire_file(path);

    if (!result.success)
    {
        return false;
    }

    U8 *data = AllocateArray(&temp_arena, U8, result.size);
    if (!platform_end_read_entire_file(&result, data))
    {
        return false;
    }

    Assert(result.size % 4 == 0);

    U32 *words = (U32 *)data;
    U32 word_count = u64_to_u32(result.size / 4);

    // note(amer): we can infer the endianness out of the magic number
    U32 magic_number = words[0];
    Assert(magic_number == SpvMagicNumber);

    U32 id_count = words[3];

    std::vector<Shader_Entity> ids(id_count);

    const U32 *instruction = &words[5];

    while (instruction != words + word_count)
    {
        SpvOp op_code = SpvOp(instruction[0] & 0xff);
        U16 count = U16(instruction[0] >> 16);

        switch (op_code)
        {
            case SpvOpName:
            {
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];

                // note(amer): not proper utf8 paring here keep the shaders in english please.
                // english mother fucker english do you speak it!!!!!.
                const char *name = (const char*)(instruction + 2);
                entity.name = name;
                entity.name_length = u64_to_u32(strlen(name));
            } break;

            case SpvOpEntryPoint:
            {
                SpvExecutionModel model = (SpvExecutionModel)instruction[1];
                switch (model)
                {
                    case SpvExecutionModelVertex:
                    {
                        shader->stage = VK_SHADER_STAGE_VERTEX_BIT;
                    } break;

                    case SpvExecutionModelFragment:
                    {
                        shader->stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                    } break;
                }
            } break;

            case SpvOpDecorate:
            {
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];

                SpvDecoration decoration = (SpvDecoration)instruction[2];
                entity.decoration = decoration;

                switch (decoration)
                {
                    case SpvDecorationBinding:
                    {
                        Assert(count <= 4);
                        entity.binding = instruction[3];
                    } break;

                    case SpvDecorationDescriptorSet:
                    {
                        Assert(count <= 4);
                        entity.set = instruction[3];
                    } break;

                    case SpvDecorationLocation:
                    {
                        Assert(count <= 4);
                        entity.location = instruction[3];
                    } break;
                }

            } break;

            case SpvOpConstant:
            {
                U32 id = instruction[2];
                Shader_Entity &entity = ids[id];
                entity.id_of_type = instruction[1];
                entity.value = instruction[3];
                if (count == 5)
                {
                    U64 value = U64(instruction[4]);
                    entity.value |= value << 31;
                }
            } break;

            case SpvOpVariable:
            {
                Assert(count <= 4);
                U32 id = instruction[2];
                Shader_Entity &entity = ids[id];

                entity.kind = ShaderEntityKind_Variable;
                entity.id_of_type = instruction[1];
                entity.storage_class = SpvStorageClass(instruction[3]);
            } break;

            case SpvOpTypeInt:
            {
                Assert(count <= 4);
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                parse_int(entity, instruction);
            }  break;

            case SpvOpTypeFloat:
            {
                Assert(count <= 3);
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                parse_float(entity, instruction);
            } break;

            case SpvOpTypeVector:
            {
                Assert(count <= 4);
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_Vector;
                entity.id_of_type = instruction[2];
                entity.component_count = instruction[3];
            } break;

            case SpvOpTypeMatrix:
            {
                Assert(count <= 4);
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_Matrix;
                entity.id_of_type = instruction[2];
                entity.component_count = instruction[3];
            } break;

            case SpvOpTypePointer:
            {
                Assert(count <= 4);
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_Pointer;
                entity.storage_class = SpvStorageClass(instruction[2]);
                entity.id_of_type = instruction[3];
            } break;

            case SpvOpTypeForwardPointer:
            {
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_Pointer;
                entity.storage_class = SpvStorageClass(instruction[2]);
            } break;

            case SpvOpTypeStruct:
            {
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_Struct;
                U32 member_count = count - 2;
                entity.members.resize(member_count);
                const U32 *member_instruction = &instruction[2];
                for (U32 member_index = 0; member_index < member_count; member_index++)
                {
                    entity.members[member_index] = member_instruction[member_index];
                }
            } break;

            case SpvOpTypeArray:
            {
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_Array;
                entity.id_of_type = instruction[2];
                U32 length_id = instruction[3];
                Shader_Entity &length = ids[length_id];
                entity.element_count = u64_to_u32(length.value);
            } break;

            case SpvOpTypeRuntimeArray:
            {
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_RuntimeArray;
                entity.id_of_type = instruction[2];
            } break;

            case SpvOpTypeSampledImage:
            {
                Assert(count <= 3);
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_SampledImage;
                entity.id_of_type = instruction[2];
            } break;
        }

        instruction += count;
    }

    std::map< U32, std::map< U32, VkDescriptorSetLayoutBinding > > sets;

    for (const Shader_Entity &entity : ids)
    {
        if (entity.kind == ShaderEntityKind_Variable)
        {
            switch (entity.storage_class)
            {
                case SpvStorageClassUniform:
                case SpvStorageClassUniformConstant:
                {
                    Assert(entity.set >= 0 && entity.set < 4);

                    auto &set = sets[entity.set];
                    auto &binding = set[entity.binding];
                    binding.descriptorCount = 1;

                    binding.binding = entity.binding;
                    binding.stageFlags = shader->stage;

                    const Shader_Entity &uniform = ids[ ids[ entity.id_of_type ].id_of_type ];

                    if (uniform.type == ShaderEntityType_Array)
                    {
                        binding.descriptorCount = uniform.element_count;
                        const Shader_Entity &element_type = ids[uniform.id_of_type];
                        set_descriptor_type(binding, element_type);
                    }
                    else if (uniform.type == ShaderEntityType_RuntimeArray)
                    {
                        binding.descriptorCount = MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT;
                        const Shader_Entity& element_type = ids[uniform.id_of_type];
                        set_descriptor_type(binding, element_type);
                    }
                    else
                    {
                        set_descriptor_type(binding, uniform);
                    }
                } break;

                case SpvStorageClassInput:
                {
                    const Shader_Entity &input_type = ids[ ids[ entity.id_of_type ].id_of_type ];
                    if (entity.location != -1)
                    {
                    }
                } break;
            }
        }
    }

    Assert(sets.size() <= 4);

    VkShaderModuleCreateInfo shader_create_info =
        { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    shader_create_info.codeSize = result.size;
    shader_create_info.pCode = (U32 *)data;

    CheckVkResult(vkCreateShaderModule(context->logical_device,
                                       &shader_create_info,
                                       nullptr,
                                       &shader->handle));

    end_temprary_memory_arena(&temp_arena);

    for (const auto &current_set : sets)
    {
        const auto& [set_id, current_set_bindings] = current_set;
        Descriptor_Set *set = &shader->sets[set_id];
        set->binding_count = u64_to_u32(current_set_bindings.size());
        set->bindings = AllocateArray(arena, VkDescriptorSetLayoutBinding, current_set_bindings.size());
        U32 binding_index = 0;
        for (const auto &current_binding : current_set_bindings)
        {
            const auto &[binding_id, binding] = current_binding;
            set->bindings[binding_index++] = binding;
        }
    }

    return true;
}

void destroy_shader(Vulkan_Shader *shader, VkDevice logical_device)
{
    vkDestroyShaderModule(logical_device, shader->handle, nullptr);
}

bool create_graphics_pipeline(Vulkan_Context *context,
                              const std::initializer_list<const Vulkan_Shader *> &shaders,
                              VkRenderPass render_pass,
                              Vulkan_Graphics_Pipeline *pipeline)
{
    VkVertexInputBindingDescription vertex_input_binding_description = {};
    vertex_input_binding_description.binding = 0;
    vertex_input_binding_description.stride = sizeof(Vertex);
    vertex_input_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_input_attribute_descriptions[3] = {};
    vertex_input_attribute_descriptions[0].binding  = 0;
    vertex_input_attribute_descriptions[0].location = 0;
    vertex_input_attribute_descriptions[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_input_attribute_descriptions[0].offset   = offsetof(Vertex, position);

    vertex_input_attribute_descriptions[1].binding  = 0;
    vertex_input_attribute_descriptions[1].location = 1;
    vertex_input_attribute_descriptions[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_input_attribute_descriptions[1].offset   = offsetof(Vertex, normal);

    vertex_input_attribute_descriptions[2].binding  = 0;
    vertex_input_attribute_descriptions[2].location = 2;
    vertex_input_attribute_descriptions[2].format   = VK_FORMAT_R32G32_SFLOAT;
    vertex_input_attribute_descriptions[2].offset   = offsetof(Vertex, uv);

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    vertex_input_state_create_info.vertexBindingDescriptionCount = 1;
    vertex_input_state_create_info.pVertexBindingDescriptions = &vertex_input_binding_description;

    vertex_input_state_create_info.vertexAttributeDescriptionCount = ArrayCount(vertex_input_attribute_descriptions);
    vertex_input_state_create_info.pVertexAttributeDescriptions = vertex_input_attribute_descriptions;

    VkPipelineShaderStageCreateInfo shader_stage_create_infos[16] = {};
    U32 shader_count = u64_to_u32(shaders.size());
    Assert(shader_count <= 16);

    U32 shader_index = 0;
    for (const Vulkan_Shader *shader : shaders)
    {
        VkPipelineShaderStageCreateInfo &pipeline_stage_create_info = shader_stage_create_infos[shader_index++];
        pipeline_stage_create_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline_stage_create_info.stage  = shader->stage;
        pipeline_stage_create_info.module = shader->handle;
        pipeline_stage_create_info.pName  = "main";
    }

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

    VkDescriptorSetLayout descriptor_set_layouts[] =
    {
        context->per_frame_descriptor_set_layout,
        context->texture_array_descriptor_set_layout
    };

    VkPipelineLayoutCreateInfo pipeline_layout_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipeline_layout_create_info.setLayoutCount = ArrayCount(descriptor_set_layouts);
    pipeline_layout_create_info.pSetLayouts = descriptor_set_layouts;
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
    graphics_pipeline_create_info.stageCount = shader_count;
    graphics_pipeline_create_info.pStages = shader_stage_create_infos;
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

    vkDestroyPipelineLayout(logical_device, graphics_pipeline->layout, nullptr);
    vkDestroyPipeline(logical_device, graphics_pipeline->handle, nullptr);
}