#include "memory.h"

#include <string.h>

void zero_memory(void *memory, Size size)
{
    HE_ASSERT(memory);
    memset(memory, 0, size);
}

void copy_memory(void *dst, const void *src, Size size)
{
    HE_ASSERT(dst);
    HE_ASSERT(src);
    HE_ASSERT(size);
    memcpy(dst, src, size);
}

//
// Memory Arena
//

Memory_Arena create_memory_arena(void *memory, Size size)
{
    HE_ASSERT(memory);
    HE_ASSERT(size);

    Memory_Arena result = {};
    result.base = (U8 *)memory;
    result.size = size;

    return result;
}

Memory_Arena create_sub_arena(Memory_Arena *arena, Size size)
{
    HE_ASSERT(arena);
    HE_ASSERT(size);

    Memory_Arena result = {};
    result.base = HE_ALLOCATE_ARRAY(arena, U8, size);
    result.size = size;

    return result;
}

HE_FORCE_INLINE static bool is_power_of_2(U16 value)
{
    return (value & (value - 1)) == 0;
}

static Size get_number_of_bytes_to_align_address(Size address, U16 alignment)
{
    // todo(amer): branchless version daddy
    Size result = 0;

    if (alignment)
    {
        HE_ASSERT(is_power_of_2(alignment));
        Size modulo = address & (alignment - 1);

        if (modulo != 0)
        {
            result = alignment - modulo;
        }
    }

    return result;
}

void* allocate(Memory_Arena *arena, Size size, U16 alignment, Temprary_Memory_Arena *parent)
{
    HE_ASSERT(arena);
    HE_ASSERT(size);
    HE_ASSERT(arena->parent == parent);

    void *result = 0;
    U8 *cursor = arena->base + arena->offset;
    Size padding = get_number_of_bytes_to_align_address((Size)cursor, alignment);
    HE_ASSERT(arena->offset + size + padding <= arena->size);
    result = cursor + padding;
    arena->last_padding = padding;
    arena->last_offset = arena->offset;
    arena->offset += padding + size;
    zero_memory(result, size);
    return result;
}

void* reallocate(Memory_Arena *arena, void *memory, Size new_size, U16 alignment, Temprary_Memory_Arena *parent)
{
    HE_ASSERT(arena);
    HE_ASSERT(memory);
    HE_ASSERT(new_size);
    HE_ASSERT(arena->parent == parent);

    if (!memory)
    {
        HE_ASSERT(arena->base + arena->last_offset + arena->last_padding == memory);
        arena->offset = arena->last_offset;
    }

    return allocate(arena, new_size, alignment, parent);
}

void deallocate(Memory_Arena *arena, void *memory)
{
    HE_ASSERT(false);
}

//
// Temprary Memory Arena
//

void begin_temprary_memory_arena(Temprary_Memory_Arena *temprary_memory_arena, Memory_Arena *arena)
{
    HE_ASSERT(temprary_memory_arena);
    HE_ASSERT(arena);

    temprary_memory_arena->arena = arena;
    temprary_memory_arena->offset = arena->offset;

#ifndef HE_SHIPPING
    temprary_memory_arena->parent = arena->parent;
    arena->parent = temprary_memory_arena;
#endif
}

void end_temprary_memory_arena(Temprary_Memory_Arena *temprary_arena)
{
    Memory_Arena *arena = temprary_arena->arena;
    HE_ASSERT(arena);

    arena->offset = temprary_arena->offset;
    arena->parent = temprary_arena->parent;

#ifndef HE_SHIPPING
    temprary_arena->arena = nullptr;
    temprary_arena->offset = 0;
#endif
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

void init_free_list_allocator(Free_List_Allocator *allocator, void *memory, Size size)
{
    HE_ASSERT(allocator);
    HE_ASSERT(size >= sizeof(Free_List_Node));

    allocator->base = (U8 *)memory;
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
}

void init_free_list_allocator(Free_List_Allocator *allocator, Memory_Arena *arena, Size size)
{
    HE_ASSERT(arena);
    U8 *base = HE_ALLOCATE_ARRAY(arena, U8, size);
    init_free_list_allocator(allocator, base, size);
}

struct Free_List_Allocation_Header
{
    Size size;
    Size offset;
    Size reserved;
};

static_assert(sizeof(Free_List_Allocation_Header) == sizeof(Free_List_Node));

void* allocate(Free_List_Allocator *allocator, Size size, U16 alignment)
{
    HE_ASSERT(allocator);
    HE_ASSERT(size);

    platform_lock_mutex(&allocator->mutex);

    void *result = nullptr;

    for (Free_List_Node *node = allocator->sentinal.next;
         node != &allocator->sentinal;
         node = node->next)
    {
        Size node_address = (Size)node;
        Size offset = get_number_of_bytes_to_align_address(node_address, alignment);
        if (offset < sizeof(Free_List_Allocation_Header))
        {
            node_address += sizeof(Free_List_Allocation_Header);
            offset = sizeof(Free_List_Allocation_Header) + get_number_of_bytes_to_align_address(node_address, alignment);
        }

        Size allocation_size = offset + size;
        if (node->size >= allocation_size)
        {
            Size remaining_size = node->size - allocation_size;

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
    }

    HE_ASSERT(result);
    zero_memory(result, size);
    platform_unlock_mutex(&allocator->mutex);
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
    
    for (Free_List_Node *node = allocator->sentinal.next;
         node != &allocator->sentinal;
         node = node->next)
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
    copy_memory(new_memory, memory, header.size);
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

    HE_DEFER
    {
        platform_unlock_mutex(&allocator->mutex);
    };

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

    for (Free_List_Node *node = allocator->sentinal.next;
         node != &allocator->sentinal;
         node = node->next)
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