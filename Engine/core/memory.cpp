#include "memory.h"
#include "platform.h"
#include "rendering/renderer.h"
#include "containers/hash_map.h"
#include "core/logging.h"

#include <string.h>

#include <imgui.h>

void zero_memory(void *memory, U64 size)
{
    HE_ASSERT(memory);
    memset(memory, 0, size);
}

void copy_memory(void *dst, const void *src, U64 size)
{
    HE_ASSERT(dst);
    HE_ASSERT(src);
    HE_ASSERT(size);
    memcpy(dst, src, size);
}

struct Memory_System
{
    U64 thread_transient_arena_capacity;

    Memory_Arena permenent_arena;
    Memory_Arena debug_arena;
    Free_List_Allocator general_purpose_allocator;

    Hash_Map< U32, Thread_Memory_State > thread_id_to_memory_state;
};

static Memory_System memory_system_state;

bool init_memory_system()
{
    memory_system_state.thread_transient_arena_capacity = HE_MEGA_BYTES(64);
    U64 capacity = platform_get_total_memory_size();

    if (!init_memory_arena(&memory_system_state.permenent_arena, capacity, HE_MEGA_BYTES(64)))
    {
        return false;
    }

    if (!init_memory_arena(&memory_system_state.debug_arena, capacity, HE_MEGA_BYTES(64)))
    {
        return false;
    }

    if (!init_free_list_allocator(&memory_system_state.general_purpose_allocator, nullptr, capacity, HE_MEGA_BYTES(256), "general_purpose_allocator"))
    {
        return false;
    }

    init(&memory_system_state.thread_id_to_memory_state, get_effective_thread_count(), to_allocator(&memory_system_state.permenent_arena));

    S32 slot_index = insert(&memory_system_state.thread_id_to_memory_state, platform_get_current_thread_id());
    HE_ASSERT(slot_index != -1);

    Thread_Memory_State *main_thread_memory_state = &memory_system_state.thread_id_to_memory_state.values[slot_index];
    if (!init_memory_arena(&main_thread_memory_state->transient_arena, capacity, memory_system_state.thread_transient_arena_capacity))
    {
        return false;
    }

    return true;
}

void deinit_memory_system()
{
    HE_ASSERT(memory_system_state.permenent_arena.temp_count == 0);
    HE_ASSERT(memory_system_state.debug_arena.temp_count == 0);
}

Thread_Memory_State *get_thread_memory_state(U32 thread_id)
{
    auto it = find(&memory_system_state.thread_id_to_memory_state, thread_id);

    if (is_valid(it))
    {
        return it.value;
    }

    S32 slot_index = insert(&memory_system_state.thread_id_to_memory_state, thread_id);
    Thread_Memory_State *thread_memory_state = &memory_system_state.thread_id_to_memory_state.values[slot_index];
    if (!init_memory_arena(&thread_memory_state->transient_arena, memory_system_state.thread_transient_arena_capacity, memory_system_state.thread_transient_arena_capacity))
    {
        return nullptr;
    }
    
    return thread_memory_state;
}

Memory_Arena *get_permenent_arena()
{
    return &memory_system_state.permenent_arena;
}

Memory_Arena *get_transient_arena()
{
    U32 thread_id = platform_get_current_thread_id();
    auto it = find(&memory_system_state.thread_id_to_memory_state, thread_id);
    HE_ASSERT(is_valid(it));
    Thread_Memory_State *memory_state = it.value;
    return &memory_state->transient_arena;
}

Memory_Arena *get_debug_arena()
{
    return &memory_system_state.debug_arena;
}

Free_List_Allocator *get_general_purpose_allocator()
{
    return &memory_system_state.general_purpose_allocator;
}

Temprary_Memory_Arena begin_scratch_memory()
{
    return begin_temprary_memory(get_transient_arena());
}

Temprary_Memory_Arena_Janitor make_scratch_memory_janitor()
{
    Memory_Arena *transient_arena = get_transient_arena();
    transient_arena->temp_count++;
    return { .arena = transient_arena, .offset = transient_arena->offset };
}

Memory_Context::~Memory_Context()
{
    HE_ASSERT(temp->temp_count);
    temp->temp_count--;
    temp->offset = temp_offset;
}

Memory_Context use_memory_context()
{
    Memory_Arena *transient_arena = get_transient_arena();
    transient_arena->temp_count++;

    return
    {
        .permenent = &memory_system_state.permenent_arena,
        .temp_offset = transient_arena->offset,
        .temp = transient_arena,
        .debug = &memory_system_state.debug_arena,
        .general = &memory_system_state.general_purpose_allocator
    };
}

//
// Memory Arena
//

bool init_memory_arena(Memory_Arena *arena, U64 capacity, U64 min_allocation_size)
{
    HE_ASSERT(capacity >= min_allocation_size);

    void *memory = platform_reserve_memory(capacity);
    if (!memory)
    {
        return false;
    }

    if (!platform_commit_memory(memory, min_allocation_size))
    {
        return false;
    }

    arena->base = (U8 *)memory;
    arena->capacity = capacity;
    arena->min_allocation_size = min_allocation_size;
    arena->size = min_allocation_size;
    arena->offset = 0;
    arena->temp_count = 0;

    return true;
}

HE_FORCE_INLINE static bool is_power_of_2(U16 value)
{
    return (value & (value - 1)) == 0;
}

U64 get_number_of_bytes_to_align_address(uintptr_t address, U16 alignment)
{
    // todo(amer): branchless version daddy
    U64 result = 0;

    if (alignment)
    {
        HE_ASSERT(is_power_of_2(alignment));
        U64 modulo = address & (alignment - 1);

        if (modulo != 0)
        {
            result = alignment - modulo;
        }
    }

    return result;
}

void* allocate(Memory_Arena *arena, U64 size, U16 alignment)
{
    HE_ASSERT(arena);
    HE_ASSERT(size);
    void *result = 0;
    U8 *cursor = arena->base + arena->offset;
    U64 padding = get_number_of_bytes_to_align_address((uintptr_t)cursor, alignment);
    U64 allocation_size = size + padding;

    if (arena->offset + allocation_size > arena->size)
    {
        U64 commit_size = HE_MAX(allocation_size, arena->min_allocation_size);
        HE_ASSERT(arena->size + commit_size <= arena->capacity);
        bool commited = platform_commit_memory(arena->base + arena->size, commit_size);
        HE_ASSERT(commited);
        arena->size += commit_size;
    }

    result = cursor + padding;
    arena->offset += allocation_size;
    zero_memory(result, size);
    return result;
}

void *reallocate(Memory_Arena *arena, void *memory, U64 new_size, U16 alignment)
{
    HE_ASSERT(false);
    return nullptr;
}

void deallocate(Memory_Arena *arena, void *memory)
{
    HE_ASSERT(false);
}

//
// Temprary Memory Arena
//

Temprary_Memory_Arena begin_temprary_memory(Memory_Arena *arena)
{
    HE_ASSERT(arena);
    arena->temp_count++;
    return { .arena = arena, .offset = arena->offset };
}

void end_temprary_memory(Temprary_Memory_Arena *temprary_arena)
{
    Memory_Arena *arena = temprary_arena->arena;
    HE_ASSERT(arena);
    HE_ASSERT(arena->temp_count);
    arena->temp_count--;

    arena->offset = temprary_arena->offset;
}

//
// Temprary Memory Arena Janitor
//

Temprary_Memory_Arena_Janitor::~Temprary_Memory_Arena_Janitor()
{
    HE_ASSERT(arena->temp_count);
    arena->temp_count--;

    arena->offset = offset;
}

Temprary_Memory_Arena_Janitor make_temprary_memory_arena_janitor(Memory_Arena *arena)
{
    HE_ASSERT(arena);
    arena->temp_count++;

    Temprary_Memory_Arena_Janitor result;
    result.arena = arena;
    result.offset = arena->offset;
    return result;
}

//
// Free List Allocator
//

static size_t calc_padding_with_header(uintptr_t ptr, uintptr_t alignment, uintptr_t header_size)
{
    HE_ASSERT(is_power_of_2((U16)alignment));
    uintptr_t modulo = ptr & (alignment - 1); // same as ptr % alignment

    uintptr_t padding = 0;
    if (modulo != 0)
    {
        padding = alignment - modulo;
    }

    uintptr_t needed = header_size;
    if (padding < needed)
    {
        needed -= padding;
        if ((needed & (alignment - 1)) == 0)
        {
            padding += alignment * (needed / alignment);
        }
        else
        {
            padding += alignment * (1 + (needed / alignment));
        }
    }
    return padding;
}

static void insert_node(Free_List_Allocator *allocator, Free_List_Node *prev_node, Free_List_Node *new_node)
{
    if (prev_node)
    {
        if (prev_node->next)
        {
            new_node->next = prev_node->next;
            prev_node->next = new_node;
        }
        else
        {
            prev_node->next = new_node;
            new_node->next = nullptr;
        }
    }
    else
    {
        new_node->next = allocator->head;
        allocator->head = new_node;
    }

    HE_ASSERT(allocator->head->size);
}

static void remove_node(Free_List_Allocator *allocator, Free_List_Node *prev_node, Free_List_Node *node)
{
    if (prev_node)
    {
        prev_node->next = node->next;
    }
    else
    {
        allocator->head = node->next;
    }

    HE_ASSERT(allocator->head->size);
}

static void merge(Free_List_Allocator *allocator, Free_List_Node *prev_node, Free_List_Node *node)
{
    if (node->next && (U8 *)node + node->size == (U8 *)node->next)
    {
        node->size += node->next->size;
        remove_node(allocator, node, node->next);
    }

    if (prev_node && prev_node->next && (U8 *)prev_node + prev_node->size == (U8 *)node)
    {
        prev_node->size += node->size;
        remove_node(allocator, prev_node, node);
    }

    HE_ASSERT(allocator->head->size);
}

bool init_free_list_allocator(Free_List_Allocator *allocator, void *memory, U64 capacity, U64 size, const char *debug_name)
{
    HE_ASSERT(allocator);
    HE_ASSERT(size >= sizeof(Free_List_Node));
    HE_ASSERT(capacity >= size);

    if (!memory)
    {
        memory = platform_reserve_memory(capacity);
        if (!memory)
        {
            return false;
        }

        bool commited = platform_commit_memory(memory, size);
        if (!commited)
        {
            return false;
        }
    }

    allocator->base = (U8 *)memory;
    allocator->capacity = capacity;
    allocator->min_allocation_size = size;
    allocator->size = size;
    allocator->used = 0;
    allocator->debug_name = debug_name;

    Free_List_Node *first_free_node = (Free_List_Node *)allocator->base;
    first_free_node->size = size;
    first_free_node->next = nullptr;
    allocator->head = first_free_node;

    bool mutex_created = platform_create_mutex(&allocator->mutex);
    HE_ASSERT(mutex_created);

    return true;
}

struct Free_List_Allocation_Header
{
    U64 size;
    U64 padding;
};

static_assert(sizeof(Free_List_Allocation_Header) == sizeof(Free_List_Node));

static void* allocate_internal(Free_List_Allocator *allocator, U64 size, U16 alignment)
{
    HE_ASSERT(allocator);
    HE_ASSERT(size);

    Free_List_Node *alloc_node = nullptr;
    Free_List_Node *prev_node = nullptr;
    U64 padding = 0;
    U64 required_size = 0;

    for (Free_List_Node *node = allocator->head; node; node = node->next)
    {
        padding = calc_padding_with_header((uintptr_t)node, (uintptr_t)alignment, sizeof(Free_List_Allocation_Header));
        required_size = size + padding;

        if (node->size >= required_size)
        {
            alloc_node = node;
            break;
        }

        prev_node = node;
    }

    HE_ASSERT(alloc_node);

    U64 remaining = alloc_node->size - required_size;
    if (remaining > sizeof(Free_List_Node))
    {
        Free_List_Node *new_node = (Free_List_Node *)((U8 *)alloc_node + required_size);
        new_node->size = remaining;
        insert_node(allocator, alloc_node, new_node);
    }

    remove_node(allocator, prev_node, alloc_node);

    Free_List_Allocation_Header *header = (Free_List_Allocation_Header *)((U8 *)alloc_node + padding - sizeof(Free_List_Allocation_Header));
    header->size = required_size;
    header->padding = padding;

    allocator->used += required_size;

    void *result = (void *)((U8 *)alloc_node + padding);
    zero_memory(result, size);
    return result;
}

void* allocate(Free_List_Allocator *allocator, U64 size, U16 alignment)
{
    platform_lock_mutex(&allocator->mutex);
    void *result = allocate_internal(allocator, size, alignment);
    platform_unlock_mutex(&allocator->mutex);
    return result;
}

static void deallocate_internal(Free_List_Allocator *allocator, void *memory)
{
    if (!memory)
    {
        return;
    }

    HE_ASSERT((U8*)memory >= allocator->base && (U8*)memory <= allocator->base + allocator->size);

    Free_List_Allocation_Header *header = (Free_List_Allocation_Header *)((U8*)memory - sizeof(Free_List_Allocation_Header));

    HE_ASSERT(header->padding >= sizeof(Free_List_Allocation_Header));
    HE_ASSERT(header->size > 0);

    U64 size = header->size;
    U64 padding = header->padding;

    Free_List_Node *free_node = (Free_List_Node *)((U8 *)memory - padding);
    free_node->size = size;
    free_node->next = nullptr;

    Free_List_Node *prev_node = nullptr;
    for (Free_List_Node *node = allocator->head; node; node = node->next)
    {
        if ((U8 *)memory < (U8 *)node)
        {
            insert_node(allocator, prev_node, free_node);
            break;
        }
        prev_node = node;
    }

    allocator->used -= size;
    merge(allocator, prev_node, free_node);
}

void deallocate(Free_List_Allocator *allocator, void *memory)
{
    platform_lock_mutex(&allocator->mutex);
    deallocate_internal(allocator, memory);
    platform_unlock_mutex(&allocator->mutex);
}

void* reallocate(Free_List_Allocator *allocator, void *memory, U64 new_size, U16 alignment)
{
    if (!memory)
    {
        platform_lock_mutex(&allocator->mutex);
        void *result = allocate_internal(allocator, new_size, alignment);
        platform_unlock_mutex(&allocator->mutex);
        return result;
    }

    platform_lock_mutex(&allocator->mutex);

    HE_ASSERT(allocator);
    HE_ASSERT(memory >= allocator->base && memory <= allocator->base + allocator->size);
    HE_ASSERT(new_size);

    Free_List_Allocation_Header header = ((Free_List_Allocation_Header *)memory)[-1];
    HE_ASSERT(header.size >= 0);
    HE_ASSERT(header.padding >= 0);
    HE_ASSERT(header.padding < allocator->size);
    HE_ASSERT(new_size != header.size);

    U64 old_size = header.size - header.padding;
    void *new_memory = allocate_internal(allocator, new_size, alignment);
    copy_memory(new_memory, memory, old_size);
    deallocate_internal(allocator, memory);

    platform_unlock_mutex(&allocator->mutex);

    return new_memory;
}