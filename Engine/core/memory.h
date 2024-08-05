#pragma once

#include "defines.h"
#include "platform.h"

#define HE_KILO_BYTES(x) (1024llu * (x))
#define HE_MEGA_BYTES(x) (1024llu * 1024llu * (x))
#define HE_GIGA_BYTES(x) (1024llu * 1024llu * 1024llu * (x))
#define HE_TERA_BYTES(x) (1024llu * 1024llu * 1024llu * 1024llu * (x))

#define HE_DEFAULT_ALIGNMENT 16

#define HE_ALLOCATE(allocator_pointer, type) \
(type *)allocate((allocator_pointer), sizeof(type), HE_DEFAULT_ALIGNMENT)

#define HE_ALLOCATE_ALIGNED(allocator_pointer, type) \
(type *)allocate((allocator_pointer), sizeof(type), alignof(type))

#define HE_ALLOCATE_ARRAY(allocator_pointer, type, count) \
(type *)allocate((allocator_pointer), sizeof(type) * (count), alignof(type))

#define HE_REALLOCATE_ARRAY(allocator_pointer, memory, type, count) \
(type *)reallocate((allocator_pointer), memory, 0, sizeof(type) * (count), alignof(type))

#define HE_REALLOCATE_ARRAY_SIZED(allocator_pointer, memory, type, old_count, new_count) \
(type *)reallocate((allocator_pointer), memory, sizeof(type) * (old_count), sizeof(type) * (new_count), alignof(type))

#define HE_ALLOCATOR_ALLOCATE(allocator, type) \
(type *)(allocator).allocate((allocator).data, sizeof(type), HE_DEFAULT_ALIGNMENT)

#define HE_ALLOCATOR_ALLOCATE_ALIGNED(allocator, type) \
(type *)(allocator).allocate((allocator).data, sizeof(type), alignof(type))

#define HE_ALLOCATOR_ALLOCATE_ARRAY(allocator, type, count) \
(type *)(allocator).allocate((allocator).data, sizeof(type) * (count), alignof(type))

#define HE_ALLOCATOR_REALLOCATE_ARRAY(allocator, memory, type, count) \
(type *)(allocator).reallocate((allocator).data, memory, 0, sizeof(type) * (count), alignof(type))

#define HE_ALLOCATOR_REALLOCATE_ARRAY_SIZED(allocator, memory, type, old_count, new_count) \
(type *)(allocator).reallocate((allocator).data, memory, sizeof(type) * (old_count), sizeof(type) * (new_count), alignof(type))

#define HE_ALLOCATOR_DEALLOCATE(allocator, memory) \
(allocator).deallocate((allocator).data, memory)

void zero_memory(void *memory, U64 size);
void copy_memory(void *dst, const void *src, U64 size);

U64 get_number_of_bytes_to_align_address(uintptr_t address, U16 alignment);

struct Allocator
{
    void   *data;
    void* (*allocate)(void *data, U64 size, U16 alignment);
    void* (*reallocate)(void *data, void *memory, U64 old_size, U64 new_size, U16 alignment);
    void  (*deallocate)(void *data, void *memory);
};

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
    S64 temp_count;
};

bool init_memory_arena(Memory_Arena *memory_arena, U64 capacity, U64 min_allocation_size = HE_MEGA_BYTES(1));

void* allocate(Memory_Arena *memory_arena, U64 size, U16 alignment);
void* reallocate(Memory_Arena *memory_arena, void *memory, U64 old_size, U64 new_size, U16 alignment);
void deallocate(Memory_Arena *memory_arena, void *memory);

void *memory_arena_allocate(void *memory_arena, U64 size, U16 alignment);
void *memory_arena_reallocate(void *memory_arena, void *memory, U64 old_size, U64 new_size, U16 alignment);
void memory_arena_deallocate(void *memory_arena, void *memory);

HE_FORCE_INLINE Allocator to_allocator(Memory_Arena *memory_arena)
{
    return { .data = memory_arena, .allocate = &memory_arena_allocate, .reallocate = &memory_arena_reallocate, .deallocate = &memory_arena_deallocate };
}

//
// Temprary Memory
//

struct Temprary_Memory
{
    Memory_Arena *arena;
    U64 offset;
};

Temprary_Memory begin_temprary_memory(Memory_Arena *arena);
void end_temprary_memory(Temprary_Memory temprary_memory);

//
// Free List Allocator
//

struct Free_List_Node
{
    U64 size;
    Free_List_Node *next;
};

struct Free_List_Allocator
{
    const char *debug_name;
    U8 *base;
    U64 capacity;
    U64 size;
    U64 used;
    U64 min_allocation_size;
    Free_List_Node *head;
    Mutex mutex;
};

bool init_free_list_allocator(Free_List_Allocator *allocator, void *memory, U64 capacity, U64 size, const char *name);

void* allocate(Free_List_Allocator *allocator, U64 size, U16 alignment);
void* reallocate(Free_List_Allocator *allocator, void *memory, U64 old_size, U64 new_size, U16 alignment);
void deallocate(Free_List_Allocator *allocator, void *memory);

void *free_list_allocator_allocate(void *free_list_allocator, U64 size, U16 alignment);
void *free_list_allocator_reallocate(void *free_list_allocator, void *memory, U64 old_size, U64 new_size, U16 alignment);
void free_list_allocator_deallocate(void *free_list_allocator, void *memory);

HE_FORCE_INLINE Allocator to_allocator(Free_List_Allocator *allocator)
{
    return { .data = allocator, .allocate = &free_list_allocator_allocate, .reallocate = &free_list_allocator_reallocate, .deallocate = &free_list_allocator_deallocate };
}

bool init_memory_system();
void deinit_memory_system();

struct Thread_Memory_State
{
    Memory_Arena arena;
};

Thread_Memory_State *get_thread_memory_state(U32 thread_id);
Memory_Arena *get_thread_arena();
Memory_Arena *get_frame_arena();

struct Memory_Context
{
    Allocator permenent_allocator;
    Allocator general_allocator;
    Allocator frame_allocator;
    
    Temprary_Memory temprary_memory;
    Allocator temp_allocator;

    bool dropped;

    ~Memory_Context();
};

Memory_Context grab_memory_context();
bool drop_memory_context(Memory_Context *memory_context, Allocator allocator);