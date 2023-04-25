#pragma once

#include "defines.h"

#define HE_KiloBytes(A) (U64(1024) * (A))
#define HE_MegaBytes(A) (U64(1024) * HE_KiloBytes((A)))
#define HE_GigaBytes(A) (U64(1024) * HE_MegaBytes((A)))
#define HE_TeraBytes(A) (U64(1024) * HE_GigaBytes((A)))

internal_function void
zero_memory(void *memory, Mem_Size size);

internal_function void
copy_memory(void *dst, void *src, Mem_Size size);




struct Memory_Arena
{
    U8 *base;
    Mem_Size size;
    Mem_Size offset;

    // todo(amer): this should used in non-shipping builds only
    bool is_used_by_a_temprary_memory_arena;
};

Memory_Arena
create_memory_arena(void *memory, Mem_Size size);

void*
allocate(Memory_Arena *arena, Mem_Size size, U16 alignment, bool is_temprary = false);

struct Temprary_Memory_Arena
{
    Memory_Arena *arena;
    Mem_Size offset;
};

Temprary_Memory_Arena
begin_temprary_memory_arena(Memory_Arena *arena);

inline internal_function void*
allocate(Temprary_Memory_Arena *temprary_arena, Mem_Size size, U16 alignment)
{
    return allocate(temprary_arena->arena, size, alignment, true);
}

void
end_temprary_memory_arena(Temprary_Memory_Arena *temprary_arena);



struct Scoped_Temprary_Memory_Arena
{
    Temprary_Memory_Arena temprary_arena;

    Scoped_Temprary_Memory_Arena(Memory_Arena *arena);
    ~Scoped_Temprary_Memory_Arena();
};

inline internal_function void*
allocate(Scoped_Temprary_Memory_Arena *temprary_arena, Mem_Size size, U16 alignment)
{
    return allocate(temprary_arena->temprary_arena.arena, size, alignment, true);
}

#define ArenaPush(ArenaPointer, Type)\
(Type *)allocate((ArenaPointer), sizeof(Type), 0)

#define ArenaPushAligned(ArenaPointer, Type)\
(Type *)allocate((ArenaPointer), sizeof(Type), alignof(Type))

#define ArenaPushArray(ArenaPointer, Type, Count)\
(Type *)allocate((ArenaPointer), sizeof(Type) * (Count), alignof(Type))