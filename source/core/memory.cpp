#include <string.h>

#include "memory.h"

void zero_memory(void *memory, Mem_Size size)
{
    HE_Assert(memory);
    memset(memory, 0, size);
}

void copy_memory(void *dst, void *src, Mem_Size size)
{
    HE_Assert(dst);
    HE_Assert(src);
    HE_Assert(size);
    memcpy(dst, src, size);
}

Memory_Arena create_memory_arena(void *memory, Mem_Size size)
{
    HE_Assert(memory);
    HE_Assert(size);

    Memory_Arena result;
    result.base = (U8 *)memory;
    result.size = size;
    result.offset = 0;
    result.is_used_by_a_temprary_memory_arena = false;

    return result;
}

bool is_power_of_2(U16 value)
{
    return (value & (value - 1)) == 0;
}

Mem_Size get_number_of_bytes_to_align_address(Mem_Size address, U16 alignment)
{
    // todo(amer): branchless version daddy
    Mem_Size result = 0;

    if (alignment != 0)
    {
        HE_Assert(is_power_of_2(alignment));
        Mem_Size modulo = address & (alignment - 1);

        if (modulo != 0)
        {
            result = alignment - modulo;
        }
    }
    return result;
}

void* allocate(Memory_Arena *arena, Mem_Size size, U16 alignment, bool is_temprary)
{
    HE_Assert(arena);
    HE_Assert(size);
    HE_Assert(arena->is_used_by_a_temprary_memory_arena == is_temprary);

    void *result = 0;
    U8 *cursor = arena->base + arena->offset;
    Mem_Size padding = get_number_of_bytes_to_align_address((Mem_Size)cursor, alignment);
    if (arena->offset + size + padding <= arena->size)
    {
        result = cursor + padding;
        arena->offset += padding + size;
    }
    zero_memory(result, padding + size);
    return result;
}



Temprary_Memory_Arena begin_temprary_memory_arena(Memory_Arena *arena)
{
    HE_Assert(arena);
    HE_Assert(arena->is_used_by_a_temprary_memory_arena == false);

    // todo(amer): this should used in non-shipping builds only
    arena->is_used_by_a_temprary_memory_arena = true;

    Temprary_Memory_Arena result;
    result.arena = arena;
    result.offset = arena->offset;

    return result;
}

void
end_temprary_memory_arena(Temprary_Memory_Arena *temprary_arena)
{
    Memory_Arena *arena = temprary_arena->arena;
    HE_Assert(arena);

    arena->offset = temprary_arena->offset;
    arena->is_used_by_a_temprary_memory_arena = false;

    // todo(amer): do this in non-shipping builds only
    temprary_arena->arena = nullptr;
    temprary_arena->offset = 0;
}



Scoped_Temprary_Memory_Arena::Scoped_Temprary_Memory_Arena(Memory_Arena *arena)
{
    temprary_arena = begin_temprary_memory_arena(arena);
}

Scoped_Temprary_Memory_Arena::~Scoped_Temprary_Memory_Arena()
{
    end_temprary_memory_arena(&temprary_arena);
}