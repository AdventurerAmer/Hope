#pragma once

#include "defines.h"
#include "platform.h"

#include <variant>

#define HE_KILO_BYTES(x) (1024llu * (x))
#define HE_MEGA_BYTES(x) (1024llu * 1024llu * (x))
#define HE_GIGA_BYTES(x) (1024llu * 1024llu * 1024llu * (x))
#define HE_TERA_BYTES(x) (1024llu * 1024llu * 1024llu * 1024llu * (x))

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

void zero_memory(void *memory, U64 size);
void copy_memory(void *dst, const void *src, U64 size);

U64 get_number_of_bytes_to_align_address(U64 address, U16 alignment);

//
// Memory Arena
//

struct Memory_Arena
{
    U8 *base;
    U64 capacity;
    U64 min_allocation_size;
    U64 size;
    U64 offset;
    U32 temp_count;
};

bool init_memory_arena(Memory_Arena *arena, U64 capacity, U64 min_allocation_size = HE_MEGA_BYTES(1));

void* allocate(Memory_Arena *arena, U64 size, U16 alignment);
void* reallocate(Memory_Arena *arena, void *memory, U64 new_size, U16 alignment);
void deallocate(Memory_Arena *arena, void *memory);

//
// Temprary Memory Arena
//

struct Temprary_Memory_Arena
{
    Memory_Arena *arena;
    U64 offset;
};

Temprary_Memory_Arena begin_temprary_memory(Memory_Arena *arena);
void end_temprary_memory(Temprary_Memory_Arena *temprary_arena);

//
// Free List Allocator
//

struct Free_List_Node
{
    // note(amer): if we are going to have no more then 4 giga bytes of memory in the allocator
    // could we use relative pointers or u32 offsets to reduce the node size...
    Free_List_Node *next;
    Free_List_Node *prev;
    U64 size;
};

struct Free_List_Allocator
{
    U8 *base;
    U64 size;
    U64 used;
    Free_List_Node sentinal;
    Mutex mutex;
};

void init_free_list_allocator(Free_List_Allocator *allocator, Memory_Arena *arena, U64 size);
void init_free_list_allocator(Free_List_Allocator *allocator, void *memory, U64 size);

void* allocate(Free_List_Allocator *allocator, U64 size, U16 alignment);
void* reallocate(Free_List_Allocator *allocator, void *memory, U64 new_size, U16 alignment);
void deallocate(Free_List_Allocator *allocator, void *memory);

typedef std::variant< Memory_Arena *, Free_List_Allocator * > Allocator;

bool init_memory_system();
void deinit_memory_system();

Memory_Arena *get_permenent_arena();
Memory_Arena *get_transient_arena();
Memory_Arena *get_debug_arena();
Free_List_Allocator *get_general_purpose_allocator();

Temprary_Memory_Arena get_scratch_arena();