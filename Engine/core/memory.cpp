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
    U64 thread_arena_capacity;

    Memory_Arena permenent_arena;
    Allocator permenent_allocator;

    Memory_Arena frame_arena;
    Allocator frame_allocator;

    Memory_Arena debug_arena;
    Allocator debug_allocator;

    Free_List_Allocator general_free_list_allocator;
    Allocator general_allocator;

    Hash_Map< U32, Thread_Memory_State > thread_id_to_memory_state;
};

static Memory_System memory_system_state;

bool init_memory_system()
{
    memory_system_state.thread_arena_capacity = HE_MEGA_BYTES(128);
    U64 capacity = platform_get_total_memory_size();

    if (!init_memory_arena(&memory_system_state.permenent_arena, capacity, HE_MEGA_BYTES(64)))
    {
        return false;
    }

    memory_system_state.permenent_allocator = to_allocator(&memory_system_state.permenent_arena);

    if (!init_memory_arena(&memory_system_state.frame_arena, capacity, HE_MEGA_BYTES(64)))
    {
        return false;
    }

    memory_system_state.frame_allocator = to_allocator(&memory_system_state.frame_arena);

    if (!init_memory_arena(&memory_system_state.debug_arena, capacity, HE_MEGA_BYTES(64)))
    {
        return false;
    }

    memory_system_state.debug_allocator = to_allocator(&memory_system_state.debug_arena);

    if (!init_free_list_allocator(&memory_system_state.general_free_list_allocator, nullptr, capacity, HE_MEGA_BYTES(1024), "general_free_list_allocator"))
    {
        return false;
    }

    memory_system_state.general_allocator = to_allocator(&memory_system_state.general_free_list_allocator);

    init(&memory_system_state.thread_id_to_memory_state, get_effective_thread_count(), to_allocator(&memory_system_state.permenent_arena));

    S32 slot_index = insert(&memory_system_state.thread_id_to_memory_state, platform_get_current_thread_id());
    HE_ASSERT(slot_index != -1);

    Thread_Memory_State *main_thread_memory_state = &memory_system_state.thread_id_to_memory_state.values[slot_index];
    if (!init_memory_arena(&main_thread_memory_state->arena, capacity, memory_system_state.thread_arena_capacity))
    {
        return false;
    }

    return true;
}

void deinit_memory_system()
{
    HE_ASSERT(memory_system_state.permenent_arena.temp_count == 0);
    HE_ASSERT(memory_system_state.frame_arena.temp_count == 0);
    HE_ASSERT(memory_system_state.debug_arena.temp_count == 0);
    Memory_Arena *arena = get_thread_arena();
    HE_ASSERT(arena->temp_count == 0);
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
    if (!init_memory_arena(&thread_memory_state->arena, memory_system_state.thread_arena_capacity, memory_system_state.thread_arena_capacity))
    {
        return nullptr;
    }
    
    return thread_memory_state;
}

Memory_Arena* get_permenent_arena()
{
    return &memory_system_state.permenent_arena;
}

Memory_Arena* get_thread_arena()
{
    U32 thread_id = platform_get_current_thread_id();
    auto it = find(&memory_system_state.thread_id_to_memory_state, thread_id);
    HE_ASSERT(is_valid(it));
    Thread_Memory_State *memory_state = it.value;
    return &memory_state->arena;
}

Memory_Arena* get_frame_arena()
{
    return &memory_system_state.frame_arena;
}

Memory_Context::~Memory_Context()
{
    if (!dropped)
    {
        end_temprary_memory(temprary_memory);
    }
}

Memory_Context grab_memory_context()
{
    Memory_Arena *arena = get_thread_arena();

    return
    {
        .permenent_allocator = memory_system_state.permenent_allocator,
        .general_allocator   = memory_system_state.general_allocator,
        .frame_allocator     = memory_system_state.frame_allocator,
        .temprary_memory     = begin_temprary_memory(arena),
        .temp_allocator      = to_allocator(arena),
        .dropped             = false
    };
}

bool drop_memory_context(Memory_Context *memory_context, Allocator allocator)
{
    HE_ASSERT(memory_context);
    HE_ASSERT(allocator.data);

    if (memory_context->temp_allocator.data == allocator.data)
    {
        Memory_Arena *arena = memory_context->temprary_memory.arena;
        HE_ASSERT(arena->temp_count);
        arena->temp_count--;

        memory_context->dropped = true;
        return true;
    }

    return false;
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

void *reallocate(Memory_Arena *arena, void *memory, U64 old_size, U64 new_size, U16 alignment)
{ 
    if (!memory)
    {
        return allocate(arena, new_size, alignment);
    }

    if (new_size == old_size)
    {
        return memory;
    }

    HE_ASSERT((U8 *)memory >= arena->base && (U8 *)memory <= arena->base + arena->size);
    if (arena->base + arena->offset - old_size == (U8 *)memory) // memory is the last allocation in the arena
    {
        if (new_size > old_size)
        {
            arena->offset += new_size - old_size;
        }
        else
        {
            arena->offset -= old_size - new_size;
        }

        return memory;
    }
    
    void *new_memory = allocate(arena, new_size, alignment);
    copy_memory(new_memory, memory, HE_MIN(old_size, new_size));
    return new_memory;
}

void deallocate(Memory_Arena *arena, void *memory)
{
}

void *memory_arena_allocate(void *memory_arena, U64 size, U16 alignment)
{
    return allocate((Memory_Arena *)memory_arena, size, alignment);
}

void *memory_arena_reallocate(void *memory_arena, void *memory, U64 old_size, U64 new_size, U16 alignment)
{
    return reallocate((Memory_Arena *)memory_arena, memory, old_size, new_size, alignment);
}

void memory_arena_deallocate(void *memory_arena, void *memory)
{
    return deallocate((Memory_Arena *)memory_arena, memory);
}

//
// Temprary Memory
//

Temprary_Memory begin_temprary_memory(Memory_Arena *arena)
{
    HE_ASSERT(arena);
    arena->temp_count++;
    return { .arena = arena, .offset = arena->offset };
}

void end_temprary_memory(Temprary_Memory temprary_memory)
{
    Memory_Arena *arena = temprary_memory.arena;
    HE_ASSERT(arena);
    HE_ASSERT(arena->temp_count);
    arena->temp_count--;
    arena->offset = temprary_memory.offset;
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
        new_node->next = prev_node->next;
        prev_node->next = new_node;
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
    HE_ASSERT(prev_node);
    HE_ASSERT(node);
    if ((U8 *)prev_node + prev_node->size == (U8 *)node)
    {
        prev_node->size += node->size;
        remove_node(allocator, prev_node, node);
    }
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

static void dump_free_list_allocator(Free_List_Allocator *allocator) {
    HE_LOG(Core, Debug, "dumping allocator %s\n", allocator->debug_name);
    for (Free_List_Node *node = allocator->head; node; node = node->next) {
        HE_LOG(Core, Debug, "node: addr -> %p, size -> %d\n", (void *)node, node->size);
    }
}

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

    for (Free_List_Node *node = allocator->head; node; node = node->next)
    {
        HE_ASSERT((U8*)memory < (U8*)node || (U8*)memory > (U8*)node + node->size);
    }

    U64 size = header->size;
    U64 padding = header->padding;

    Free_List_Node *free_node = (Free_List_Node *)((U8 *)memory - padding);
    free_node->size = size;
    free_node->next = nullptr;

    Free_List_Node *prev_node = nullptr;
    for (Free_List_Node *node = allocator->head; node; node = node->next)
    {
        if ((U8 *)node > (U8 *)memory)
        {
            insert_node(allocator, prev_node, free_node);
            break;
        }
        prev_node = node;
    }

    allocator->used -= size;
    if (prev_node)
    {
        merge(allocator, prev_node, free_node);
    }

    if (free_node->next)
    {
        merge(allocator, free_node, free_node->next);
    }
}

void deallocate(Free_List_Allocator *allocator, void *memory)
{
    platform_lock_mutex(&allocator->mutex);
    deallocate_internal(allocator, memory);
    platform_unlock_mutex(&allocator->mutex);
}

void* reallocate(Free_List_Allocator *allocator, void *memory, U64 _, U64 new_size, U16 alignment)
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

    Free_List_Allocation_Header *header = (Free_List_Allocation_Header *)((U8*)memory - sizeof(Free_List_Allocation_Header));
    HE_ASSERT(header->size >= 0);
    HE_ASSERT(header->padding >= 0);
    HE_ASSERT(header->padding < allocator->size);
    HE_ASSERT(new_size != header->size);

    U64 old_size = header->size - header->padding;
    if (old_size == new_size)
    {
        return memory;
    }

    for (Free_List_Node *node = allocator->head; node; node = node->next)
    {
        HE_ASSERT((U8*)memory < (U8*)node || (U8*)memory > (U8*)node + node->size);
    }
    
    void *new_memory = allocate_internal(allocator, new_size, alignment);
    copy_memory(new_memory, memory, HE_MIN(old_size, new_size));
    deallocate_internal(allocator, memory);

    platform_unlock_mutex(&allocator->mutex);

    return new_memory;
}

void *free_list_allocator_allocate(void *free_list_allocator, U64 size, U16 alignment)
{
    return allocate((Free_List_Allocator *)free_list_allocator, size, alignment);
}

void *free_list_allocator_reallocate(void *free_list_allocator, void *memory, U64 old_size, U64 new_size, U16 alignment)
{
    return reallocate((Free_List_Allocator *)free_list_allocator, memory, old_size, new_size, alignment);
}

void free_list_allocator_deallocate(void *free_list_allocator, void *memory)
{
    return deallocate((Free_List_Allocator *)free_list_allocator, memory);
}