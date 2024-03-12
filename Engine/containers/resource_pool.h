#pragma once

#include "core/defines.h"
#include "core/memory.h"
#include "core/platform.h"

template< typename T >
struct Resource_Handle
{
    S32 index = -1;
    U32 generation = 0;

    bool operator==(const Resource_Handle< T > &other)
    {
        return index == other.index && generation == other.generation;
    }

    bool operator!=(const Resource_Handle< T > &other)
    {
        return index != other.index || generation != other.generation;
    }
};

struct Resource_Pool_Node
{
    S32 next;
};

template< typename T >
struct Resource_Pool
{
    constexpr static Resource_Handle< T > invalid_handle = { -1, 0 };

    static_assert(sizeof(T) >= sizeof(U32));

    void *memory;

    T *data;
    U32 *generations;
    bool *is_allocated;

    S32 first_free_node_index;

    U32 capacity;
    U32 count;

    Allocator allocator;

    Mutex mutex;
};

template< typename T >
void init(Resource_Pool< T > *resource_pool, U32 capacity, Allocator allocator)
{
    HE_ASSERT(resource_pool);
    HE_ASSERT(capacity);

    U64 size = (sizeof(T) + sizeof(U32) + sizeof(bool)) * capacity;
    U8 *memory = HE_ALLOCATOR_ALLOCATE_ARRAY(&allocator, U8, size);
    resource_pool->memory = memory;
    resource_pool->data = (T *)memory;
    resource_pool->generations = (U32 *)(memory + sizeof(T) * capacity);
    resource_pool->is_allocated = (bool *)(memory + (sizeof(T) + sizeof(U32)) * capacity);

    for (U32 slot_index = 0; slot_index < capacity - 1; slot_index++)
    {
        Resource_Pool_Node *node = (Resource_Pool_Node *)&resource_pool->data[slot_index];
        node->next = slot_index + 1;
    }
    Resource_Pool_Node *last_node = (Resource_Pool_Node *)&resource_pool->data[capacity - 1];
    last_node->next = -1;
    resource_pool->first_free_node_index = 0;

    zero_memory(resource_pool->generations, sizeof(U32) * capacity);
    zero_memory(resource_pool->is_allocated, sizeof(bool) * capacity);

    resource_pool->capacity = capacity;
    resource_pool->count = 0;
    resource_pool->allocator = allocator;

    platform_create_mutex(&resource_pool->mutex);
}

template< typename T >
void deinit(Resource_Pool< T > *resource_pool)
{
    HE_ASSERT(resource_pool);
    HE_ALLOCATOR_DEALLOCATE(resource_pool->allocator, resource_pool->memory);
}

template< typename T >
HE_FORCE_INLINE bool is_valid_handle(Resource_Pool< T > *resource_pool, Resource_Handle< T > handle)
{
    HE_ASSERT(resource_pool);
    bool result = handle.index >= 0 && handle.index < (S32)resource_pool->capacity && resource_pool->is_allocated[handle.index] && resource_pool->generations[handle.index] == handle.generation;
    return result;
}

template< typename T >
Resource_Handle< T > aquire_handle(Resource_Pool< T > *resource_pool)
{
    HE_ASSERT(resource_pool);
    HE_ASSERT(resource_pool->count < resource_pool->capacity);
    HE_ASSERT(resource_pool->first_free_node_index >= 0 && (U32)resource_pool->first_free_node_index < resource_pool->capacity);

    platform_lock_mutex(&resource_pool->mutex);

    S32 index = resource_pool->first_free_node_index;
    HE_ASSERT(!resource_pool->is_allocated[index]);

    Resource_Pool_Node *node = (Resource_Pool_Node *)&resource_pool->data[index];
    resource_pool->first_free_node_index = node->next;
    resource_pool->count++;
    resource_pool->is_allocated[index] = true;
    Resource_Handle< T > handle = { index, resource_pool->generations[index] };

    platform_unlock_mutex(&resource_pool->mutex);
    return handle;
}

template< typename T >
T* get(Resource_Pool< T > *resource_pool, Resource_Handle< T > handle)
{
    HE_ASSERT(resource_pool);
    HE_ASSERT(is_valid_handle(resource_pool, handle));
    return &resource_pool->data[handle.index];
}

template< typename T >
void release_handle(Resource_Pool< T > *resource_pool, Resource_Handle< T > handle)
{
    HE_ASSERT(resource_pool);
    HE_ASSERT(resource_pool->count);
    HE_ASSERT(is_valid_handle(resource_pool, handle));

    platform_lock_mutex(&resource_pool->mutex);

    Resource_Pool_Node *node = (Resource_Pool_Node *)&resource_pool->data[handle.index];
    node->next = resource_pool->first_free_node_index;
    resource_pool->first_free_node_index = handle.index;
    resource_pool->generations[handle.index]++;
    resource_pool->is_allocated[handle.index] = false;
    resource_pool->count--;

    platform_unlock_mutex(&resource_pool->mutex);
}

template< typename T >
Resource_Handle< T > iterator(Resource_Pool< T > *resource_pool)
{
    HE_ASSERT(resource_pool);
    return Resource_Pool< T >::invalid_handle;
}

template< typename T >
bool next(Resource_Pool< T > *resource_pool, Resource_Handle< T > &handle)
{
    for (U32 index = handle.index + 1; index < resource_pool->capacity; index++)
    {
        if (resource_pool->is_allocated[index])
        {
            handle.index = (S32)index;
            handle.generation = resource_pool->generations[index];
            return true;
        }
    }

    return false;
}