#pragma once

#include "containers/string.h"

enum CVar_Type
{
    CVarType_Int,
    CVarType_Float,
    CVarType_String
};

enum CVar_Flags
{
    CVarFlag_None      = 0 << 0,
    CVarFlag_NoEdit    = 1 << 0,
    CVarFlag_CheckBox  = 1 << 1,
};

bool init_cvars(const char *filepath, struct Engine *engine);
void deinit_cvars(struct Engine *engine);

void* declare_cvar(const char *name,
                   const char *description,
                   S64 default_value,
                   U64 category_hash,
                   const char *category,
                   CVar_Flags flags);

void* declare_cvar(const char *name,
                   const char *description,
                   F64 default_value,
                   U64 category_hash,
                   const char *category,
                   CVar_Flags flags);

void* declare_cvar(const char *name,
                   const char *description,
                   const char *default_value,
                   U64 category_hash,
                   const char *category,
                   CVar_Flags flags);

void *get_cvar(const char *name, U64 category, CVar_Type type);

#define HOPE_CVarInt(Name, Description, DefaultValue, Category, Flags)\
    S64 *config_##Name = (S64 *)declare_cvar(HOPE_Stringify(Name), Description, (S64)DefaultValue, compile_time_string_hash(Category), Category, Flags)

#define HOPE_CVarFloat(Name, Description, DefaultValue, Category, Flags)\
    F64 *config_##Name = (F64 *)declare_cvar(HOPE_Stringify(Name), Description, (F64)DefaultValue, compile_time_string_hash(Category), Category, Flags)

#define HOPE_CVarString(Name, Description, DefaultValue, Category, Flags)\
    String *config_##Name = (String *)declare_cvar(HOPE_Stringify(Name), Description, DefaultValue, compile_time_string_hash(Category), Category, Flags)

#define HOPE_CVarGetInt(Name, Category)\
    S64 *Name = (S64 *)get_cvar(HOPE_Stringify(Name), compile_time_string_hash(Category), CVarType_Int)

#define HOPE_CVarGetFloat(Name, Category)\
    F64 *Name = (F64 *)get_cvar(HOPE_Stringify(Name), compile_time_string_hash(Category), CVarType_Float)

#define HOPE_CVarGetString(Name, Category)\
    String *Name = (String *)get_cvar(HOPE_Stringify(Name), compile_time_string_hash(Category), CVarType_String)