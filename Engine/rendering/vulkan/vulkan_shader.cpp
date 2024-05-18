#include <spirv-headers/spirv.h>

#include "vulkan_shader.h"
#include "vulkan_utils.h"

#include "core/platform.h"
#include "core/logging.h"
#include "core/engine.h"
#include "core/file_system.h"

#include "containers/dynamic_array.h"

#include "rendering/renderer_utils.h"

#include <spirv_cross/spirv_glsl.hpp>

static VkShaderStageFlagBits get_shader_stage(Shader_Stage shader_stage)
{
    switch (shader_stage)
    {
        case Shader_Stage::VERTEX: return VK_SHADER_STAGE_VERTEX_BIT;
        case Shader_Stage::FRAGMENT: return VK_SHADER_STAGE_FRAGMENT_BIT;

        default:
        {
            HE_ASSERT(!"unsupported shader stage");
        } break;
    }

    return VK_SHADER_STAGE_ALL;
} 

// todo(amer): handle more vector types and matrices
static VkFormat get_format_from_spirv_type(spirv_cross::SPIRType type)
{
    using namespace spirv_cross;
    
    switch (type.basetype)
    {
		case SPIRType::SByte: return VK_FORMAT_R8_SINT; 
        case SPIRType::Short: return VK_FORMAT_R16_SINT;
        case SPIRType::Int: return VK_FORMAT_R32_SINT;
        case SPIRType::Int64: return VK_FORMAT_R64_SINT;

        case SPIRType::UByte: return VK_FORMAT_R8_UINT;
        case SPIRType::UShort: return VK_FORMAT_R16_UINT;
        case SPIRType::UInt: return VK_FORMAT_R32_UINT;
        case SPIRType::UInt64: return VK_FORMAT_R64_UINT;

        case SPIRType::Half: return VK_FORMAT_R16_SFLOAT;
        
        case SPIRType::Float:
        {
            if (type.vecsize == 1)
            {
                return VK_FORMAT_R32_SFLOAT;
            }
            else if (type.vecsize == 2)
            {
                return VK_FORMAT_R32G32_SFLOAT;
            }
            else if (type.vecsize == 3)
            {
                return VK_FORMAT_R32G32B32_SFLOAT;
            }
            else if (type.vecsize == 4)
            {
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            }
        } break;
        
        case SPIRType::Double: return VK_FORMAT_R64_SFLOAT;

        default:
        {
            HE_ASSERT(!"unsupported type");
        } break;
    }

    return VK_FORMAT_UNDEFINED;
}

static Shader_Data_Type spirv_type_to_shader_data_type(spirv_cross::SPIRType type)
{
    using namespace spirv_cross;
    
    switch (type.basetype)
    {
		case SPIRType::SByte: return Shader_Data_Type::S8; 
        case SPIRType::Short: return Shader_Data_Type::S16;
        case SPIRType::Int: return Shader_Data_Type::S32;
        case SPIRType::Int64: return Shader_Data_Type::S64;

        case SPIRType::UByte: return Shader_Data_Type::U8;
        case SPIRType::UShort: return Shader_Data_Type::U16;
        case SPIRType::UInt: return Shader_Data_Type::U32;
        case SPIRType::UInt64: return Shader_Data_Type::U64;

        case SPIRType::Half: return Shader_Data_Type::F16;
        
        case SPIRType::Float:
        {
            if (type.vecsize == 1)
            {
                return Shader_Data_Type::F32;
            }
            else if (type.vecsize == 2)
            {
                return Shader_Data_Type::VECTOR2F;
            }
            else if (type.vecsize == 3)
            {
                return type.columns == 3 ? Shader_Data_Type::MATRIX3F : Shader_Data_Type::VECTOR3F;
            }
            else if (type.vecsize == 4)
            {
                return type.columns == 4 ? Shader_Data_Type::MATRIX4F : Shader_Data_Type::VECTOR4F;
            }
        } break;
        
        case SPIRType::Double: return Shader_Data_Type::F64;
        case SPIRType::Struct: return Shader_Data_Type::STRUCT;

        default:
        {
            HE_ASSERT(!"unsupported type");
        } break;
    }

    return Shader_Data_Type::NONE;
}

static U32 get_size_of_spirv_type(spirv_cross::SPIRType type)
{
    using namespace spirv_cross;
    
    switch (type.basetype)
    {
		case SPIRType::SByte: return 1; 
        case SPIRType::Short: return 2;
        case SPIRType::Int: return 4;
        case SPIRType::Int64: return 8;

        case SPIRType::UByte: return 1;
        case SPIRType::UShort: return 2;
        case SPIRType::UInt: return 4;
        case SPIRType::UInt64: return 8;

        case SPIRType::Half: return 2;
        
        case SPIRType::Float:
        {
            if (type.vecsize == 1)
            {
                return 4;
            }
            else if (type.vecsize == 2)
            {
                return 4 * 2;
            }
            else if (type.vecsize == 3)
            {
                return type.columns == 3 ? 4 * 3 * 3 : 4 * 3;
            }
            else if (type.vecsize == 4)
            {
                return type.columns == 4 ? 4 * 4 * 4 : 4 * 4;
            }
        } break;
        
        case SPIRType::Double: return 8;

        default:
        {
            HE_ASSERT(!"unsupported type");
        } break;
    }

    return VK_FORMAT_UNDEFINED;
}

#define HE_MAX_BINDING_COUNT_PER_DESCRIPTOR_SET 64

static VkDescriptorSetLayoutBinding& find_or_add_binding(Counted_Array< VkDescriptorSetLayoutBinding, HE_MAX_BINDING_COUNT_PER_DESCRIPTOR_SET > &set, U32 binding_number)
{
    for (U32 i = 0; i < set.count; i++)
    {
        if (set[i].binding == binding_number)
        {
            return set[i];
        }
    }
    VkDescriptorSetLayoutBinding &binding = append(&set);
    binding.binding = binding_number;
    return binding;
}

bool create_shader(Shader_Handle shader_handle, const Shader_Descriptor &descriptor, Vulkan_Context *context)
{
    Free_List_Allocator *allocator = get_general_purpose_allocator();
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
    
    Shader *shader = get(&context->renderer_state->shaders, shader_handle);
    Vulkan_Shader *vulkan_shader = &context->shaders[shader_handle.index];

    Counted_Array< VkDescriptorSetLayoutBinding, HE_MAX_BINDING_COUNT_PER_DESCRIPTOR_SET > sets[HE_MAX_BIND_GROUP_INDEX_COUNT] = {};
    
    U32 vertex_shader_input_count = 0;
    VkVertexInputBindingDescription *vertex_input_binding_descriptions = nullptr;
    VkVertexInputAttributeDescription *vertex_input_attribute_descriptions = nullptr;

    Dynamic_Array< Shader_Struct > structs;
    init(&structs);

    for (U32 stage_index = 0; stage_index < (U32)Shader_Stage::COUNT; stage_index++)
    {
        Shader_Stage stage = (Shader_Stage)stage_index;
        vulkan_shader->handles[stage_index] = VK_NULL_HANDLE;

        String blob = descriptor.compilation_result->stages[stage_index]; 
        if (!blob.count)
        {
            continue;
        }
        
        HE_ASSERT(blob.count % 4 == 0);

        VkShaderModuleCreateInfo shader_module_create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        shader_module_create_info.pCode = (U32 *)blob.data;
        shader_module_create_info.codeSize = blob.count;
        
        VkResult result = vkCreateShaderModule(context->logical_device, &shader_module_create_info, &context->allocation_callbacks, &vulkan_shader->handles[stage_index]);
        if (result != VK_SUCCESS)
        {
            return false;
        }
        
        spirv_cross::CompilerGLSL compiler((U32 *)blob.data, blob.count / 4);
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        
        if (stage == Shader_Stage::VERTEX)
        {
            vertex_shader_input_count = u64_to_u32(resources.stage_inputs.size());
            
            if (vertex_shader_input_count)
            {
                vertex_input_binding_descriptions = HE_ALLOCATE_ARRAY(allocator, VkVertexInputBindingDescription, vertex_shader_input_count);
                vertex_input_attribute_descriptions = HE_ALLOCATE_ARRAY(allocator, VkVertexInputAttributeDescription, vertex_shader_input_count);
            }

            U32 input_index = 0;

            for (auto &input : resources.stage_inputs)
            {
                U32 location = compiler.get_decoration(input.id, spv::DecorationLocation);
                const auto &type = compiler.get_type(input.type_id);

                VkVertexInputBindingDescription *vertex_binding = &vertex_input_binding_descriptions[input_index];
                vertex_binding->binding = u64_to_u32(location);
                vertex_binding->stride = get_size_of_spirv_type(type);
                vertex_binding->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                VkVertexInputAttributeDescription *vertex_attribute = &vertex_input_attribute_descriptions[input_index];
                vertex_attribute->binding = location;
                vertex_attribute->location = location;
                vertex_attribute->format = get_format_from_spirv_type(type);
                vertex_attribute->offset = 0;

                input_index++;
            }
        }

        for (auto &resource : resources.sampled_images)
        {
            U32 set_index = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            U32 binding_number = compiler.get_decoration(resource.id, spv::DecorationBinding);
            
            const auto &type = compiler.get_type(resource.type_id);
            VkDescriptorSetLayoutBinding &binding = find_or_add_binding(sets[set_index], binding_number);
            binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            binding.stageFlags |= get_shader_stage(stage);
            
            U32 descriptor_count = 1;
            if (!type.array.empty())
            {
                descriptor_count = type.array[0];
                if (!descriptor_count)
                {
                    descriptor_count = HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT;
                }
            }

            binding.descriptorCount = descriptor_count;
        }

        for (auto &resource : resources.storage_images)
        {
            U32 set_index = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            U32 binding_number = compiler.get_decoration(resource.id, spv::DecorationBinding);
            
            const auto &type = compiler.get_type(resource.type_id);
            VkDescriptorSetLayoutBinding &binding = find_or_add_binding(sets[set_index], binding_number);
            binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            binding.stageFlags |= get_shader_stage(stage);
            
            U32 descriptor_count = 1;
            if (!type.array.empty())
            {
                descriptor_count = type.array[0];
                if (!descriptor_count)
                {
                    descriptor_count = HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT;
                }
            }

            binding.descriptorCount = descriptor_count;
        }

        auto append_struct = [&](String name, const spirv_cross::SPIRType &type)
        {
            Shader_Struct &struct_ = append(&structs);
            struct_.size = compiler.get_declared_struct_size(type);
            struct_.name = name;

            U32 member_count = u64_to_u32(type.member_types.size());
            struct_.member_count = member_count;
            struct_.members = HE_ALLOCATE_ARRAY(allocator, Shader_Struct_Member, member_count);

            for (U32 i = 0; i < member_count; i++)
            {
                Shader_Struct_Member &member = struct_.members[i];

                auto &member_type = compiler.get_type(type.member_types[i]);
                size_t member_size = compiler.get_declared_struct_member_size(type, i);
                const std::string &name = compiler.get_member_name(type.self, i);
                size_t offset = compiler.type_struct_member_offset(type, i);
                
                member.offset = u64_to_u32(offset);
                member.size = u64_to_u32(member_size);
                member.name = copy_string(HE_STRING(name.c_str()), to_allocator(allocator));
                member.data_type = spirv_type_to_shader_data_type(member_type);

                if (!member_type.array.empty())
                {
                    // Get array stride, e.g. float4 foo[]; Will have array stride of 16 bytes.
                    size_t array_stride = compiler.type_struct_member_array_stride(type, i);
                }

                if (member_type.columns > 1)
                {
                    // Get bytes stride between columns (if column major), for float4x4 -> 16 bytes.
                    size_t matrix_stride = compiler.type_struct_member_matrix_stride(type, i);
                }
            }
        };

        for (auto &resource : resources.uniform_buffers)
        {
            U32 set_index = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            U32 binding_number = compiler.get_decoration(resource.id, spv::DecorationBinding);

            const auto &type = compiler.get_type(resource.type_id);
            VkDescriptorSetLayoutBinding &binding = find_or_add_binding(sets[set_index], binding_number);
            binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.stageFlags |= get_shader_stage(stage);
            
            U32 descriptor_count = 1;
            if (!type.array.empty())
            {
                descriptor_count = type.array[0];
                if (!descriptor_count)
                {
                    descriptor_count = HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT;
                }
            }

            binding.descriptorCount = descriptor_count;

            String name = copy_string(HE_STRING(resource.name.c_str()), to_allocator(allocator));
            append_struct(name, type);
        }

        for (auto &resource : resources.storage_buffers)
        {
            U32 set_index = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            U32 binding_number = compiler.get_decoration(resource.id, spv::DecorationBinding);

            const auto &type = compiler.get_type(resource.type_id);
            VkDescriptorSetLayoutBinding &binding = find_or_add_binding(sets[set_index], binding_number);
            binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.stageFlags |= get_shader_stage(stage);
            
            U32 descriptor_count = 1;
            if (!type.array.empty())
            {
                descriptor_count = type.array[0];
                if (!descriptor_count)
                {
                    descriptor_count = HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT;
                }
            }

            binding.descriptorCount = descriptor_count;

            String name = copy_string(HE_STRING(resource.name.c_str()), to_allocator(allocator));
            append_struct(name, type);
        }
    }

    shader->structs = structs.data; // todo(amer): @Leak
    shader->struct_count = structs.count;

    vulkan_shader->vertex_shader_input_count = vertex_shader_input_count;
    vulkan_shader->vertex_input_binding_descriptions = vertex_input_binding_descriptions;
    vulkan_shader->vertex_input_attribute_descriptions = vertex_input_attribute_descriptions;
    
    U32 set_count = 0;

    for (U32 set_index = 0; set_index < HE_MAX_BIND_GROUP_INDEX_COUNT; set_index++)
    {
        if (!sets[set_index].count)
        {
            break;
        }
        set_count++;
        const auto &set = sets[set_index];

        VkDescriptorBindingFlags *bindings_flags = HE_ALLOCATE_ARRAY(scratch_memory.arena, VkDescriptorBindingFlags, set.count);
        for (U32 i = 0; i < set.count; i++)
        {
            bindings_flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT;
        }

        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_descriptor_set_layout_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
        extended_descriptor_set_layout_create_info.bindingCount = set.count;
        extended_descriptor_set_layout_create_info.pBindingFlags = bindings_flags;

        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descriptor_set_layout_create_info.bindingCount = set.count;
        descriptor_set_layout_create_info.pBindings = set.data;
        descriptor_set_layout_create_info.flags = 0;
        descriptor_set_layout_create_info.pNext = &extended_descriptor_set_layout_create_info;

        vkCreateDescriptorSetLayout(context->logical_device, &descriptor_set_layout_create_info, &context->allocation_callbacks, &vulkan_shader->descriptor_set_layouts[set_index]);
    }

    VkPipelineLayoutCreateInfo pipeline_layout_create_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipeline_layout_create_info.setLayoutCount = set_count;
    pipeline_layout_create_info.pSetLayouts = vulkan_shader->descriptor_set_layouts;
    vkCreatePipelineLayout(context->logical_device, &pipeline_layout_create_info, &context->allocation_callbacks, &vulkan_shader->pipeline_layout);

    return true;
}

void destroy_shader(Shader_Handle shader_handle, Vulkan_Context *context)
{
    Vulkan_Shader *vulkan_shader = &context->shaders[shader_handle.index];

    for (U32 stage_index = 0; stage_index < (U32)Shader_Stage::COUNT; stage_index++)
    {
        if (vulkan_shader->handles[stage_index] == VK_NULL_HANDLE)
        {
            continue;
        }
        vkDestroyShaderModule(context->logical_device, vulkan_shader->handles[stage_index], &context->allocation_callbacks);
    }

    for (U32 i = 0; i < HE_MAX_BIND_GROUP_INDEX_COUNT; i++)
    {
        if (vulkan_shader->descriptor_set_layouts[i] != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(context->logical_device, vulkan_shader->descriptor_set_layouts[i], &context->allocation_callbacks);
            vulkan_shader->descriptor_set_layouts[i] = VK_NULL_HANDLE;
        }
    }

    vkDestroyPipelineLayout(context->logical_device, vulkan_shader->pipeline_layout, &context->allocation_callbacks);
}

static VkPolygonMode get_polygon_mode(Fill_Mode fill_mode)
{
    switch (fill_mode)
    {
        case Fill_Mode::SOLID: return VK_POLYGON_MODE_FILL;
        case Fill_Mode::WIREFRAME: return VK_POLYGON_MODE_LINE;

        default:
        {
            HE_ASSERT(!"unsupported fill mode");
        } break;
    }

    return VK_POLYGON_MODE_MAX_ENUM;
}

static VkCullModeFlagBits get_cull_mode(Cull_Mode cull_mode)
{
    switch (cull_mode)
    {
        case Cull_Mode::NONE: return VK_CULL_MODE_NONE;
        case Cull_Mode::FRONT: return VK_CULL_MODE_FRONT_BIT;
        case Cull_Mode::BACK: return VK_CULL_MODE_BACK_BIT;

        default:
        {
            HE_ASSERT(!"unsupported cull mode");
        } break;
    }

    return VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
}

static VkFrontFace get_front_face(Front_Face front_face)
{
    switch (front_face)
    {
        case Front_Face::COUNTER_CLOCKWISE: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        case Front_Face::CLOCKWISE: return VK_FRONT_FACE_CLOCKWISE;

        default:
        {
            HE_ASSERT(!"unsupported front face");
        } break;
    }

    return VK_FRONT_FACE_MAX_ENUM;
}

static VkCompareOp get_compare_operation(Compare_Operation op)
{
    switch (op)
    {
        case Compare_Operation::NEVER:
        {
            return VK_COMPARE_OP_NEVER;
        } break;

        case Compare_Operation::LESS:
        {
            return VK_COMPARE_OP_LESS;
        } break;

        case Compare_Operation::EQUAL:
        {
            return VK_COMPARE_OP_EQUAL;
        } break;

        case Compare_Operation::LESS_OR_EQUAL:
        {
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        } break;

        case Compare_Operation::GREATER:
        {
            return VK_COMPARE_OP_GREATER;
        } break;

        case Compare_Operation::NOT_EQUAL:
        {
            return VK_COMPARE_OP_NOT_EQUAL;
        } break;

        case Compare_Operation::GREATER_OR_EQUAL:
        {
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        } break;

        case Compare_Operation::ALWAYS:
        {
            return VK_COMPARE_OP_ALWAYS;
        } break;

        default:
        {
            HE_ASSERT(!"unsupported compare operation");
        } break;
    }

    return VK_COMPARE_OP_MAX_ENUM;
}

static VkStencilOp get_stencil_operation(Stencil_Operation op)
{
    switch (op)
    {
        case Stencil_Operation::KEEP: return VK_STENCIL_OP_KEEP;
        case Stencil_Operation::ZERO: return VK_STENCIL_OP_ZERO;
        case Stencil_Operation::REPLACE: return VK_STENCIL_OP_REPLACE;
        case Stencil_Operation::INCREMENT_AND_CLAMP: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case Stencil_Operation::DECREMENT_AND_CLAMP: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case Stencil_Operation::INVERT: return VK_STENCIL_OP_INVERT;
        case Stencil_Operation::INCREMENT_AND_WRAP: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case Stencil_Operation::DECREMENT_AND_WRAP: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default:
        {
            HE_ASSERT(!"unsupported stencil operation");
        } break;
    }

    return VK_STENCIL_OP_MAX_ENUM;
}

bool create_graphics_pipeline(Pipeline_State_Handle pipeline_state_handle,  const Pipeline_State_Descriptor &descriptor, Vulkan_Context *context)
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();

    Renderer_State *renderer_state = context->renderer_state;
    
    Pipeline_State *pipeline_state = get(&renderer_state->pipeline_states, pipeline_state_handle);
    
    Shader *shader = get(&context->renderer_state->shaders, descriptor.shader);
    Vulkan_Shader *vulkan_shader = &context->shaders[descriptor.shader.index];

    Render_Pass *render_pass = get(&renderer_state->render_passes, descriptor.render_pass);
    Vulkan_Pipeline_State *vulkan_pipeline_state = &context->pipeline_states[pipeline_state_handle.index];

    Counted_Array< VkPipelineShaderStageCreateInfo, HE_MAX_SHADER_COUNT_PER_PIPELINE > shader_stage_create_infos = {};

    for (U32 stage_index = 0; stage_index < (U32)Shader_Stage::COUNT; stage_index++)
    {
        if (vulkan_shader->handles[stage_index] == VK_NULL_HANDLE)
        {
            continue;
        }

        VkPipelineShaderStageCreateInfo &pipeline_stage_create_info = append(&shader_stage_create_infos);
        pipeline_stage_create_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        pipeline_stage_create_info.stage  = get_shader_stage((Shader_Stage)stage_index);
        pipeline_stage_create_info.module = vulkan_shader->handles[stage_index];
        pipeline_stage_create_info.pName  = "main";
    }

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertex_input_state_create_info.vertexBindingDescriptionCount = vulkan_shader->vertex_shader_input_count;
    vertex_input_state_create_info.pVertexBindingDescriptions = vulkan_shader->vertex_input_binding_descriptions;
    vertex_input_state_create_info.vertexAttributeDescriptionCount = vulkan_shader->vertex_shader_input_count;
    vertex_input_state_create_info.pVertexAttributeDescriptions = vulkan_shader->vertex_input_attribute_descriptions;

    VkDynamicState dynamic_states[] =
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic_state_create_info.dynamicStateCount = HE_ARRAYCOUNT(dynamic_states);
    dynamic_state_create_info.pDynamicStates = dynamic_states;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
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

    VkPipelineViewportStateCreateInfo viewport_state_create_info = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports = &viewport;
    viewport_state_create_info.scissorCount = 1;
    viewport_state_create_info.pScissors = &sissor;

    VkPipelineRasterizationStateCreateInfo rasterization_state_create_info = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };

    rasterization_state_create_info.depthClampEnable = VK_FALSE;
    rasterization_state_create_info.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state_create_info.polygonMode = get_polygon_mode(descriptor.settings.fill_mode);
    rasterization_state_create_info.lineWidth = 1.0f;
    rasterization_state_create_info.cullMode = get_cull_mode(descriptor.settings.cull_mode);
    rasterization_state_create_info.frontFace = get_front_face(descriptor.settings.front_face);
    rasterization_state_create_info.depthBiasEnable = VK_FALSE;
    rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
    rasterization_state_create_info.depthBiasClamp = 0.0f;
    rasterization_state_create_info.depthBiasSlopeFactor = 0.0f;

    VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT;

    if (render_pass->color_attachments.count)
    {
        sample_count = get_sample_count(render_pass->color_attachments[0].sample_count);
    }
    else if (render_pass->depth_stencil_attachments.count)
    {
        sample_count = get_sample_count(render_pass->depth_stencil_attachments[0].sample_count);
    }

    VkPipelineMultisampleStateCreateInfo multisampling_state_create_info = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling_state_create_info.rasterizationSamples = sample_count;
    multisampling_state_create_info.alphaToCoverageEnable = VK_FALSE;
    multisampling_state_create_info.alphaToOneEnable = VK_FALSE;
    multisampling_state_create_info.sampleShadingEnable = descriptor.settings.sample_shading ? VK_TRUE : VK_FALSE;
    multisampling_state_create_info.minSampleShading = 0.2f;
    multisampling_state_create_info.pSampleMask = nullptr;

    U32 color_mask = 0;

    if (descriptor.settings.color_mask & COLOR_MASK_R)
    {
        color_mask |= VK_COLOR_COMPONENT_R_BIT;
    }

    if (descriptor.settings.color_mask & COLOR_MASK_G)
    {
        color_mask |= VK_COLOR_COMPONENT_G_BIT;
    }

    if (descriptor.settings.color_mask & COLOR_MASK_B)
    {
        color_mask |= VK_COLOR_COMPONENT_B_BIT;
    }

    if (descriptor.settings.color_mask & COLOR_MASK_A)
    {
        color_mask |= VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendAttachmentState *blend_states = HE_ALLOCATE_ARRAY(scratch_memory.arena, VkPipelineColorBlendAttachmentState, render_pass->color_attachments.count);
    for (U32 i = 0; i < render_pass->color_attachments.count; i++)
    {
        const Attachment_Info &info = render_pass->color_attachments[i];
        
        VkPipelineColorBlendAttachmentState &blend_state = blend_states[i];
        blend_state.colorWriteMask = color_mask;

        if (is_color_format_int(info.format) || is_color_format_uint(info.format))
        {
        }
        else
        {
            blend_state.blendEnable = descriptor.settings.alpha_blending ? VK_TRUE : VK_FALSE;
        }
        
        blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_state.colorBlendOp = VK_BLEND_OP_ADD;

        blend_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_state.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    
    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    color_blend_state_create_info.logicOpEnable = VK_FALSE;
    color_blend_state_create_info.logicOp = VK_LOGIC_OP_COPY;
    color_blend_state_create_info.attachmentCount = render_pass->color_attachments.count;
    color_blend_state_create_info.pAttachments = blend_states;
    color_blend_state_create_info.blendConstants[0] = 0.0f;
    color_blend_state_create_info.blendConstants[1] = 0.0f;
    color_blend_state_create_info.blendConstants[2] = 0.0f;
    color_blend_state_create_info.blendConstants[3] = 0.0f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth_stencil_state_create_info.depthTestEnable = descriptor.settings.depth_testing ? VK_TRUE : VK_FALSE;
    depth_stencil_state_create_info.depthWriteEnable = descriptor.settings.depth_writing ? VK_TRUE : VK_FALSE;
    depth_stencil_state_create_info.depthCompareOp = get_compare_operation(descriptor.settings.depth_operation);
    depth_stencil_state_create_info.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state_create_info.minDepthBounds = 0.0f;
    depth_stencil_state_create_info.maxDepthBounds = 1.0f;
    depth_stencil_state_create_info.stencilTestEnable = descriptor.settings.stencil_testing ? VK_TRUE: VK_FALSE;
    depth_stencil_state_create_info.back =
    {
        .failOp = get_stencil_operation(descriptor.settings.depth_fail),
        .passOp = get_stencil_operation(descriptor.settings.stencil_pass),
        .depthFailOp = get_stencil_operation(descriptor.settings.depth_fail),
        .compareOp = get_compare_operation(descriptor.settings.stencil_operation),
        .compareMask = descriptor.settings.stencil_compare_mask,
        .writeMask = descriptor.settings.stencil_write_mask,
        .reference = descriptor.settings.stencil_reference_value
    };
    depth_stencil_state_create_info.front = depth_stencil_state_create_info.back;

    VkGraphicsPipelineCreateInfo graphics_pipeline_create_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    graphics_pipeline_create_info.stageCount = shader_stage_create_infos.count;
    graphics_pipeline_create_info.pStages = shader_stage_create_infos.data;
    graphics_pipeline_create_info.pVertexInputState = &vertex_input_state_create_info;
    graphics_pipeline_create_info.pInputAssemblyState = &input_assembly_state_create_info;
    graphics_pipeline_create_info.pViewportState = &viewport_state_create_info;
    graphics_pipeline_create_info.pRasterizationState = &rasterization_state_create_info;
    graphics_pipeline_create_info.pMultisampleState = &multisampling_state_create_info;
    graphics_pipeline_create_info.pDepthStencilState = &depth_stencil_state_create_info;
    graphics_pipeline_create_info.pColorBlendState = &color_blend_state_create_info;
    graphics_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    graphics_pipeline_create_info.layout = vulkan_shader->pipeline_layout;
    graphics_pipeline_create_info.renderPass = context->render_passes[ descriptor.render_pass.index ].handle;
    graphics_pipeline_create_info.subpass = 0;
    graphics_pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    graphics_pipeline_create_info.basePipelineIndex = -1;

    HE_CHECK_VKRESULT(vkCreateGraphicsPipelines(context->logical_device, context->pipeline_cache, 1, &graphics_pipeline_create_info, &context->allocation_callbacks, &vulkan_pipeline_state->handle));
    return true;
}

void destroy_pipeline(Pipeline_State_Handle pipeline_state_handle, Vulkan_Context *context)
{
    HE_ASSERT(context);
    Vulkan_Pipeline_State *pipeline = &context->pipeline_states[pipeline_state_handle.index];
    vkDestroyPipeline(context->logical_device, pipeline->handle, &context->allocation_callbacks);
}