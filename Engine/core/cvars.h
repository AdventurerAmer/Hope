#pragma once

#include "containers/string.h"

enum class CVar_Type
{
    NONE,

    BOOL,

    U8,
    U16,
    U32,
    U64,

    S8,
    S16,
    S32,
    S64,

    F32,
    F64,

    STRING
};

// todo(amer): cvar flags and imgui
enum CVar_Flags
{
    CVarFlag_None = 0 << 0,
};

bool init_cvars(const char *filepath);
void deinit_cvars();

void declare_cvar(const char *category, const char *name, bool *memory, CVar_Flags flags);

void declare_cvar(const char *category, const char *name, U8 *memory, CVar_Flags flags);
void declare_cvar(const char *category, const char *name, U16 *memory, CVar_Flags flags);
void declare_cvar(const char *category, const char *name, U32 *memory, CVar_Flags flags);
void declare_cvar(const char *category, const char *name, U64 *memory, CVar_Flags flags);

void declare_cvar(const char *category, const char *name, S8 *memory, CVar_Flags flags);
void declare_cvar(const char *category, const char *name, S16 *memory, CVar_Flags flags);
void declare_cvar(const char *category, const char *name, S32 *memory, CVar_Flags flags);
void declare_cvar(const char *category, const char *name, S64 *memory, CVar_Flags flags);

void declare_cvar(const char *category, const char *name, F32 *memory, CVar_Flags flags);
void declare_cvar(const char *category, const char *name, F64 *memory, CVar_Flags flags);
void declare_cvar(const char *category, const char *name, String *memory, CVar_Flags flags);

void *get_cvar(const char *category, const char *name);

#define HE_DECLARE_CVAR(category, name, flags) declare_cvar(category, HE_STRINGIFY(name), &name, flags)