#pragma once

#include "defines.h"

#define HE_KiloBytes(A) (U64(1024) * (A))
#define HE_MegaBytes(A) (U64(1024) * HE_KiloBytes((A)))
#define HE_GigaBytes(A) (U64(1024) * HE_MegaBytes((A)))
#define HE_TeraBytes(A) (U64(1024) * HE_GigaBytes((A)))

#define Allocate(AllocatorPointer, Type)\
(Type *)allocate((AllocatorPointer), sizeof(Type), 0)

#define AllocateAligned(AllocatorPointer, Type)\
(Type *)allocate((AllocatorPointer), sizeof(Type), alignof(Type))

#define AllocateArray(AllocatorPointer, Type, Count)\
(Type *)allocate((AllocatorPointer), sizeof(Type) * (Count), alignof(Type))

void zero_memory(void *memory, Mem_Size size);
void copy_memory(void *dst, void *src, Mem_Size size);

//
// Memory Arena
//

struct Temprary_Memory_Arena;

struct Memory_Arena
{
    U8 *base;
    Mem_Size size;
    Mem_Size offset;

    // todo(amer): this data member is used for debugging purposes only and
    // should be used accessed non-shipping builds only
    Temprary_Memory_Arena *current_temprary_owner;
};

Memory_Arena
create_memory_arena(void *memory, Mem_Size size);

void*
allocate(Memory_Arena *arena,
         Mem_Size size, U16 alignment,
         Temprary_Memory_Arena *parent = nullptr);

//
// Temprary Memory Arena
//

struct Temprary_Memory_Arena
{
    Memory_Arena *arena;
    Mem_Size offset;

    // todo(amer): this data member is used for debugging purposes only and
    // should be used accessed non-shipping builds only
    Temprary_Memory_Arena *parent;
};

void begin_temprary_memory_arena(Temprary_Memory_Arena *temprary_memory_arena,
                                 Memory_Arena *arena);

// todo(amer): force inline
inline internal_function void*
allocate(Temprary_Memory_Arena *temprary_arena, Mem_Size size, U16 alignment)
{
    return allocate(temprary_arena->arena, size, alignment, temprary_arena);
}

void
end_temprary_memory_arena(Temprary_Memory_Arena *temprary_arena);

//
// Scoped Temprary Memory Arena
//

struct Scoped_Temprary_Memory_Arena
{
    Temprary_Memory_Arena temprary_arena;

    Scoped_Temprary_Memory_Arena(Memory_Arena *arena);
    ~Scoped_Temprary_Memory_Arena();
};

// todo(amer): force inline
inline internal_function void*
allocate(Scoped_Temprary_Memory_Arena *scoped_temprary_memory_arena, Mem_Size size, U16 alignment)
{
    return allocate(scoped_temprary_memory_arena->temprary_arena.arena,
                    size,
                    alignment,
                    &scoped_temprary_memory_arena->temprary_arena);
}

//
// Free List Allocator
//

struct Free_List_Node
{
    // note(amer): if we are going to have no more then 4 giga bytes of memory in the allocator
    // could we use relative pointers to reduce the node size
    Free_List_Node *next;
    Free_List_Node *prev;
    Mem_Size size;
};

struct Free_List_Allocator
{
    U8 *base;
    Mem_Size size;
    Mem_Size used;
    Free_List_Node sentinal;
};

void
init_free_list_allocator(Free_List_Allocator *free_list_allocator,
                         Memory_Arena *arena,
                         Mem_Size size);

void *
allocate(Free_List_Allocator *allocator,
         Mem_Size size,
         U16 alignment);

void deallocate(Free_List_Allocator *allocator, void *memory);