#include "memory.h"
#include "platform.h"
#include "rendering/renderer.h"
#include "containers/hash_map.h"

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

    if (!init_free_list_allocator(&memory_system_state.general_purpose_allocator, nullptr, capacity, HE_MEGA_BYTES(256)))
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

void imgui_draw_arena(Memory_Arena *arena, const char *name)
{
    float bytes_to_mega = 1.0f / (1024.0f * 1024.0f);
    ImGui::Text(name);
    ImGui::Text("capacity: %.2f mb", arena->capacity * bytes_to_mega);
    ImGui::Text("min allocation size: %.2f mb", arena->min_allocation_size * bytes_to_mega);
    ImGui::Text("size: %.2f mb", arena->size * bytes_to_mega);
    ImGui::Text("offset: %.2f mb", arena->offset * bytes_to_mega);
    ImGui::Text("temp count: %llu", arena->temp_count);
    ImGui::Separator();
}

void imgui_draw_free_list_allocator(Free_List_Allocator *allocator, const char *name)
{
    float bytes_to_mega = 1.0f / (1024.0f * 1024.0f);

    ImGui::Text(name);
    ImGui::Text("capacity: %.2f mb", allocator->capacity * bytes_to_mega);
    ImGui::Text("min allocation size: %.2f mb", allocator->min_allocation_size * bytes_to_mega);
    ImGui::Text("size: %.2f mb", allocator->size * bytes_to_mega);
    ImGui::Text("used: %.2f mb", allocator->used * bytes_to_mega);

    ImGui::Text("base: 0x%p", allocator->base);
    ImGui::Text("free nodes: ");

    platform_lock_mutex(&allocator->mutex);
    
    for (Free_List_Node *node = allocator->sentinal.next; node != &allocator->sentinal; node = node->next)
    {
        ImGui::Text("0x%p --- %llu bytes", node, node->size);
    }

    platform_unlock_mutex(&allocator->mutex);

    ImGui::Separator();
}

void imgui_draw_memory_system()
{
    ImGui::Begin("Memory");

    imgui_draw_arena(&memory_system_state.permenent_arena, "Permenent Arena");
    imgui_draw_arena(&memory_system_state.debug_arena, "Debug Arena");
    imgui_draw_free_list_allocator(&memory_system_state.general_purpose_allocator, "General Purpose Allocator");

    Render_Context render_context = get_render_context();
    imgui_draw_free_list_allocator(&render_context.renderer_state->transfer_allocator, "Transfer Allocator");

    ImGui::End();
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

HE_FORCE_INLINE static void insert_after(Free_List_Node *node, Free_List_Node *before)
{
    node->next = before->next;
    node->prev = before;

    before->next->prev = node;
    before->next = node;
}

HE_FORCE_INLINE static void remove_node(Free_List_Node *node)
{
    HE_ASSERT(node->next);
    HE_ASSERT(node->prev);

    node->prev->next = node->next;
    node->next->prev = node->prev;
}

static bool merge(Free_List_Node *left, Free_List_Node *right)
{
    if ((U8 *)left + left->size == (U8 *)right)
    {
        left->size += right->size;
        remove_node(right);
        return true;
    }

    return false;
}

bool init_free_list_allocator(Free_List_Allocator *allocator, void *memory, U64 capacity, U64 size)
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

    allocator->sentinal.next = &allocator->sentinal;
    allocator->sentinal.prev = &allocator->sentinal;
    allocator->sentinal.size = 0;

    bool mutex_created = platform_create_mutex(&allocator->mutex);
    HE_ASSERT(mutex_created);

    Free_List_Node *first_free_node = (Free_List_Node *)allocator->base;
    first_free_node->size = size;

    insert_after(first_free_node, &allocator->sentinal);
    return true;
}

struct Free_List_Allocation_Header
{
    U64 size;
    U64 offset;
    U64 reserved;
};

static_assert(sizeof(Free_List_Allocation_Header) == sizeof(Free_List_Node));

void* allocate(Free_List_Allocator *allocator, U64 size, U16 alignment)
{
    HE_ASSERT(allocator);
    HE_ASSERT(size);

    platform_lock_mutex(&allocator->mutex);

    void *result = nullptr;

    for (Free_List_Node *node = allocator->sentinal.next; node != &allocator->sentinal; node = node->next)
    {
        U64 node_address = (U64)node;
        U64 offset = get_number_of_bytes_to_align_address(node_address, alignment);
        if (offset < sizeof(Free_List_Allocation_Header))
        {
            node_address += sizeof(Free_List_Allocation_Header);
            offset = sizeof(Free_List_Allocation_Header) + get_number_of_bytes_to_align_address(node_address, alignment);
        }

        U64 allocation_size = offset + size;
        if (node->size >= allocation_size)
        {
            U64 remaining_size = node->size - allocation_size;

            Free_List_Allocation_Header allocation_header = {};
            allocation_header.offset = offset;

            if (remaining_size > sizeof(Free_List_Node))
            {
                allocation_header.size = allocation_size;

                Free_List_Node *new_node = (Free_List_Node *)((U8 *)node + allocation_size);
                new_node->size = remaining_size;
                insert_after(new_node, node->prev);
            }
            else
            {
                allocation_header.size = node->size;
            }

            remove_node(node);
            result = (U8*)node + offset;
            ((Free_List_Allocation_Header *)result)[-1] = allocation_header;

            allocator->used += allocation_header.size;
            break;
        }
        else if (node->next == &allocator->sentinal) // last node
        {
            // todo(amer): do more testing here i don't trust this...
            U64 commit_size = HE_MAX(allocation_size, allocator->min_allocation_size);
            if (allocator->size + commit_size <= allocator->capacity)
            {
                bool commited = platform_commit_memory(allocator->base + allocator->size, commit_size);
                HE_ASSERT(commited);
                Free_List_Node *new_node = (Free_List_Node *)(allocator->base + allocator->size);
                new_node->size = commit_size;
                allocator->size += commit_size;
                insert_after(new_node, node);
                merge(node, new_node);
                node = node->prev;
            }
        }
    }

    platform_unlock_mutex(&allocator->mutex);

    HE_ASSERT(result);
    zero_memory(result, size);
    return result;
}

void* reallocate(Free_List_Allocator *allocator, void *memory, U64 new_size, U16 alignment)
{
    if (!memory)
    {
        return allocate(allocator, new_size, alignment);
    }

    HE_ASSERT(allocator);
    HE_ASSERT(memory >= allocator->base && memory <= allocator->base + allocator->size);
    HE_ASSERT(new_size);

    Free_List_Allocation_Header &header = ((Free_List_Allocation_Header *)memory)[-1];
    HE_ASSERT(header.size >= 0);
    HE_ASSERT(header.offset >= 0);
    HE_ASSERT(header.offset < allocator->size);
    HE_ASSERT(new_size != header.size);

    platform_lock_mutex(&allocator->mutex);

    U8 *memory_node_address = (U8 *)memory - header.offset;
    U64 old_size = header.size - header.offset;

    Free_List_Node *adjacent_node_after_memory = nullptr;
    Free_List_Node *node_before_memory = &allocator->sentinal;
    
    for (Free_List_Node *node = allocator->sentinal.next; node != &allocator->sentinal; node = node->next)
    {
        if ((U8 *)node < memory_node_address)
        {
            node_before_memory = node;
        }

        if (memory_node_address + header.size == (U8 *)node)
        {
            adjacent_node_after_memory = node;
            break;
        }
    }

    if (new_size < old_size)
    {
        U64 amount = old_size - new_size;

        if (adjacent_node_after_memory)
        {
            header.size = new_size + sizeof(Free_List_Allocation_Header);

            U64 adjacent_node_after_memory_size = adjacent_node_after_memory->size;
            remove_node(adjacent_node_after_memory);

            Free_List_Node *new_node = (Free_List_Node *)((U8 *)memory + new_size);
            new_node->size = adjacent_node_after_memory_size + amount;

            insert_after(new_node, node_before_memory);
        }
        else
        {
            if (amount >= sizeof(Free_List_Node))
            {
                header.size = new_size + sizeof(Free_List_Allocation_Header);

                Free_List_Node *new_node = (Free_List_Node *)((U8 *)memory_node_address + header.size);
                new_node->size = amount;

                insert_after(new_node, node_before_memory);
            }
        }

        platform_unlock_mutex(&allocator->mutex);
        return memory;
    }
    else
    {
        if (adjacent_node_after_memory)
        {
            if (new_size <= old_size + adjacent_node_after_memory->size)
            {
                U64 remaining = (old_size + adjacent_node_after_memory->size) - new_size;
                if (remaining >= sizeof(Free_List_Node))
                {
                    header.size = new_size + sizeof(Free_List_Allocation_Header);
                    remove_node(adjacent_node_after_memory);

                    Free_List_Node *new_node = (Free_List_Node *)(memory_node_address + header.size);
                    new_node->size = remaining;
                    insert_after(new_node, node_before_memory);
                }
                else
                {
                    header.size += adjacent_node_after_memory->size;
                    remove_node(adjacent_node_after_memory);
                }

                platform_unlock_mutex(&allocator->mutex);
                return memory;
            }
        }
    }

    platform_unlock_mutex(&allocator->mutex);

    void *new_memory = allocate(allocator, new_size, alignment);
    copy_memory(new_memory, memory, old_size);
    deallocate(allocator, memory);

    return new_memory;
}

void deallocate(Free_List_Allocator *allocator, void *memory)
{
    if (!memory)
    {
        return;
    }

    platform_lock_mutex(&allocator->mutex);
    HE_DEFER { platform_unlock_mutex(&allocator->mutex); };

    HE_ASSERT(allocator);
    HE_ASSERT(memory >= allocator->base && memory <= allocator->base + allocator->size);

    Free_List_Allocation_Header header = ((Free_List_Allocation_Header *)memory)[-1];
    HE_ASSERT(header.size >= 0);
    HE_ASSERT(header.offset >= 0);

    allocator->used -= header.size;

    Free_List_Node *new_node = (Free_List_Node *)((U8 *)memory - header.offset);
    new_node->size = header.size;

    if (allocator->sentinal.next == &allocator->sentinal)
    {
        insert_after(new_node, &allocator->sentinal);
        return;
    }

    for (Free_List_Node *node = allocator->sentinal.next; node != &allocator->sentinal; node = node->next)
    {
        bool inserted = false;

        if ((new_node < node) && (new_node > node->prev || node->prev == &allocator->sentinal))
        {
            insert_after(new_node, node->prev);
            inserted = true;
        }

        if ((new_node > node) && (new_node < node->next || node->next == &allocator->sentinal))
        {
            insert_after(new_node, node);
            inserted = true;
        }

        if (inserted)
        {
            merge(new_node, new_node->next);
            merge(new_node->prev, new_node);
            break;
        }
    }
}