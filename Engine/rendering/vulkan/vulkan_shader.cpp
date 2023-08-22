#include <spirv-headers/spirv.h>

#include "vulkan_shader.h"
#include "core/platform.h"
#include "core/debugging.h"
#include "core/engine.h"

#include <initializer_list>
#include <vector>

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

    ShaderEntityType_Bool,
    ShaderEntityType_Int,
    ShaderEntityType_Float,

    ShaderEntityType_Vector,
    ShaderEntityType_Matrix,

    ShaderEntityType_Pointer,

    ShaderEntityType_Struct,
    ShaderEntityType_StructMember,

    ShaderEntityType_Array,

    ShaderEntityType_SampledImage
};

struct SPIRV_Struct_Member
{
    String name;

    S32 offset = -1;
    S32 id_of_type = -1;
};

struct SPIRV_Shader_Struct
{
    String name;
    std::vector< Shader_Struct_Member > members;
};

struct Shader_Entity
{
    String name;

    ShaderEntityKind kind = ShaderEntityKind_Unkown;
    ShaderEntityType type = ShaderEntityType_Unkown;
    S32 id_of_type = -1;

    SpvStorageClass storage_class = SpvStorageClassMax;
    SpvDecoration decoration = SpvDecorationMax;

    std::vector< SPIRV_Struct_Member > members;

    S32 component_count = -1;
    S32 element_count = -1;

    S32 location = -1;

    U64 value = 0;

    S32 binding = -1;
    S32 set = -1;

    ShaderDataType data_type;
};

void parse_int(Shader_Entity &entity, const U32 *instruction)
{
    entity.kind = ShaderEntityKind_Type;
    entity.type = ShaderEntityType_Int;

    U32 width = instruction[2];
    U32 signedness = instruction[3];

    if (signedness == 0)
    {
        switch (width)
        {
            case 8:
            {
                entity.data_type = ShaderDataType_U8;
            } break;

            case 16:
            {
                entity.data_type = ShaderDataType_U16;
            } break;

            case 32:
            {
                entity.data_type = ShaderDataType_U32;
            } break;

            case 64:
            {
                entity.data_type = ShaderDataType_U64;
            } break;

            default:
            {
                HOPE_Assert(!"invalid width");
            } break;
        }
    }
    else
    {
        switch (width)
        {
            case 8:
            {
                entity.data_type = ShaderDataType_S8;
            } break;

            case 16:
            {
                entity.data_type = ShaderDataType_S16;
            } break;

            case 32:
            {
                entity.data_type = ShaderDataType_S32;
            } break;

            case 64:
            {
                entity.data_type = ShaderDataType_S64;
            } break;

            default:
            {
                HOPE_Assert(!"invalid width");
            } break;
        }
    }
}

void parse_float(Shader_Entity &entity, const U32* instruction)
{
    entity.kind = ShaderEntityKind_Type;
    entity.type = ShaderEntityType_Float;

    U32 width = instruction[2];
    if (width == 16)
    {
        entity.data_type = ShaderDataType_F16;
    }
    else if (width == 32)
    {
        entity.data_type = ShaderDataType_F32;
    }
    else if (width == 64)
    {
        entity.data_type = ShaderDataType_F64;
    }
    else
    {
        HOPE_Assert(!"invalid width");
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

U32 parse_struct(const Shader_Entity &entity,
                 std::vector< SPIRV_Shader_Struct > &structs,
                 std::vector< Shader_Entity > &ids)
{
    for (U32 struct_index = 0; struct_index < structs.size(); struct_index++)
    {
        if (equal(&entity.name, &structs[struct_index].name) == 0)
        {
            return struct_index;
        }
    }

    SPIRV_Shader_Struct struct_;
    struct_.name = entity.name;
    
    U32 member_count = u64_to_u32(entity.members.size());
    struct_.members.resize(member_count);

    for (U32 member_index = 0; member_index < member_count; member_index++)
    {
        const SPIRV_Struct_Member &spirv_struct_member = entity.members[member_index];
        Shader_Struct_Member &member = struct_.members[member_index];
        member.name = spirv_struct_member.name;
        member.offset = spirv_struct_member.offset;

        Shader_Entity &spirv_struct_member_type = ids[spirv_struct_member.id_of_type];
        member.data_type = spirv_struct_member_type.data_type;
        
        if (spirv_struct_member_type.type == ShaderEntityType_Array)
        {
            member.is_array = true;
            member.array_element_count = spirv_struct_member_type.element_count;
            if (spirv_struct_member_type.data_type == ShaderDataType_Struct)
            {
                const Shader_Entity &array_type = ids[spirv_struct_member_type.id_of_type];
                member.struct_index = parse_struct(array_type, structs, ids);
            }
        }
        else if (spirv_struct_member_type.type == ShaderEntityType_Struct)
        {
            member.struct_index = parse_struct(spirv_struct_member_type, structs, ids);
        }
    }

    structs.push_back(struct_);
    return u64_to_u32(structs.size() - 1);
}

bool
load_shader(Shader *shader, const char *path, Vulkan_Context *context)
{
    Memory_Arena *arena = &context->engine->memory.transient_arena;
    
    Temprary_Memory_Arena temp_arena = {};
    begin_temprary_memory_arena(&temp_arena, arena);

    Vulkan_Shader *vulkan_shader = context->shaders + index_of(&context->engine->renderer_state, shader);

    Read_Entire_File_Result result = read_entire_file(path, &temp_arena);

    if (!result.success)
    {
        return false;
    }

    U8 *data = result.data;
    HOPE_Assert(result.size % 4 == 0);

    U32 *words = (U32 *)data;
    U32 word_count = u64_to_u32(result.size / 4);

    // note(amer): we can infer the endianness out of the magic number
    U32 magic_number = words[0];
    HOPE_Assert(magic_number == SpvMagicNumber);

    U32 id_count = words[3];

    std::vector< Shader_Entity > ids(id_count);

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
                // english mother fucker english do you speak it !!!!!.
                const char *name = (const char*)(instruction + 2);
                entity.name = copy_string(name, string_length(name), context->allocator);
            } break;

            case SpvOpMemberName:
            {
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];

                U32 member_index = instruction[2];
                if (member_index >= entity.members.size())
                {
                    entity.members.push_back(SPIRV_Struct_Member {});
                }
                SPIRV_Struct_Member &member = entity.members[member_index];
                const char *name = (const char*)(instruction + 3);
                member.name = copy_string(name, string_length(name), context->allocator);
            } break;

            case SpvOpEntryPoint:
            {
                SpvExecutionModel model = (SpvExecutionModel)instruction[1];
                switch (model)
                {
                    case SpvExecutionModelVertex:
                    {
                        vulkan_shader->stage = VK_SHADER_STAGE_VERTEX_BIT;
                    } break;

                    case SpvExecutionModelFragment:
                    {
                        vulkan_shader->stage = VK_SHADER_STAGE_FRAGMENT_BIT;
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
                        HOPE_Assert(count <= 4);
                        entity.binding = instruction[3];
                    } break;

                    case SpvDecorationDescriptorSet:
                    {
                        HOPE_Assert(count <= 4);
                        entity.set = instruction[3];
                    } break;

                    case SpvDecorationLocation:
                    {
                        HOPE_Assert(count <= 4);
                        entity.location = instruction[3];
                    } break;
                }

            } break;

            case SpvOpMemberDecorate:
            {
                U32 id = instruction[1];
                Shader_Entity &struct_entity = ids[id];
                U32 member_index = instruction[2];

                if (member_index >= struct_entity.members.size())
                {
                    struct_entity.members.push_back(SPIRV_Struct_Member{});
                }

                SPIRV_Struct_Member &member = struct_entity.members[member_index];
                U32 decoration = instruction[3];

                switch (decoration)
                {
                    case SpvDecorationOffset:
                    {
                        member.offset = instruction[4];
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
                HOPE_Assert(count <= 4);
                U32 id = instruction[2];
                Shader_Entity &entity = ids[id];

                entity.kind = ShaderEntityKind_Variable;
                entity.id_of_type = instruction[1];
                entity.storage_class = SpvStorageClass(instruction[3]);
            } break;

            case SpvOpTypeBool:
            {
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_Bool;
                entity.data_type = ShaderDataType_Bool;
            } break;

            case SpvOpTypeInt:
            {
                HOPE_Assert(count <= 4);
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                parse_int(entity, instruction);
            }  break;

            case SpvOpTypeFloat:
            {
                HOPE_Assert(count <= 3);
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                parse_float(entity, instruction);
            } break;

            case SpvOpTypeVector:
            {
                HOPE_Assert(count <= 4);
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_Vector;
                entity.id_of_type = instruction[2];
                entity.component_count = instruction[3];
                const Shader_Entity& type_entity = ids[entity.id_of_type];
                if (type_entity.type == ShaderEntityType_Float)
                {
                    if (entity.component_count == 2)
                    {
                        entity.data_type = ShaderDataType_Vector2f;
                    }
                    else if (entity.component_count == 3)
                    {
                        entity.data_type = ShaderDataType_Vector3f;
                    }
                    else if (entity.component_count == 4)
                    {
                        entity.data_type = ShaderDataType_Vector4f;
                    }
                }
            } break;

            case SpvOpTypeMatrix:
            {
                HOPE_Assert(count <= 4);
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_Matrix;
                entity.id_of_type = instruction[2];
                entity.component_count = instruction[3];
                const Shader_Entity &vector_type_entity = ids[ids[entity.id_of_type].id_of_type];
                if (entity.component_count == 3)
                {
                    entity.data_type = ShaderDataType_Matrix3f;
                }
                else if (entity.component_count == 4)
                {
                    entity.data_type = ShaderDataType_Matrix4f;
                }
            } break;

            case SpvOpTypePointer:
            {
                HOPE_Assert(count <= 4);
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
                entity.data_type = ShaderDataType_Struct;
                U32 member_count = count - 2;
                const U32 *member_instruction = &instruction[2];
                for (U32 member_index = 0; member_index < member_count; member_index++)
                {
                    entity.members[member_index].id_of_type = member_instruction[member_index];
                }
            } break;

            case SpvOpTypeArray:
            {
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_Array;
                entity.id_of_type = instruction[2];
                const Shader_Entity &type_entity = ids[entity.id_of_type];
                entity.data_type = type_entity.data_type;
                U32 length_id = instruction[3];
                Shader_Entity &length = ids[length_id];
                entity.element_count = u64_to_u32(length.value);
            } break;

            case SpvOpTypeRuntimeArray:
            {
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_Array;
                entity.id_of_type = instruction[2];
                const Shader_Entity &type_entity = ids[entity.id_of_type];
                entity.data_type = type_entity.data_type;
                entity.element_count = MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT;
            } break;

            case SpvOpTypeSampledImage:
            {
                HOPE_Assert(count <= 3);
                U32 id = instruction[1];
                Shader_Entity &entity = ids[id];
                entity.kind = ShaderEntityKind_Type;
                entity.type = ShaderEntityType_SampledImage;
                entity.id_of_type = instruction[2];
            } break;
        }

        instruction += count;
    }

    std::vector< VkDescriptorSetLayoutBinding > sets[MAX_DESCRIPTOR_SET_COUNT];
    std::vector< Shader_Input_Variable > inputs;
    std::vector< Shader_Output_Variable > outputs;
    std::vector< SPIRV_Shader_Struct > structs;

    for (const Shader_Entity &entity : ids)
    {
        if (entity.kind == ShaderEntityKind_Variable)
        {
            switch (entity.storage_class)
            {
                case SpvStorageClassUniform:
                case SpvStorageClassUniformConstant:
                {
                    HOPE_Assert(entity.set >= 0 && entity.set < MAX_DESCRIPTOR_SET_COUNT);

                    auto &set = sets[entity.set];
                    set.push_back(VkDescriptorSetLayoutBinding {});
                    VkDescriptorSetLayoutBinding &descriptor_set_layout_binding = set.back();

                    descriptor_set_layout_binding.binding = entity.binding;
                    descriptor_set_layout_binding.stageFlags = vulkan_shader->stage;

                    const Shader_Entity &uniform = ids[ ids[ entity.id_of_type ].id_of_type ];

                    if (uniform.type == ShaderEntityType_Array)
                    {
                        descriptor_set_layout_binding.descriptorCount = uniform.element_count;
                        const Shader_Entity &element_type = ids[uniform.id_of_type];
                        set_descriptor_type(descriptor_set_layout_binding, element_type);
                    }
                    else if (uniform.type == ShaderEntityType_Struct)
                    {
                        descriptor_set_layout_binding.descriptorCount = 1;
                        set_descriptor_type(descriptor_set_layout_binding, uniform);
                        parse_struct(uniform, structs, ids);
                    }
                } break;

                case SpvStorageClassInput:
                {
                    const Shader_Entity &input_type = ids[ ids[ entity.id_of_type ].id_of_type ];
                    if (entity.location != -1)
                    {
                        Shader_Input_Variable input = {};
                        input.name = entity.name;
                        input.location = entity.location;
                        input.type = entity.data_type;
                        inputs.push_back(input);
                    }
                } break;

                case SpvStorageClassOutput:
                {
                    const Shader_Entity& input_type = ids[ids[entity.id_of_type].id_of_type];
                    if (entity.location != -1)
                    {
                        Shader_Output_Variable output = {};
                        output.name = entity.name;
                        output.location = entity.location;
                        output.type = entity.data_type;
                        outputs.push_back(output);
                    }
                }
            }
        }
    }

    VkShaderModuleCreateInfo shader_create_info =
        { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    shader_create_info.codeSize = result.size;
    shader_create_info.pCode = (U32 *)data;

    CheckVkResult(vkCreateShaderModule(context->logical_device,
                                       &shader_create_info,
                                       nullptr,
                                       &vulkan_shader->handle));

    end_temprary_memory_arena(&temp_arena);

    for (U32 set_index = 0; set_index < MAX_DESCRIPTOR_SET_COUNT; set_index++)
    {
        U32 binding_count = u64_to_u32(sets[set_index].size());
        if (!binding_count)
        {
            continue;
        }

        Vulkan_Descriptor_Set *set = &vulkan_shader->sets[set_index];
        set->binding_count = binding_count;
        set->bindings = AllocateArray(arena, VkDescriptorSetLayoutBinding, binding_count);
        for (U32 binding_index = 0; binding_index < binding_count; binding_index++)
        {
            set->bindings[binding_index] = sets[set_index][binding_index];
        }
    }

    U32 input_count = u64_to_u32(inputs.size());
    Shader_Input_Variable *input_variables = AllocateArray(arena, Shader_Input_Variable, input_count);
    memcpy(input_variables, inputs.data(), sizeof(Shader_Input_Variable) * input_count);

    U32 output_count = u64_to_u32(outputs.size());
    Shader_Output_Variable *output_variables = AllocateArray(arena, Shader_Output_Variable, input_count);
    memcpy(output_variables, outputs.data(), sizeof(Shader_Output_Variable) * output_count);

    shader->input_count = input_count;
    shader->inputs = input_variables;

    shader->output_count = output_count;
    shader->outputs = output_variables;

    U32 struct_count = u64_to_u32(structs.size());
    Shader_Struct *shader_structs = AllocateArray(arena, Shader_Struct, struct_count);

    for (U32 struct_index = 0; struct_index < struct_count; struct_index++)
    {
        const SPIRV_Shader_Struct &spirv_struct = structs[struct_index];
        Shader_Struct *shader_struct = &shader_structs[struct_index];
        shader_struct->name = spirv_struct.name;
        
        U32 member_count = u64_to_u32(spirv_struct.members.size());
        shader_struct->member_count = member_count;
        shader_struct->members = AllocateArray(arena, Shader_Struct_Member, member_count);
        copy_memory(shader_struct->members, spirv_struct.members.data(), sizeof(Shader_Struct_Member) * member_count);
    }

    shader->struct_count = struct_count;
    shader->structs = shader_structs;

    return true;
}

void destroy_shader(Shader *shader, Vulkan_Context *context)
{
    Vulkan_Shader *vulkan_shader = context->shaders + index_of(&context->engine->renderer_state, shader);
    vkDestroyShaderModule(context->logical_device, vulkan_shader->handle, nullptr);
}

static void
combine_stage_flags_or_add_binding_if_not_found(std::vector<VkDescriptorSetLayoutBinding> &set,
                                                const VkDescriptorSetLayoutBinding &descriptor_set_layout_binding)
{
    U32 binding_count = u64_to_u32(set.size());
    for (U32 binding_index = 0; binding_index < binding_count; binding_index++)
    {
        if (set[binding_index].binding == descriptor_set_layout_binding.binding)
        {
            set[binding_index].stageFlags |= descriptor_set_layout_binding.stageFlags;
            return;
        }
    }
    set.push_back(descriptor_set_layout_binding);
}

bool create_graphics_pipeline(Pipeline_State *pipeline_state,
                              const std::initializer_list< const Shader * > &shaders,
                              VkRenderPass render_pass,
                              Vulkan_Context *context)
{
    U32 shader_count = (U32)shaders.size();
    Shader **shaders_ = AllocateArray(context->allocator, Shader *, shader_count);
    U32 shader_index_ = 0;
    for (const Shader *shader : shaders)
    {
        shaders_[shader_index_++] = const_cast< Shader * >(shader);
    }
    pipeline_state->shader_count = shader_count;
    pipeline_state->shaders = shaders_;

    Vulkan_Pipeline_State *pipeline = context->pipeline_states + index_of(&context->engine->renderer_state, pipeline_state);
    std::vector< VkPipelineShaderStageCreateInfo > shader_stage_create_infos(shaders.size());

    bool is_using_vertex_shader = false;
    std::vector<VkDescriptorSetLayoutBinding> sets[MAX_DESCRIPTOR_SET_COUNT];
    U32 shader_index = 0;

    for (const Shader *shader : shaders)
    {
        Vulkan_Shader *vulkan_shader = context->shaders + index_of(&context->engine->renderer_state, shader);

        VkPipelineShaderStageCreateInfo &pipeline_stage_create_info = shader_stage_create_infos[shader_index++];
        pipeline_stage_create_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline_stage_create_info.stage  = vulkan_shader->stage;
        pipeline_stage_create_info.module = vulkan_shader->handle;
        pipeline_stage_create_info.pName  = "main";

        if (vulkan_shader->stage == VK_SHADER_STAGE_VERTEX_BIT)
        {
            is_using_vertex_shader = true;
        }

        for (U32 set_index = 0; set_index < MAX_DESCRIPTOR_SET_COUNT; set_index++)
        {
            const Vulkan_Descriptor_Set &set = vulkan_shader->sets[set_index];
            for (U32 binding_index = 0;
                 binding_index < set.binding_count;
                 binding_index++)
            {
                const VkDescriptorSetLayoutBinding &descriptor_set_layout_binding = set.bindings[binding_index];
                combine_stage_flags_or_add_binding_if_not_found(sets[set_index], descriptor_set_layout_binding);
            }
        }
    }

    U32 set_count = MAX_DESCRIPTOR_SET_COUNT;

    for (U32 set_index = 0; set_index < MAX_DESCRIPTOR_SET_COUNT; set_index++)
    {
        bool is_first_empty_set = sets[set_index].size() == 0;
        if (is_first_empty_set)
        {
            set_count = set_index;
            break;
        }
    }

    pipeline->descriptor_set_layout_count = set_count;

    for (U32 set_index = 0; set_index < set_count; set_index++)
    {
        const std::vector< VkDescriptorSetLayoutBinding > &set = sets[set_index];

        const U32 binding_count = u64_to_u32(set.size());
        std::vector< VkDescriptorBindingFlags > layout_bindings_flags(set.size(), VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT);

        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_descriptor_set_layout_create_info =
        { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
        extended_descriptor_set_layout_create_info.bindingCount = binding_count;
        extended_descriptor_set_layout_create_info.pBindingFlags = layout_bindings_flags.data();

        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info =
            { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };

        descriptor_set_layout_create_info.bindingCount = binding_count;
        descriptor_set_layout_create_info.pBindings = set.data();
        descriptor_set_layout_create_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        descriptor_set_layout_create_info.pNext = &extended_descriptor_set_layout_create_info;

        CheckVkResult(vkCreateDescriptorSetLayout(context->logical_device,
                                                  &descriptor_set_layout_create_info,
                                                  nullptr,
                                                  &pipeline->descriptor_set_layouts[set_index]));
    }

    VkVertexInputBindingDescription vertex_input_binding_description = {};
    vertex_input_binding_description.binding = 0;
    vertex_input_binding_description.stride = sizeof(Vertex);
    vertex_input_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_input_attribute_descriptions[5] = {};
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
    vertex_input_attribute_descriptions[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_input_attribute_descriptions[2].offset   = offsetof(Vertex, tangent);

    vertex_input_attribute_descriptions[3].binding  = 0;
    vertex_input_attribute_descriptions[3].location = 3;
    vertex_input_attribute_descriptions[3].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_input_attribute_descriptions[3].offset   = offsetof(Vertex, bitangent);

    vertex_input_attribute_descriptions[4].binding  = 0;
    vertex_input_attribute_descriptions[4].location = 4;
    vertex_input_attribute_descriptions[4].format   = VK_FORMAT_R32G32_SFLOAT;
    vertex_input_attribute_descriptions[4].offset   = offsetof(Vertex, uv);

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    vertex_input_state_create_info.vertexBindingDescriptionCount = 1;
    vertex_input_state_create_info.pVertexBindingDescriptions = &vertex_input_binding_description;
    vertex_input_state_create_info.vertexAttributeDescriptionCount = HOPE_ArrayCount(vertex_input_attribute_descriptions);
    vertex_input_state_create_info.pVertexAttributeDescriptions = vertex_input_attribute_descriptions;

    VkDynamicState dynamic_states[] =
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic_state_create_info.dynamicStateCount = HOPE_ArrayCount(dynamic_states);
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

    multisampling_state_create_info.rasterizationSamples = context->msaa_samples;
    multisampling_state_create_info.alphaToCoverageEnable = VK_FALSE;
    multisampling_state_create_info.alphaToOneEnable = VK_FALSE;
    multisampling_state_create_info.sampleShadingEnable = VK_TRUE;
    multisampling_state_create_info.minSampleShading = 0.2f;
    multisampling_state_create_info.pSampleMask = nullptr;

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

    VkPipelineLayoutCreateInfo pipeline_layout_create_info =
        { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipeline_layout_create_info.setLayoutCount = pipeline->descriptor_set_layout_count;
        pipeline_layout_create_info.pSetLayouts = pipeline->descriptor_set_layouts;
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
    graphics_pipeline_create_info.stageCount = u64_to_u32(shader_stage_create_infos.size());
    graphics_pipeline_create_info.pStages = shader_stage_create_infos.data();

    if (is_using_vertex_shader)
    {
        graphics_pipeline_create_info.pVertexInputState = &vertex_input_state_create_info;
        graphics_pipeline_create_info.pInputAssemblyState = &input_assembly_state_create_info;
    }

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

    CheckVkResult(vkCreateGraphicsPipelines(context->logical_device, context->pipeline_cache,
                                            1, &graphics_pipeline_create_info,
                                            nullptr, &pipeline->handle));

    return true;
}

void destroy_pipeline(Pipeline_State *pipeline_state, Vulkan_Context *context)
{
    HOPE_Assert(pipeline_state);
    HOPE_Assert(context);
    Vulkan_Pipeline_State *pipeline = context->pipeline_states + index_of(&context->engine->renderer_state, pipeline_state);

    for (U32 set_index = 0; set_index < pipeline->descriptor_set_layout_count; set_index++)
    {
        vkDestroyDescriptorSetLayout(context->logical_device,
                                     pipeline->descriptor_set_layouts[set_index],
                                     nullptr);
    }

    vkDestroyPipelineLayout(context->logical_device, pipeline->layout, nullptr);
    vkDestroyPipeline(context->logical_device, pipeline->handle, nullptr);
}