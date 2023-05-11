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

//
// Memory Arena
//

Memory_Arena create_memory_arena(void *memory, Mem_Size size)
{
    HE_Assert(memory);
    HE_Assert(size);

    Memory_Arena result = {};
    result.base = (U8 *)memory;
    result.size = size;

    return result;
}

internal_function inline bool
is_power_of_2(U16 value)
{
    return (value & (value - 1)) == 0;
}

internal_function Mem_Size
get_number_of_bytes_to_align_address(Mem_Size address, U16 alignment)
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

void*
allocate(Memory_Arena *arena,
         Mem_Size size, U16 alignment,
         Temprary_Memory_Arena *parent)
{
    HE_Assert(arena);
    HE_Assert(size);
    HE_Assert(arena->current_temprary_owner == parent);

    void *result = 0;
    U8 *cursor = arena->base + arena->offset;
    Mem_Size padding = get_number_of_bytes_to_align_address((Mem_Size)cursor, alignment);
    HE_Assert(arena->offset + size + padding <= arena->size);
    result = cursor + padding;
    arena->offset += padding + size;
    zero_memory(result, padding + size);
    return result;
}

//
// Temprary Memory Arena
//

void begin_temprary_memory_arena(Temprary_Memory_Arena *temprary_memory_arena,
                                 Memory_Arena *arena)
{
    HE_Assert(temprary_memory_arena);
    HE_Assert(arena);

    temprary_memory_arena->arena = arena;
    temprary_memory_arena->offset = arena->offset;

    // todo(amer): this data member is used for debugging purposes only and
    // should be used accessed non-shipping builds only
    temprary_memory_arena->parent = arena->current_temprary_owner;
    arena->current_temprary_owner = temprary_memory_arena;
}

void
end_temprary_memory_arena(Temprary_Memory_Arena *temprary_arena)
{
    Memory_Arena *arena = temprary_arena->arena;
    HE_Assert(arena);

    arena->offset = temprary_arena->offset;
    arena->current_temprary_owner = temprary_arena->parent;

    temprary_arena->arena = nullptr;
    temprary_arena->offset = 0;
}

//
// Scoped Temprary Memory Arena
//

Scoped_Temprary_Memory_Arena::Scoped_Temprary_Memory_Arena(Memory_Arena *arena)
{
    begin_temprary_memory_arena(&temprary_arena, arena);
}

Scoped_Temprary_Memory_Arena::~Scoped_Temprary_Memory_Arena()
{
    end_temprary_memory_arena(&temprary_arena);
}

//
// Free List Allocator
//

internal_function void insert_after(Free_List_Node *node, Free_List_Node *before)
{
    node->next = before->next;
    node->prev = before;

    before->next->prev = node;
    before->next = node;
}

internal_function void remove_node(Free_List_Node *node)
{
    HE_Assert(node->next);
    HE_Assert(node->prev);

    node->prev->next = node->next;
    node->next->prev = node->prev;
}

internal_function void merge(Free_List_Node *left, Free_List_Node *right)
{
    if ((U8 *)left + left->size == (U8 *)right)
    {
        left->size += right->size;
        remove_node(right);
    }
}

void init_free_list_allocator(Free_List_Allocator *allocator,
                              Memory_Arena *arena,
                              Mem_Size size)
{
    HE_Assert(allocator);
    HE_Assert(arena);
    HE_Assert(size >= sizeof(Free_List_Node));

    U8 *base = AllocateArray(arena, U8, size);
    allocator->base = base;
    allocator->size = size;
    allocator->used = 0;

    allocator->sentinal.next = &allocator->sentinal;
    allocator->sentinal.prev = &allocator->sentinal;
    allocator->sentinal.size = 0;

    Free_List_Node *first_free_node = (Free_List_Node *)allocator->base;
    first_free_node->size = size;
    insert_after(first_free_node, &allocator->sentinal);
}

struct Free_List_Allocation_Header
{
    Mem_Size size;
    Mem_Size offset;
    Mem_Size reserved;
};

HE_StaticAssert(sizeof(Free_List_Allocation_Header) == sizeof(Free_List_Node));

void* allocate(Free_List_Allocator *allocator,
               Mem_Size size,
               U16 alignment)
{
    void *result = nullptr;

    for (Free_List_Node *node = allocator->sentinal.next;
         node != &allocator->sentinal;
         node = node->next)
    {
        Mem_Size node_address = (Mem_Size)node;
        Mem_Size offset = get_number_of_bytes_to_align_address(node_address, alignment);
        if (offset < sizeof(Free_List_Allocation_Header))
        {
            node_address += sizeof(Free_List_Allocation_Header);
            offset = sizeof(Free_List_Allocation_Header) + get_number_of_bytes_to_align_address(node_address,
                                                                                                alignment);
        }

        // todo(amer): after an allocation if we have a free space
        // check if there is an free adjacent node and merge with it
        Mem_Size allocation_size = offset + size;
        if (node->size >= allocation_size)
        {
            Mem_Size remaining_size = node->size - allocation_size;

            Free_List_Allocation_Header allocation_header = {};
            allocation_header.size = node->size;
            allocation_header.offset = offset;

            Free_List_Node *before = node->prev;
            remove_node(node);

            if (remaining_size > sizeof(Free_List_Node))
            {
                allocation_header.size = allocation_size;

                Free_List_Node *new_node = (Free_List_Node *)((U8 *)node + allocation_size);
                new_node->size = remaining_size;
                new_node->next = new_node->prev = nullptr; // note(amer): is this nessessary

                insert_after(new_node, before);
            }

            result = (U8*)node + offset;
            ((Free_List_Allocation_Header *)result)[-1] = allocation_header;

            allocator->used += allocation_header.size;
            break;
        }
    }

    HE_Assert(result);
    zero_memory(result, size);
    return result;
}

void deallocate(Free_List_Allocator *allocator, void *memory)
{
    HE_Assert(allocator);
    HE_Assert(memory >= allocator->base && memory <= allocator->base + allocator->size);

    Free_List_Allocation_Header header = ((Free_List_Allocation_Header *)memory)[-1];
    HE_Assert(header.size >= 0);
    HE_Assert(header.offset >= 0);

    allocator->used -= header.size;

    Free_List_Node *new_node = (Free_List_Node *)((U8 *)memory - header.offset);
    new_node->size = header.size;
    new_node->next = new_node->prev = nullptr; // note(amer): is this nessessary

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