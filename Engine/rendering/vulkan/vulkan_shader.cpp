#include <spirv-headers/spirv.h>

#include "vulkan_shader.h"
#include "vulkan_utils.h"

#include "core/platform.h"
#include "core/debugging.h"
#include "core/engine.h"
#include "core/file_system.h"

#include "containers/dynamic_array.h"

static Vulkan_Context *vulkan_context;

enum class SPRIV_Shader_Entity_Kind
{
    UNKNOWN,

    CONSTANT,
    VARIABLE,

    TYPE,
};

enum class SPRIV_Shader_Entity_Type
{
    UNKNOWN,

    BOOL,
    INT,
    FLOAT,

    VECTOR,
    MATRIX,

    POINTER,

    STRUCT,
    STRUCT_MEMBER,

    ARRAY,

    SAMPLED_IMAGE
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
    Dynamic_Array< Shader_Struct_Member > members;
};

struct SPIRV_Entity
{
    String name;

    SPRIV_Shader_Entity_Kind kind = SPRIV_Shader_Entity_Kind::UNKNOWN;
    SPRIV_Shader_Entity_Type type = SPRIV_Shader_Entity_Type::UNKNOWN;
    S32 id_of_type = -1;

    SpvStorageClass storage_class = SpvStorageClassMax;
    SpvDecoration decoration = SpvDecorationMax;

    Dynamic_Array< SPIRV_Struct_Member > members;

    S32 component_count = -1;
    S32 element_count = -1;

    S32 location = -1;

    U64 value = 0;

    S32 binding = -1;
    S32 set = -1;

    Shader_Data_Type data_type;
};

static void parse_int(SPIRV_Entity &entity, const U32 *instruction)
{
    entity.kind = SPRIV_Shader_Entity_Kind::TYPE;
    entity.type = SPRIV_Shader_Entity_Type::INT;

    U32 width = instruction[2];
    U32 signedness = instruction[3];

    if (signedness == 0)
    {
        switch (width)
        {
            case 8:
            {
                entity.data_type = Shader_Data_Type::U8;
            } break;

            case 16:
            {
                entity.data_type = Shader_Data_Type::U16;
            } break;

            case 32:
            {
                entity.data_type = Shader_Data_Type::U32;
            } break;

            case 64:
            {
                entity.data_type = Shader_Data_Type::U64;
            } break;

            default:
            {
                HE_ASSERT(!"invalid width");
            } break;
        }
    }
    else
    {
        switch (width)
        {
            case 8:
            {
                entity.data_type = Shader_Data_Type::S8;
            } break;

            case 16:
            {
                entity.data_type = Shader_Data_Type::S16;
            } break;

            case 32:
            {
                entity.data_type = Shader_Data_Type::S32;
            } break;

            case 64:
            {
                entity.data_type = Shader_Data_Type::S64;
            } break;

            default:
            {
                HE_ASSERT(!"invalid width");
            } break;
        }
    }
}

static void parse_float(SPIRV_Entity &entity, const U32* instruction)
{
    entity.kind = SPRIV_Shader_Entity_Kind::TYPE;
    entity.type = SPRIV_Shader_Entity_Type::FLOAT;

    U32 width = instruction[2];
    if (width == 16)
    {
        entity.data_type = Shader_Data_Type::F16;
    }
    else if (width == 32)
    {
        entity.data_type = Shader_Data_Type::F32;
    }
    else if (width == 64)
    {
        entity.data_type = Shader_Data_Type::F64;
    }
    else
    {
        HE_ASSERT(!"invalid width");
    }
}

static void set_descriptor_type(Binding &binding, const SPIRV_Entity &shader_entity)
{
    switch (shader_entity.type)
    {
        case SPRIV_Shader_Entity_Type::STRUCT:
        {
            if (shader_entity.decoration == SpvDecorationBlock)
            {
                binding.type = Binding_Type::UNIFORM_BUFFER;
            }
            else if (shader_entity.decoration == SpvDecorationBufferBlock)
            {
                binding.type = Binding_Type::STORAGE_BUFFER;
            }
        } break;

        case SPRIV_Shader_Entity_Type::SAMPLED_IMAGE:
        {
            binding.type = Binding_Type::COMBINED_IMAGE_SAMPLER;
        } break;
    }
}

U32 parse_struct(const SPIRV_Entity &entity, Dynamic_Array< SPIRV_Shader_Struct > &structs, SPIRV_Entity *ids)
{
    for (U32 struct_index = 0; struct_index < structs.count; struct_index++)
    {
        if (entity.name == structs[struct_index].name)
        {
            return struct_index;
        }
    }

    SPIRV_Shader_Struct struct_;
    struct_.name = entity.name;
    
    U32 member_count = entity.members.count;
    init(&struct_.members, vulkan_context->allocator, member_count);

    for (U32 member_index = 0; member_index < member_count; member_index++)
    {
        const SPIRV_Struct_Member &spirv_struct_member = entity.members[member_index];
        Shader_Struct_Member &member = struct_.members[member_index];
        member.name = spirv_struct_member.name;
        member.offset = spirv_struct_member.offset;

        SPIRV_Entity &spirv_struct_member_type = ids[spirv_struct_member.id_of_type];
        member.data_type = spirv_struct_member_type.data_type;
        
        if (spirv_struct_member_type.type == SPRIV_Shader_Entity_Type::ARRAY)
        {
            member.is_array = true;
            member.array_element_count = spirv_struct_member_type.element_count;
            if (spirv_struct_member_type.data_type == Shader_Data_Type::STRUCT)
            {
                const SPIRV_Entity &array_type = ids[spirv_struct_member_type.id_of_type];
                member.struct_index = parse_struct(array_type, structs, ids);
            }
        }
        else if (spirv_struct_member_type.type == SPRIV_Shader_Entity_Type::STRUCT)
        {
            member.struct_index = parse_struct(spirv_struct_member_type, structs, ids);
        }
    }

    append(&structs, struct_);
    return u64_to_u32(structs.count - 1);
}

VkShaderStageFlagBits get_shader_stage(Shader_Stage shader_stage)
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

bool load_shader(Shader_Handle shader_handle, const char *path, Vulkan_Context *context)
{
    if (!vulkan_context)
    {
        vulkan_context = context;
    }

    Shader *shader = get(&context->renderer_state->shaders, shader_handle);
    Vulkan_Shader *vulkan_shader = &context->shaders[shader_handle.index];

    Memory_Arena *arena = &context->arena;
    
    Temprary_Memory_Arena temp_arena = {};
    begin_temprary_memory_arena(&temp_arena, arena);

    Read_Entire_File_Result result = read_entire_file(path, &temp_arena);

    if (!result.success)
    {
        return false;
    }

    U8 *data = result.data;
    HE_ASSERT(result.size % 4 == 0);

    U32 *words = (U32 *)data;
    U32 word_count = u64_to_u32(result.size / 4);

    // note(amer): we can infer the endianness out of the magic number
    U32 magic_number = words[0];
    HE_ASSERT(magic_number == SpvMagicNumber);

    U32 id_count = words[3];

    SPIRV_Entity *ids = HE_ALLOCATE_ARRAY(&temp_arena, SPIRV_Entity, id_count);
    for (U32 id_index = 0; id_index < id_count; id_index++)
    {
        ids[id_index] = SPIRV_Entity {};
    }

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
                SPIRV_Entity &entity = ids[id];

                // note(amer): not proper utf8 paring here keep the shaders in english please.
                // english mother fucker english do you speak it !!!!!.
                const char *name = (const char*)(instruction + 2);
                entity.name = copy_string(HE_STRING(name), context->allocator);
            } break;

            case SpvOpMemberName:
            {
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];

                U32 member_index = instruction[2];

                if (!entity.members.data)
                {
                    init(&entity.members, context->allocator);
                }

                if (member_index >= entity.members.count)
                {
                    append(&entity.members, SPIRV_Struct_Member {});
                }
                SPIRV_Struct_Member &member = entity.members[member_index];
                const char *name = (const char*)(instruction + 3);
                member.name = copy_string(HE_STRING(name), context->allocator);
            } break;

            case SpvOpEntryPoint:
            {
                SpvExecutionModel model = (SpvExecutionModel)instruction[1];
                switch (model)
                {
                    case SpvExecutionModelVertex:
                    {
                        shader->stage = Shader_Stage::VERTEX;
                    } break;

                    case SpvExecutionModelFragment:
                    {
                        shader->stage = Shader_Stage::FRAGMENT;
                    } break;

                    default:
                    {
                        HE_ASSERT(!"unsupported");
                    } break;
                }
            } break;

            case SpvOpDecorate:
            {
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];

                SpvDecoration decoration = (SpvDecoration)instruction[2];
                entity.decoration = decoration;

                switch (decoration)
                {
                    case SpvDecorationBinding:
                    {
                        HE_ASSERT(count <= 4);
                        entity.binding = instruction[3];
                    } break;

                    case SpvDecorationDescriptorSet:
                    {
                        HE_ASSERT(count <= 4);
                        entity.set = instruction[3];
                    } break;

                    case SpvDecorationLocation:
                    {
                        HE_ASSERT(count <= 4);
                        entity.location = instruction[3];
                    } break;
                }

            } break;

            case SpvOpMemberDecorate:
            {
                U32 id = instruction[1];
                SPIRV_Entity &struct_entity = ids[id];
                U32 member_index = instruction[2];

                if (member_index >= struct_entity.members.count)
                {
                    append(&struct_entity.members, SPIRV_Struct_Member{});
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
                SPIRV_Entity &entity = ids[id];
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
                HE_ASSERT(count <= 4);
                U32 id = instruction[2];
                SPIRV_Entity &entity = ids[id];

                entity.kind = SPRIV_Shader_Entity_Kind::VARIABLE;
                entity.id_of_type = instruction[1];
                entity.storage_class = SpvStorageClass(instruction[3]);
            } break;

            case SpvOpTypeBool:
            {
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];
                entity.kind = SPRIV_Shader_Entity_Kind::TYPE;
                entity.type = SPRIV_Shader_Entity_Type::BOOL;
                entity.data_type = Shader_Data_Type::BOOL;
            } break;

            case SpvOpTypeInt:
            {
                HE_ASSERT(count <= 4);
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];
                parse_int(entity, instruction);
            }  break;

            case SpvOpTypeFloat:
            {
                HE_ASSERT(count <= 3);
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];
                parse_float(entity, instruction);
            } break;

            case SpvOpTypeVector:
            {
                HE_ASSERT(count <= 4);
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];
                entity.kind = SPRIV_Shader_Entity_Kind::TYPE;
                entity.type = SPRIV_Shader_Entity_Type::VECTOR;
                entity.id_of_type = instruction[2];
                entity.component_count = instruction[3];
                const SPIRV_Entity& type_entity = ids[entity.id_of_type];
                if (type_entity.type == SPRIV_Shader_Entity_Type::FLOAT)
                {
                    if (entity.component_count == 2)
                    {
                        entity.data_type = Shader_Data_Type::VECTOR2F;
                    }
                    else if (entity.component_count == 3)
                    {
                        entity.data_type = Shader_Data_Type::VECTOR3F;
                    }
                    else if (entity.component_count == 4)
                    {
                        entity.data_type = Shader_Data_Type::VECTOR4F;
                    }
                }
            } break;

            case SpvOpTypeMatrix:
            {
                HE_ASSERT(count <= 4);
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];
                entity.kind = SPRIV_Shader_Entity_Kind::TYPE;
                entity.type = SPRIV_Shader_Entity_Type::MATRIX;
                entity.id_of_type = instruction[2];
                entity.component_count = instruction[3];
                const SPIRV_Entity &vector_type_entity = ids[ids[entity.id_of_type].id_of_type];
                if (entity.component_count == 3)
                {
                    entity.data_type = Shader_Data_Type::MATRIX3F;
                }
                else if (entity.component_count == 4)
                {
                    entity.data_type = Shader_Data_Type::MATRIX4F;
                }
            } break;

            case SpvOpTypePointer:
            {
                HE_ASSERT(count <= 4);
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];
                entity.kind = SPRIV_Shader_Entity_Kind::TYPE;
                entity.type = SPRIV_Shader_Entity_Type::POINTER;
                entity.storage_class = SpvStorageClass(instruction[2]);
                entity.id_of_type = instruction[3];
            } break;

            case SpvOpTypeForwardPointer:
            {
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];
                entity.kind = SPRIV_Shader_Entity_Kind::TYPE;
                entity.type = SPRIV_Shader_Entity_Type::POINTER;
                entity.storage_class = SpvStorageClass(instruction[2]);
            } break;

            case SpvOpTypeStruct:
            {
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];
                entity.kind = SPRIV_Shader_Entity_Kind::TYPE;
                entity.type = SPRIV_Shader_Entity_Type::STRUCT;
                entity.data_type = Shader_Data_Type::STRUCT;
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
                SPIRV_Entity &entity = ids[id];
                entity.kind = SPRIV_Shader_Entity_Kind::TYPE;
                entity.type = SPRIV_Shader_Entity_Type::ARRAY;
                entity.id_of_type = instruction[2];
                const SPIRV_Entity &type_entity = ids[entity.id_of_type];
                entity.data_type = type_entity.data_type;
                U32 length_id = instruction[3];
                SPIRV_Entity &length = ids[length_id];
                entity.element_count = u64_to_u32(length.value);
            } break;

            case SpvOpTypeRuntimeArray:
            {
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];
                entity.kind = SPRIV_Shader_Entity_Kind::TYPE;
                entity.type = SPRIV_Shader_Entity_Type::ARRAY;
                entity.id_of_type = instruction[2];
                const SPIRV_Entity &type_entity = ids[entity.id_of_type];
                entity.data_type = type_entity.data_type;
                entity.element_count = HE_MAX_BINDLESS_RESOURCE_DESCRIPTOR_COUNT;
            } break;

            case SpvOpTypeSampledImage:
            {
                HE_ASSERT(count <= 3);
                U32 id = instruction[1];
                SPIRV_Entity &entity = ids[id];
                entity.kind = SPRIV_Shader_Entity_Kind::TYPE;
                entity.type = SPRIV_Shader_Entity_Type::SAMPLED_IMAGE;
                entity.id_of_type = instruction[2];
            } break;
        }

        instruction += count;
    }

    Dynamic_Array< Binding > sets[HE_MAX_DESCRIPTOR_SET_COUNT];

    for (U32 set_layout_binding_index = 0; set_layout_binding_index < HE_MAX_DESCRIPTOR_SET_COUNT; set_layout_binding_index++)
    {
        init(&sets[set_layout_binding_index], context->allocator);
    }

    Dynamic_Array< Shader_Input_Variable > inputs;
    init(&inputs, context->allocator);

    Dynamic_Array< Shader_Output_Variable > outputs;
    init(&outputs, context->allocator);

    Dynamic_Array< SPIRV_Shader_Struct > structs;
    init(&structs, context->allocator);

    HE_DEFER
    {
        for (U32 set_layout_binding_index = 0; set_layout_binding_index < HE_MAX_DESCRIPTOR_SET_COUNT; set_layout_binding_index++)
        {
            deinit(&sets[set_layout_binding_index]);
        }
        deinit(&inputs);
        deinit(&outputs);
        deinit(&structs);
    };

    for (U32 entity_index = 0; entity_index < id_count; entity_index++)
    {
        const SPIRV_Entity &entity = ids[entity_index];

        if (entity.kind == SPRIV_Shader_Entity_Kind::VARIABLE)
        {
            switch (entity.storage_class)
            {
                case SpvStorageClassUniform:
                case SpvStorageClassUniformConstant:
                {
                    HE_ASSERT(entity.set >= 0 && entity.set < HE_MAX_DESCRIPTOR_SET_COUNT);

                    auto &set = sets[entity.set];
                    append(&set, Binding {});
                    Binding &binding = back(&set);
                    binding.stage_flags = get_shader_stage(shader->stage);

                    binding.number = entity.binding;

                    const SPIRV_Entity &uniform = ids[ ids[ entity.id_of_type ].id_of_type ];

                    if (uniform.type == SPRIV_Shader_Entity_Type::ARRAY)
                    {
                        binding.count = uniform.element_count;
                        const SPIRV_Entity &element_type = ids[uniform.id_of_type];
                        set_descriptor_type(binding, element_type);
                    }
                    else if (uniform.type == SPRIV_Shader_Entity_Type::STRUCT)
                    {
                        binding.count = 1;
                        set_descriptor_type(binding, uniform);
                        parse_struct(uniform, structs, ids);
                    }
                } break;

                case SpvStorageClassInput:
                {
                    const SPIRV_Entity &type_entity = ids[ ids[ entity.id_of_type ].id_of_type ];
                    if (entity.location != -1)
                    {
                        Shader_Input_Variable input = {};
                        input.name = entity.name;
                        input.location = entity.location;
                        input.data_type = type_entity.data_type;
                        append(&inputs, input);
                    }
                } break;

                case SpvStorageClassOutput:
                {
                    const SPIRV_Entity &type_entity = ids[ ids [ entity.id_of_type ].id_of_type ];
                    if (entity.location != -1)
                    {
                        Shader_Output_Variable output = {};
                        output.name = entity.name;
                        output.location = entity.location;
                        output.data_type = type_entity.data_type;
                        append(&outputs, output);
                    }
                }
            }
        }
    }

    VkShaderModuleCreateInfo shader_create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    shader_create_info.codeSize = result.size;
    shader_create_info.pCode = (U32 *)data;

    HE_CHECK_VKRESULT(vkCreateShaderModule(context->logical_device, &shader_create_info, nullptr, &vulkan_shader->handle));

    end_temprary_memory_arena(&temp_arena);

    for (U32 set_index = 0; set_index < HE_MAX_DESCRIPTOR_SET_COUNT; set_index++)
    {
        U32 binding_count = sets[set_index].count;
        if (!binding_count)
        {
            continue;
        }

        Bind_Group_Layout_Descriptor *set = &shader->sets[set_index];
        set->binding_count = binding_count;
        set->bindings = HE_ALLOCATE_ARRAY(arena, Binding, binding_count);
        for (U32 binding_index = 0; binding_index < binding_count; binding_index++)
        {
            set->bindings[binding_index] = sets[set_index][binding_index];
        }
    }

    U32 input_count = inputs.count;
    Shader_Input_Variable *input_variables = HE_ALLOCATE_ARRAY(arena, Shader_Input_Variable, input_count);
    memcpy(input_variables, inputs.data, sizeof(Shader_Input_Variable) * input_count);

    U32 output_count = outputs.count;
    Shader_Output_Variable *output_variables = HE_ALLOCATE_ARRAY(arena, Shader_Output_Variable, input_count);
    memcpy(output_variables, outputs.data, sizeof(Shader_Output_Variable) * output_count);

    shader->input_count = input_count;
    shader->inputs = input_variables;

    shader->output_count = output_count;
    shader->outputs = output_variables;

    U32 struct_count = structs.count;
    Shader_Struct *shader_structs = HE_ALLOCATE_ARRAY(arena, Shader_Struct, struct_count);

    for (U32 struct_index = 0; struct_index < struct_count; struct_index++)
    {
        const SPIRV_Shader_Struct &spirv_struct = structs[struct_index];
        Shader_Struct *shader_struct = &shader_structs[struct_index];
        shader_struct->name = spirv_struct.name;
        
        U32 member_count = spirv_struct.members.count;
        shader_struct->member_count = member_count;
        shader_struct->members = HE_ALLOCATE_ARRAY(arena, Shader_Struct_Member, member_count);
        copy_memory(shader_struct->members, spirv_struct.members.data, sizeof(Shader_Struct_Member) * member_count);
    }

    shader->struct_count = struct_count;
    shader->structs = shader_structs;
    return true;
}

void destroy_shader(Shader_Handle shader_handle, Vulkan_Context *context)
{
    Vulkan_Shader *vulkan_shader = &context->shaders[shader_handle.index];
    vkDestroyShaderModule(context->logical_device, vulkan_shader->handle, nullptr);
}

static void combine_stage_flags_or_add_binding_if_not_found(Dynamic_Array< Binding > &set, const Binding &binding)
{
    U32 binding_count = set.count;
    for (U32 binding_index = 0; binding_index < binding_count; binding_index++)
    {
        if (set[binding_index].number == binding.number)
        {
            set[binding_index].stage_flags = set[binding_index].stage_flags|binding.stage_flags;
            return;
        }
    }
    append(&set, binding);
}

static VkFormat get_format_from_shader_data_type(Shader_Data_Type data_type)
{
    switch (data_type)
    {
        case Shader_Data_Type::BOOL: return VK_FORMAT_R8_UINT;

        case Shader_Data_Type::S8: return VK_FORMAT_R8_SINT;
        case Shader_Data_Type::S16: return VK_FORMAT_R16_SINT;
        case Shader_Data_Type::S32: return VK_FORMAT_R32_SINT;
        case Shader_Data_Type::S64: return VK_FORMAT_R64_SINT;

        case Shader_Data_Type::U8: return VK_FORMAT_R8_UINT;
        case Shader_Data_Type::U16: return VK_FORMAT_R16_UINT;
        case Shader_Data_Type::U32: return VK_FORMAT_R32_UINT;
        case Shader_Data_Type::U64: return VK_FORMAT_R64_UINT;

        case Shader_Data_Type::F16: return VK_FORMAT_R16_SFLOAT;
        case Shader_Data_Type::F32: return VK_FORMAT_R32_SFLOAT;
        case Shader_Data_Type::F64: return VK_FORMAT_R64_SFLOAT;

        case Shader_Data_Type::VECTOR2F: return VK_FORMAT_R32G32_SFLOAT;
        case Shader_Data_Type::VECTOR3F: return VK_FORMAT_R32G32B32_SFLOAT;
        case Shader_Data_Type::VECTOR4F: return VK_FORMAT_R32G32B32A32_SFLOAT;

        // todo(amer): add support for ShaderDataType_Matrix3 and ShaderDataType_Matrix4
        default:
        {
            HE_ASSERT(!"unsupported type");
        } break;
    }

    return VK_FORMAT_UNDEFINED;
}

static VkDescriptorType get_descriptor_type(Binding_Type type)
{
    switch (type)
    {
        case Binding_Type::UNIFORM_BUFFER: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case Binding_Type::STORAGE_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case Binding_Type::COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

        default:
        {
            HE_ASSERT(!"unsupported binding type");
        } break;
    }

    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
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

bool create_graphics_pipeline(Pipeline_State_Handle pipeline_state_handle,  const Pipeline_State_Descriptor &descriptor, Vulkan_Context *context)
{
    Temprary_Memory_Arena temprary_arena = {};
    begin_temprary_memory_arena(&temprary_arena, &context->arena);
    HE_DEFER { end_temprary_memory_arena(&temprary_arena); };

    Renderer_State *renderer_state = context->renderer_state;
    Pipeline_State *pipeline_state = get(&renderer_state->pipeline_states, pipeline_state_handle);
    pipeline_state->descriptor = descriptor;
    Render_Pass *render_pass = get(&renderer_state->render_passes, descriptor.render_pass);

    Shader_Group *shader_group = get(&renderer_state->shader_groups, descriptor.shader_group);
    
    Vulkan_Shader_Group *vulkan_shader_group = &context->shader_groups[descriptor.shader_group.index];
    Vulkan_Pipeline_State *vulkan_pipeline_state = &context->pipeline_states[pipeline_state_handle.index];

    VkPipelineShaderStageCreateInfo *shader_stage_create_infos = HE_ALLOCATE_ARRAY(&temprary_arena, VkPipelineShaderStageCreateInfo, shader_group->shaders.count);

    bool is_using_vertex_shader = false;
    VkVertexInputBindingDescription *vertex_input_binding_descriptions = nullptr;
    VkVertexInputAttributeDescription *vertex_input_attribute_descriptions = nullptr;

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    for (U32 shader_index = 0; shader_index < shader_group->shaders.count; shader_index++)
    {
        Shader_Handle shader_handle = shader_group->shaders[shader_index];

        Shader *shader = get(&context->renderer_state->shaders, shader_handle);
        Vulkan_Shader *vulkan_shader = &context->shaders[shader_handle.index];

        VkPipelineShaderStageCreateInfo &pipeline_stage_create_info = shader_stage_create_infos[shader_index];
        pipeline_stage_create_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        pipeline_stage_create_info.stage  = get_shader_stage(shader->stage);
        pipeline_stage_create_info.module = vulkan_shader->handle;
        pipeline_stage_create_info.pName  = "main";

        if (shader->stage == Shader_Stage::VERTEX)
        {
            is_using_vertex_shader = true;
            vertex_input_binding_descriptions = HE_ALLOCATE_ARRAY(&temprary_arena, VkVertexInputBindingDescription, shader->input_count);
            vertex_input_attribute_descriptions = HE_ALLOCATE_ARRAY(&temprary_arena, VkVertexInputAttributeDescription, shader->input_count);

            for (U32 input_variable_index = 0; input_variable_index < shader->input_count; input_variable_index++)
            {
                const Shader_Input_Variable *input_variable = &shader->inputs[input_variable_index];
                VkVertexInputBindingDescription *vertex_binding = &vertex_input_binding_descriptions[input_variable_index];
                vertex_binding->binding = input_variable->location;
                vertex_binding->stride = get_size_of_shader_data_type(input_variable->data_type);
                vertex_binding->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                VkVertexInputAttributeDescription *vertex_attribute = &vertex_input_attribute_descriptions[input_variable_index];
                vertex_attribute->binding = input_variable->location;
                vertex_attribute->location = input_variable->location;
                vertex_attribute->format = get_format_from_shader_data_type(input_variable->data_type);
                vertex_attribute->offset = 0;
            }

            vertex_input_state_create_info.vertexBindingDescriptionCount = shader->input_count;
            vertex_input_state_create_info.pVertexBindingDescriptions = vertex_input_binding_descriptions;
            vertex_input_state_create_info.vertexAttributeDescriptionCount = shader->input_count;
            vertex_input_state_create_info.pVertexAttributeDescriptions = vertex_input_attribute_descriptions;
        }

    }

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
    rasterization_state_create_info.polygonMode = get_polygon_mode(descriptor.fill_mode);
    rasterization_state_create_info.lineWidth = 1.0f;
    rasterization_state_create_info.cullMode = get_cull_mode(descriptor.cull_mode);
    rasterization_state_create_info.frontFace = get_front_face(descriptor.front_face);
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
    multisampling_state_create_info.sampleShadingEnable = descriptor.sample_shading ? VK_TRUE : VK_FALSE;
    multisampling_state_create_info.minSampleShading = 0.2f;
    multisampling_state_create_info.pSampleMask = nullptr;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
    color_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment_state.blendEnable = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    color_blend_state_create_info.logicOpEnable = VK_FALSE;
    color_blend_state_create_info.logicOp = VK_LOGIC_OP_COPY;
    color_blend_state_create_info.attachmentCount = 1;
    color_blend_state_create_info.pAttachments = &color_blend_attachment_state;
    color_blend_state_create_info.blendConstants[0] = 0.0f;
    color_blend_state_create_info.blendConstants[1] = 0.0f;
    color_blend_state_create_info.blendConstants[2] = 0.0f;
    color_blend_state_create_info.blendConstants[3] = 0.0f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
    depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
    depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil_state_create_info.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state_create_info.minDepthBounds = 0.0f;
    depth_stencil_state_create_info.maxDepthBounds = 1.0f;
    depth_stencil_state_create_info.stencilTestEnable = VK_FALSE; // note(amer): stencil test is disabled
    depth_stencil_state_create_info.front = {};
    depth_stencil_state_create_info.back = {};

    VkGraphicsPipelineCreateInfo graphics_pipeline_create_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    graphics_pipeline_create_info.stageCount = shader_group->shaders.count;
    graphics_pipeline_create_info.pStages = shader_stage_create_infos;

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
    graphics_pipeline_create_info.layout = vulkan_shader_group->pipeline_layout;
    graphics_pipeline_create_info.renderPass = context->render_passes[ descriptor.render_pass.index ].handle;
    graphics_pipeline_create_info.subpass = 0;
    graphics_pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    graphics_pipeline_create_info.basePipelineIndex = -1;

    HE_CHECK_VKRESULT(vkCreateGraphicsPipelines(context->logical_device, context->pipeline_cache, 1, &graphics_pipeline_create_info, nullptr, &vulkan_pipeline_state->handle));
    return true;
}

void destroy_pipeline(Pipeline_State_Handle pipeline_state_handle, Vulkan_Context *context)
{
    HE_ASSERT(context);
    Vulkan_Pipeline_State *pipeline = &context->pipeline_states[pipeline_state_handle.index];
    vkDestroyPipeline(context->logical_device, pipeline->handle, nullptr);
}