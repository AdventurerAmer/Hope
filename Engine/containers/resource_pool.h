#pragma once

#include "core/defines.h"
#include "core/memory.h"

template< typename T >
struct Resource_Handle
{
    U32 index;
    U32 generation;
};

struct Resource_Pool_Node
{
    S32 next;
};

template< typename T >
struct Resource_Pool
{
    static_assert(sizeof(T) >= sizeof(U32));

    void *memory;

    T *data;
    U32 *generations;

    U32 capacity;
    U32 count;
    S32 first_free_node_index;

    Allocator allocator;
};

template< typename T, typename Allocator >
void init(Resource_Pool< T > *resource_pool, Allocator *allocator, U32 capacity)
{
    HE_ASSERT(resource_pool);
    HE_ASSERT(allocator);
    HE_ASSERT(capacity);

    Size size = (sizeof(T) + sizeof(U32)) * capacity;
    U8 *memory = HE_ALLOCATE_ARRAY(allocator, U8, size);
    resource_pool->memory = memory;
    resource_pool->data = (T *)memory;
    resource_pool->generations = (U32 *)(memory + sizeof(T) * capacity);

    for (U32 slot_index = 0; slot_index < capacity - 1; slot_index++)
    {
        Resource_Pool_Node *node = (Resource_Pool_Node *)&resource_pool->data[slot_index];
        node->next = slot_index + 1;
    }
    Resource_Pool_Node *last_node = (Resource_Pool_Node *)&resource_pool->data[capacity - 1];
    last_node->next = -1;
    resource_pool->first_free_node_index = 0;

    for (U32 generation_index = 0; generation_index < capacity; generation_index++)
    {
        resource_pool->generations[generation_index] = 1;
    }

    resource_pool->capacity = capacity;
    resource_pool->count = 0;
    resource_pool->allocator = allocator;
}

template< typename T >
void deinit(Resource_Pool< T > *resource_pool)
{
    HE_ASSERT(resource_pool);

    std::visit([&](auto &&allocator)
    {
        deallocate(allocator, resource_pool->memory);
    }, resource_pool->allocator);
}

template< typename T >
HE_FORCE_INLINE bool is_valid_handle(Resource_Pool< T > *resource_pool, Resource_Handle< T > handle)
{
    HE_ASSERT(resource_pool);
    HE_ASSERT(handle.index < resource_pool->capacity);
    return resource_pool->generations[handle.index] == handle.generation;
}

template< typename T >
Resource_Handle< T > aquire_handle(Resource_Pool< T > *resource_pool)
{
    HE_ASSERT(resource_pool);
    HE_ASSERT(resource_pool->count < resource_pool->capacity);
    HE_ASSERT(resource_pool->first_free_node_index >= 0 && (U32)resource_pool->first_free_node_index < resource_pool->capacity);

    U32 index = resource_pool->first_free_node_index;
    Resource_Pool_Node *node = (Resource_Pool_Node *)&resource_pool->data[index];
    resource_pool->first_free_node_index = node->next;
    resource_pool->count++;
    return { index, resource_pool->generations[index] };
}

template< typename T >
T& get(Resource_Pool< T > *resource_pool, Resource_Handle< T > handle)
{
    HE_ASSERT(resource_pool);
    HE_ASSERT(is_valid_handle(resource_pool, handle));
    return resource_pool->data[handle.index];
}

template< typename T >
void release_handle(Resource_Pool< T > *resource_pool, Resource_Handle< T > handle)
{
    HE_ASSERT(resource_pool);
    HE_ASSERT(resource_pool->count);
    HE_ASSERT(is_valid_handle(resource_pool, handle));

    Resource_Pool_Node *node = (Resource_Pool_Node *)&resource_pool->data[handle.index];
    node->next = resource_pool->first_free_node_index;
    resource_pool->first_free_node_index = handle.index;
    resource_pool->generations[handle.index]++;
    resource_pool->count--;
}