#pragma once

#include "defines.h"
#include "platform.h"

#include <variant>

#define HE_KILO(x) (1024llu * (x))
#define HE_MEGA(x) (1024llu * 1024llu * (x))
#define HE_GIGA(x) (1024llu * 1024llu * 1024llu * (x))
#define HE_TERA(x) (1024llu * 1024llu * 1024llu * 1024llu * (x))

#define HE_ALLOCATE(allocator_pointer, type)\
(type *)allocate((allocator_pointer), sizeof(type), 1)

#define HE_ALLOCATE_ALIGNED(allocator_pointer, type)\
(type *)allocate((allocator_pointer), sizeof(type), alignof(type))

#define HE_ALLOCATE_ARRAY(allocator_pointer, type, count)\
(type *)allocate((allocator_pointer), sizeof(type) * (count), alignof(type))

#define HE_REALLOCATE(allocator_pointer, memory, type)\
(type *)reallocate((allocator_pointer), (memory), sizeof(type), 1)

#define HE_REALLOCATE_ALIGNED(allocator_pointer, type)\
(type *)reallocate((allocator_pointer), sizeof(type), alignof(type))

#define HE_REALLOCATE_ARRAY(allocator_pointer, memory, type, count)\
(type *)reallocate((allocator_pointer), memory, sizeof(type) * (count), alignof(type))

void zero_memory(void *memory, Size size);
void copy_memory(void *dst, const void *src, Size size);

//
// Memory Arena
//

struct Temprary_Memory_Arena;

struct Memory_Arena
{
    U8 *base;
    Size size;
    Size offset;

    Size last_padding;
    Size last_offset;

#ifndef HE_SHIPPING
    Temprary_Memory_Arena *parent;
#endif
};

Memory_Arena create_memory_arena(void *memory, Size size);
Memory_Arena create_sub_arena(Memory_Arena *arena, Size size);

void* allocate(Memory_Arena *arena, Size size, U16 alignment, Temprary_Memory_Arena *parent = nullptr);
void* reallocate(Memory_Arena *arena, void *memory, Size new_size, U16 alignment, Temprary_Memory_Arena *parent = nullptr);
void deallocate(Memory_Arena *arena, void *memory);

//
// Temprary Memory Arena
//

struct Temprary_Memory_Arena
{
    Memory_Arena *arena;
    Size offset;

#ifndef HE_SHIPPING
    Temprary_Memory_Arena *parent;
#endif
};

void begin_temprary_memory_arena(Temprary_Memory_Arena *temprary_memory_arena, Memory_Arena *arena);

HE_FORCE_INLINE static void* allocate(Temprary_Memory_Arena *temprary_arena, Size size, U16 alignment)
{
    return allocate(temprary_arena->arena, size, alignment, temprary_arena);
}

HE_FORCE_INLINE static void* reallocate(Temprary_Memory_Arena *temprary_arena, void *memory, Size new_size, U16 alignment)
{
    return reallocate(temprary_arena->arena, memory, new_size, alignment, temprary_arena);
}

HE_FORCE_INLINE static void deallocate(Temprary_Memory_Arena *temprary_arena, void *memory)
{
    deallocate(temprary_arena->arena, memory);
}

void end_temprary_memory_arena(Temprary_Memory_Arena *temprary_arena);

//
// Free List Allocator
//

struct Free_List_Node
{
    // note(amer): if we are going to have no more then 4 giga bytes of memory in the allocator
    // could we use relative pointers or u32 offsets to reduce the node size...
    Free_List_Node *next;
    Free_List_Node *prev;
    Size size;
};

struct Free_List_Allocator
{
    U8 *base;
    Size size;
    Size used;
    Free_List_Node sentinal;
    Mutex mutex;
};

void init_free_list_allocator(Free_List_Allocator *allocator, Memory_Arena *arena, Size size);
void init_free_list_allocator(Free_List_Allocator *allocator, void *memory, Size size);

void* allocate(Free_List_Allocator *allocator, Size size, U16 alignment);
void* reallocate(Free_List_Allocator *allocator, void *memory, U64 new_size, U16 alignment);
void deallocate(Free_List_Allocator *allocator, void *memory);

typedef std::variant< Memory_Arena *, Temprary_Memory_Arena *, Free_List_Allocator * > Allocator;