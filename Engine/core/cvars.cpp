#include "cvars.h"
#include "platform.h"
#include "memory.h"
#include "file_system.h"
#include "containers/string.h"
#include "containers/dynamic_array.h"

#include <stdlib.h>

struct CVar
{
    String name;
    CVar_Type type;

    bool is_declared;
    String value;
    void *memory;

    CVar_Flags flags;
};

struct CVar_Category
{
    String name;
    Dynamic_Array< CVar > vars;
};

struct CVars_State
{
    String filepath;
    Dynamic_Array< CVar_Category > categories;
};

static CVars_State cvars_state;

static CVar_Category* find_or_append_category(const String &name, bool should_append = true)
{
    Memory_Context memory_context = grab_memory_context();

    auto &categories = cvars_state.categories;

    for (U32 category_index = 0; category_index < categories.count; category_index++)
    {
        if (categories[category_index].name == name)
        {
            return &categories[category_index];
        }
    }

    if (should_append)
    {
        CVar_Category category = {};
        category.name = copy_string(name, memory_context.permenent_allocator);
        init(&category.vars);

        append(&categories, category);
        return &back(&categories);
    }

    return nullptr;
}

static CVar* find_or_append_cvar(CVar_Category *category, const String &name, bool should_append = true)
{
    Memory_Context memory_context = grab_memory_context();

    auto &vars = category->vars;

    for (U32 var_index = 0; var_index < vars.count; var_index++)
    {
        if (vars[var_index].name == name)
        {
            return &vars[var_index];
        }
    }

    if (should_append)
    {
        CVar var = {};
        var.name = copy_string(name, memory_context.permenent_allocator);
        append(&vars, var);
        return &back(&vars);
    }

    return nullptr;
}

bool init_cvars(String filepath)
{
    Memory_Context memory_context = grab_memory_context();

    cvars_state.filepath = filepath;

    auto &categories = cvars_state.categories;
    init(&categories);
    
    Read_Entire_File_Result result = read_entire_file(filepath, memory_context.temp_allocator);
    CVar_Category *category = nullptr;

    if (result.success)
    {
        String str = { result.size, (char *)result.data };

        while (true)
        {
            S64 new_line_index = find_first_char_from_left(str, HE_STRING_LITERAL("\n"));
            if (new_line_index == -1 || str.count == 0)
            {
                break;
            }
            String line = sub_string(str, 0, new_line_index);

            if (line.data[0] == '@')
            {
                String category_name = sub_string(line, 1);
                category = find_or_append_category(category_name);
            }
            else if (line.data[0] == ':')
            {
                String cvar_name_value_pair = sub_string(line, 1);

                S64 space = find_first_char_from_left(cvar_name_value_pair, HE_STRING_LITERAL(" "));
                HE_ASSERT(space != -1);

                String name = sub_string(cvar_name_value_pair, 0, space);
                String value = sub_string(cvar_name_value_pair, space + 1);

                CVar *var = find_or_append_cvar(category, name);
                var->value = copy_string(value, memory_context.permenent_allocator);
            }

            str.data += new_line_index + 1;
            str.count -= new_line_index + 1;
        }

        return true;
    }

    return false;
}

void deinit_cvars()
{
    Memory_Context memory_context = grab_memory_context();

    auto &categories = cvars_state.categories;
    
    String_Builder string_builder = {};
    begin_string_builder(&string_builder, memory_context.temprary_memory.arena);

    for (U32 category_index = 0; category_index < categories.count; category_index++)
    {
        CVar_Category &category = categories[category_index];

        append(&string_builder, "@%.*s\n", HE_EXPAND_STRING(category.name));

        for (U32 var_index = 0; var_index < category.vars.count; var_index++)
        {
            CVar &var = category.vars[var_index];
            switch (var.type)
            {
                case CVar_Type::S8:
                {
                    append(&string_builder, ":%.*s %d\n", HE_EXPAND_STRING(var.name), *(S8 *)var.memory);
                } break;

                case CVar_Type::S16:
                {
                    append(&string_builder, ":%.*s %d\n", HE_EXPAND_STRING(var.name), *(S16 *)var.memory);
                } break;

                case CVar_Type::S32:
                {
                    append(&string_builder, ":%.*s %d\n", HE_EXPAND_STRING(var.name), *(S32 *)var.memory);
                } break;

                case CVar_Type::S64:
                {
                    append(&string_builder, ":%.*s %lld\n", HE_EXPAND_STRING(var.name), *(S64 *)var.memory);
                } break;

                case CVar_Type::BOOL:
                case CVar_Type::U8:
                {
                    append(&string_builder, ":%.*s %u\n", HE_EXPAND_STRING(var.name), *(U8 *)var.memory);
                } break;

                case CVar_Type::U16:
                {
                    append(&string_builder, ":%.*s %u\n", HE_EXPAND_STRING(var.name), *(U16 *)var.memory);
                } break;

                case CVar_Type::U32:
                {
                    append(&string_builder, ":%.*s %u\n", HE_EXPAND_STRING(var.name), *(U32 *)var.memory);
                } break;

                case CVar_Type::U64:
                {
                    append(&string_builder, ":%.*s %llu\n", HE_EXPAND_STRING(var.name), *(U64 *)var.memory);
                } break;

                case CVar_Type::F32:
                {
                    append(&string_builder, ":%.*s %f\n", HE_EXPAND_STRING(var.name), *(F32 *)var.memory);
                } break;

                case CVar_Type::F64:
                {
                    append(&string_builder, ":%.*s %f\n", HE_EXPAND_STRING(var.name), *(F64 *)var.memory);
                } break;

                case CVar_Type::STRING:
                {
                    append(&string_builder, ":%.*s %.*s\n", HE_EXPAND_STRING(var.name), HE_EXPAND_STRING(*(String *)var.memory));
                } break;
            }
        }
    }

    String str = end_string_builder(&string_builder);
    bool success = write_entire_file(cvars_state.filepath, (void *)str.data, str.count);
    HE_ASSERT(success);
}

static void declare_cvar(const String &category_name, const String &cvar_name, void *memory, CVar_Type type, CVar_Flags flags)
{
    CVar_Category *category = find_or_append_category(category_name);
    CVar *var = find_or_append_cvar(category, cvar_name);
    var->type = type;
    var->flags = flags;
    var->memory = memory;
    var->is_declared = true;

    bool is_var_in_config_file = var->value.count != 0;
    if (is_var_in_config_file)
    {
        switch (type)
        {
            case CVar_Type::S8:
            {
                S32 value = atoi(var->value.data);
                HE_ASSERT(value >= HE_MIN_S8 && value <= HE_MAX_S8);
                *(S8 *)var->memory = (S8)value;
            } break;

            case CVar_Type::S16:
            {
                S32 value = atoi(var->value.data);
                HE_ASSERT(value >= HE_MIN_S16 && value <= HE_MAX_S16);
                *(S16 *)var->memory = (S16)value;
            } break;

            case CVar_Type::S32:
            {
                *(S32 *)var->memory = (S32)atoi(var->value.data);
            } break;

            case CVar_Type::S64:
            {
                *(S64 *)var->memory = (S64)atoll(var->value.data);
            } break;

            case CVar_Type::BOOL:
            case CVar_Type::U8:
            {
                S32 value = atoi(var->value.data);
                HE_ASSERT(value >= 0 && value <= HE_MAX_U8);
                *(U8 *)var->memory = (U8)value;
            } break;

            case CVar_Type::U16:
            {
                S32 value = atoi(var->value.data);
                HE_ASSERT(value >= 0 && value <= HE_MAX_U16);
                *(U16 *)var->memory = (U16)value;
            } break;

            case CVar_Type::U32:
            {
                S64 value = atoll(var->value.data);
                HE_ASSERT(value >= 0 && value <= HE_MAX_U32);
                *(U32 *)var->memory = (U32)atoll(var->value.data);
            } break;

            case CVar_Type::U64:
            {
                S64 value = atoll(var->value.data);
                HE_ASSERT(value >= 0);
                *(U64*)var->memory = (U64)value;
            } break;

            case CVar_Type::F32:
            {
                *(F32 *)var->memory = (F32)atof(var->value.data);
            } break;

            case CVar_Type::F64:
            {
                *(F64 *)var->memory = atof(var->value.data);
            } break;

            case CVar_Type::STRING:
            {
                *(String *)var->memory = var->value;
            } break;
        }
    }
}

void declare_cvar(const char *category, const char *name, bool *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::BOOL, flags);
}

void declare_cvar(const char *category, const char *name, U8 *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::U8, flags);
}

void declare_cvar(const char *category, const char *name, U16 *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::U16, flags);
}

void declare_cvar(const char *category, const char *name, U32 *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::U32, flags);
}

void declare_cvar(const char *category, const char *name, U64 *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::U64, flags);
}

void declare_cvar(const char *category, const char *name, S8 *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::S8, flags);
}

void declare_cvar(const char *category, const char *name, S16 *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::S16, flags);
}

void declare_cvar(const char *category, const char *name, S32 *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::S32, flags);
}

void declare_cvar(const char *category, const char *name, S64 *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::S64, flags);
}

void declare_cvar(const char *category, const char *name, F32 *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::F32, flags);
}

void declare_cvar(const char *category, const char *name, F64 *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::F64, flags);
}

void declare_cvar(const char *category, const char *name, String *memory, CVar_Flags flags)
{
    declare_cvar(HE_STRING(category), HE_STRING(name), memory, CVar_Type::STRING, flags);
}

void* get_cvar(const String &category_name, const String &cvar_name)
{
    bool should_append = false;

    CVar_Category *category = find_or_append_category(category_name, should_append);
    if (!category)
    {
        return nullptr;
    }

    CVar *var = find_or_append_cvar(category, cvar_name, should_append);
    if (!var || !var->is_declared)
    {
        return nullptr;
    }

    return var->memory;
}