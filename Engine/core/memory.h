#pragma once

#include "defines.h"
#include "platform.h"

#define HE_KILO_BYTES(x) (1024llu * (x))
#define HE_MEGA_BYTES(x) (1024llu * 1024llu * (x))
#define HE_GIGA_BYTES(x) (1024llu * 1024llu * 1024llu * (x))
#define HE_TERA_BYTES(x) (1024llu * 1024llu * 1024llu * 1024llu * (x))

#define HE_DEFAULT_ALIGNMENT 16

#define HE_ALLOCATE(allocator_pointer, type)\
(type *)allocate((allocator_pointer), sizeof(type), HE_DEFAULT_ALIGNMENT)

#define HE_ALLOCATE_ALIGNED(allocator_pointer, type)\
(type *)allocate((allocator_pointer), sizeof(type), alignof(type))

#define HE_ALLOCATE_ARRAY(allocator_pointer, type, count)\
(type *)allocate((allocator_pointer), sizeof(type) * (count), alignof(type))

#define HE_REALLOCATE(allocator_pointer, memory, type)\
(type *)reallocate((allocator_pointer), (memory), sizeof(type), HE_DEFAULT_ALIGNMENT)

#define HE_REALLOCATE_ALIGNED(allocator_pointer, type)\
(type *)reallocate((allocator_pointer), sizeof(type), alignof(type))

#define HE_REALLOCATE_ARRAY(allocator_pointer, memory, type, count)\
(type *)reallocate((allocator_pointer), memory, sizeof(type) * (count), alignof(type))

void zero_memory(void *memory, U64 size);
void copy_memory(void *dst, const void *src, U64 size);

U64 get_number_of_bytes_to_align_address(uintptr_t address, U16 alignment);

struct Allocator
{
    void *data;
    void* (*allocate)(void *data, U64 size, U16 alignment);
    void* (*reallocate)(void *data, void *memory, U64 new_size, U16 alignment);
    void (*deallocate)(void *data, void *memory);
};

#define HE_ALLOCATOR_ALLOCATE(allocator_pointer, type)\
(type *)(allocator_pointer)->allocate((allocator_pointer)->data, sizeof(type), HE_DEFAULT_ALIGNMENT)

#define HE_ALLOCATOR_ALLOCATE_ALIGNED(allocator_pointer, type)\
(type *)(allocator_pointer)->allocate((allocator_pointer)->data, sizeof(type), alignof(type))

#define HE_ALLOCATOR_ALLOCATE_ARRAY(allocator_pointer, type, count)\
(type *)(allocator_pointer)->allocate((allocator_pointer)->data, sizeof(type) * (count), alignof(type))

#define HE_ALLOCATOR_REALLOCATE(allocator_pointer, memory, type)\
(type *)(allocator_pointer)->reallocate((allocator_pointer)->data, (memory), sizeof(type), HE_DEFAULT_ALIGNMENT)

#define HE_ALLOCATOR_REALLOCATE_ALIGNED(allocator_pointer, type)\
(type *)(allocator_pointer)->reallocate((allocator_pointer)->data, sizeof(type), alignof(type))

#define HE_ALLOCATOR_REALLOCATE_ARRAY(allocator_pointer, memory, type, count)\
(type *)(allocator_pointer)->reallocate((allocator_pointer)->data, memory, sizeof(type) * (count), alignof(type))

#define HE_ALLOCATOR_DEALLOCATE(allocator_pointer, memory)\
(allocator_pointer)->deallocate((allocator_pointer)->data, memory)

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

bool init_memory_arena(Memory_Arena *memory_arena, U64 capacity, U64 min_allocation_size = HE_MEGA_BYTES(1));

void* allocate(Memory_Arena *memory_arena, U64 size, U16 alignment);
void* reallocate(Memory_Arena *memory_arena, void *memory, U64 new_size, U16 alignment);
void deallocate(Memory_Arena *memory_arena, void *memory);

HE_FORCE_INLINE void *memory_arena_allocate(void *memory_arena, U64 size, U16 alignment)
{
    return allocate((Memory_Arena *)memory_arena, size, alignment);
}

HE_FORCE_INLINE void *memory_arena_reallocate(void *memory_arena, void *memory, U64 new_size, U16 alignment)
{
    return reallocate((Memory_Arena *)memory_arena, memory, new_size, alignment);
}

HE_FORCE_INLINE void memory_arena_deallocate(void *memory_arena, void *memory)
{
    return deallocate((Memory_Arena *)memory_arena, memory);
}

HE_FORCE_INLINE Allocator to_allocator(Memory_Arena *memory_arena)
{
    return { .data = memory_arena, .allocate = &memory_arena_allocate, .reallocate = &memory_arena_reallocate, .deallocate = &memory_arena_deallocate };
}

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
// Temprary Memory Arena Janitor
//

struct Temprary_Memory_Arena_Janitor
{
    Memory_Arena *arena;
    U64 offset;

    ~Temprary_Memory_Arena_Janitor();
};

Temprary_Memory_Arena_Janitor make_temprary_memory_arena_janitor(Memory_Arena *arena);

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
    U64 capacity;
    U64 min_allocation_size;
    U64 size;
    U64 used;
    Free_List_Node sentinal;
    Mutex mutex;
};

bool init_free_list_allocator(Free_List_Allocator *allocator, void *memory, U64 capacity, U64 size);

void* allocate(Free_List_Allocator *allocator, U64 size, U16 alignment);
void* reallocate(Free_List_Allocator *allocator, void *memory, U64 new_size, U16 alignment);
void deallocate(Free_List_Allocator *allocator, void *memory);

HE_FORCE_INLINE void *free_list_allocator_allocate(void *free_list_allocator, U64 size, U16 alignment)
{
    return allocate((Free_List_Allocator *)free_list_allocator, size, alignment);
}

HE_FORCE_INLINE void *free_list_allocator_reallocate(void *free_list_allocator, void *memory, U64 new_size, U16 alignment)
{
    return reallocate((Free_List_Allocator *)free_list_allocator, memory, new_size, alignment);
}

HE_FORCE_INLINE void free_list_allocator_deallocate(void *free_list_allocator, void *memory)
{
    return deallocate((Free_List_Allocator *)free_list_allocator, memory);
}

HE_FORCE_INLINE Allocator to_allocator(Free_List_Allocator *allocator)
{
    return { .data = allocator, .allocate = &free_list_allocator_allocate, .reallocate = &free_list_allocator_reallocate, .deallocate = &free_list_allocator_deallocate };
}

bool init_memory_system();
void deinit_memory_system();

void imgui_draw_memory_system();

struct Thread_Memory_State
{
    Memory_Arena transient_arena;
};

Thread_Memory_State *get_thread_memory_state(U32 thread_id);

Memory_Arena *get_permenent_arena();
Memory_Arena *get_transient_arena();
Memory_Arena *get_debug_arena();
Free_List_Allocator *get_general_purpose_allocator();

Temprary_Memory_Arena begin_scratch_memory();
Temprary_Memory_Arena_Janitor make_scratch_memory_janitor();

struct Memory_Context
{
    Memory_Arena *permenent;

    U64 temp_offset;
    Memory_Arena *temp;

    Memory_Arena *debug;
    Free_List_Allocator *general;

    ~Memory_Context();
};

Memory_Context use_memory_context();