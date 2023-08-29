#include "cvars.h"
#include "platform.h"
#include "memory.h"
#include "containers/string.h"
#include "engine.h"

#include <unordered_map>
#include <vector>

union CVar_Data
{
    S64 s64;
    F64 f64;
    String string;
};

struct CVar
{
    String name;
    String description;

    CVar_Flags flags;
    CVar_Type type;
    CVar_Data value;
};

struct CVars_Category
{
    const char *name;
    std::vector< CVar > cvars;
};

struct CVars_State
{
    const char *filepath;
    std::unordered_map< U64, CVars_Category > category_to_cvars;
};

static CVars_State cvars_state;

bool init_cvars(const char *filepath, Engine *engine)
{
    cvars_state.filepath = filepath;

    Temprary_Memory_Arena temprary_arena = {};
    begin_temprary_memory_arena(&temprary_arena, &engine->memory.transient_arena);

    Read_Entire_File_Result result = read_entire_file(filepath, &temprary_arena);

    std::vector< CVar > *cvars = nullptr;

    if (result.success)
    {
        String str = { (char *)result.data, result.size };
        while (true)
        {
            S64 new_line_index = find_first_char_from_left(&str, "\n");
            if (new_line_index == -1 || str.count == 0)
            {
                break;
            }
            String line = sub_string(&str, 0, new_line_index);

            if (line.data[0] == '@')
            {
                String category = sub_string(&line, 1);
                auto it = cvars_state.category_to_cvars.find(hash(&category));
                HOPE_Assert(it != cvars_state.category_to_cvars.end());
                cvars = &it->second.cvars;
            }
            else if (line.data[0] == ':')
            {
                String cvar_name_value_pair = sub_string(&line, 1);

                S64 space = find_first_char_from_left(&cvar_name_value_pair, " ");
                HOPE_Assert(space != -1);

                String cvar_name = sub_string(&cvar_name_value_pair, 0, space);
                String cvar_value = sub_string(&cvar_name_value_pair, space + 1);

                for (CVar &cvar : *cvars)
                {
                    if (cvar_name == cvar.name)
                    {
                        switch (cvar.type)
                        {
                            case CVarType_Int:
                            {
                                String temp = copy_string(cvar_value.data,
                                                          cvar_value.count,
                                                          &temprary_arena);
                                cvar.value.s64 = atoll(temp.data);
                            } break;

                            case CVarType_Float:
                            {
                                String temp = copy_string(cvar_value.data,
                                                          cvar_value.count,
                                                          &temprary_arena);
                                cvar.value.f64 = atof(temp.data);
                            } break;

                            case CVarType_String:
                            {
                                cvar.value.string = copy_string(cvar_value.data,
                                                                cvar_value.count,
                                                                &engine->memory.free_list_allocator);
                            } break;
                        }
                    }
                }
            }

            str.data += new_line_index + 1;
            str.count -= new_line_index + 1;
        }
    }

    end_temprary_memory_arena(&temprary_arena);
    return false;
}

void deinit_cvars(Engine *engine)
{
    Temprary_Memory_Arena temprary_arena = {};
    begin_temprary_memory_arena(&temprary_arena, &engine->memory.transient_arena);

    String_Builder string_builder = {};
    begin_string_builder(&string_builder, temprary_arena.arena);

    for (const auto &[hash, category] : cvars_state.category_to_cvars)
    {
        push(&string_builder, "@%s\n", category.name);

        for (const auto &cvar : category.cvars)
        {
            switch (cvar.type)
            {
                case CVarType_Int:
                {
                    push(&string_builder, ":%s %lld\n", cvar.name, cvar.value.s64);
                } break;

                case CVarType_Float:
                {
                    push(&string_builder, ":%s %f\n", cvar.name, cvar.value.f64);
                } break;

                case CVarType_String:
                {
                    push(&string_builder, ":%s %.*s\n", cvar.name, HOPE_ExpandString(&cvar.value.string));
                } break;
            }
        }
    }

    String str = end_string_builder(&string_builder);
    bool success = write_entire_file(cvars_state.filepath, (void *)str.data, str.count);
    HOPE_Assert(success);
    end_temprary_memory_arena(&temprary_arena);
}

static void* declare_cvar(const char *name,
                          const char *description,
                          CVar_Type type,
                          CVar_Data default_value,
                          U64 category_hash,
                          const char *category,
                          CVar_Flags flags)
{
    CVars_Category &cvars_category = cvars_state.category_to_cvars[category_hash];
    cvars_category.name = category;

    std::vector< CVar > &cvars = cvars_category.cvars;
    cvars.push_back(CVar {});
    CVar &cvar = cvars.back();

    cvar.name = HOPE_String(name);
    cvar.description = HOPE_String(description);
    cvar.type = type;
    cvar.flags = flags;
    cvar.value = default_value;

    switch (type)
    {
        case CVarType_Int:
        {
            return &cvar.value.s64;
        } break;

        case CVarType_Float:
        {
            return &cvar.value.f64;
        } break;

        case CVarType_String:
        {
            return &cvar.value.string;
        } break;
    }

    return nullptr;
}


void* declare_cvar(const char *name,
                   const char *description,
                   S64 default_value,
                   U64 category_hash,
                   const char *category,
                   CVar_Flags flags)
{
    CVar_Data data = {};
    data.s64 = default_value;
    return declare_cvar(name, description, CVarType_Int, data, category_hash, category, flags);
}

void* declare_cvar(const char *name,
                   const char *description,
                   F64 default_value,
                   U64 category_hash,
                   const char *category,
                   CVar_Flags flags)
{
    CVar_Data data = {};
    data.f64 = default_value;
    return declare_cvar(name, description, CVarType_Float, data, category_hash, category, flags);
}

void* declare_cvar(const char *name,
                   const char *description,
                   const char *default_value,
                   U64 category_hash,
                   const char *category,
                   CVar_Flags flags)
{
    CVar_Data data = {};
    data.string = { default_value, string_length(default_value) };
    return declare_cvar(name, description, CVarType_String, data, category_hash, category, flags);
}

void *get_cvar(const char *name, U64 category, CVar_Type type)
{
    auto it = cvars_state.category_to_cvars.find(category);
    HOPE_Assert(it != cvars_state.category_to_cvars.end());
    std::vector< CVar > &cvars = it->second.cvars;
    for (CVar &cvar : cvars)
    {
        if (cvar.name == name)
        {
            HOPE_Assert(type == cvar.type);
            switch (type)
            {
                case CVarType_Int:
                {
                    return &cvar.value.s64;
                } break;

                case CVarType_Float:
                {
                    return &cvar.value.f64;
                } break;

                case CVarType_String:
                {
                    return &cvar.value.string;
                } break;
            }
        }
    }

    return nullptr;
}
