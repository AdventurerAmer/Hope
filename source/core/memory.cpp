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

// Memory Arena

Memory_Arena create_memory_arena(void *memory, Mem_Size size)
{
    HE_Assert(memory);
    HE_Assert(size);

    Memory_Arena result = {};
    result.base = (U8 *)memory;
    result.size = size;

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

void* allocate(Memory_Arena *arena, Mem_Size size, U16 alignment, Temprary_Memory_Arena *parent)
{
    HE_Assert(arena);
    HE_Assert(size);
    HE_Assert(arena->temprary_owner == parent);

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

// Temprary Memory Arena

void begin_temprary_memory_arena(Temprary_Memory_Arena *temprary_memory_arena,
                                 Memory_Arena *arena)
{
    HE_Assert(temprary_memory_arena);
    HE_Assert(arena);

    temprary_memory_arena->arena = arena;
    temprary_memory_arena->offset = arena->offset;

    // todo(amer): this data member is used for debugging purposes only and
    // should be used accessed non-shipping builds only
    temprary_memory_arena->parent = arena->temprary_owner;
    arena->temprary_owner = temprary_memory_arena;
}

void
end_temprary_memory_arena(Temprary_Memory_Arena *temprary_arena)
{
    Memory_Arena *arena = temprary_arena->arena;
    HE_Assert(arena);

    arena->offset = temprary_arena->offset;
    arena->temprary_owner = temprary_arena->parent;

    // todo(amer): this data member is used for debugging purposes only and
    // should be used accessed non-shipping builds only
    temprary_arena->arena = nullptr;
    temprary_arena->offset = 0;
}

// Scoped Temprary Memory Arena

Scoped_Temprary_Memory_Arena::Scoped_Temprary_Memory_Arena(Memory_Arena *arena)
{
    begin_temprary_memory_arena(&temprary_arena, arena);
}

Scoped_Temprary_Memory_Arena::~Scoped_Temprary_Memory_Arena()
{
    end_temprary_memory_arena(&temprary_arena);
}