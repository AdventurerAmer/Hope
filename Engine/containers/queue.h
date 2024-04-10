#pragma once

#include "core/defines.h"
#include "core/memory.h"

template< typename T >
struct Ring_Queue
{
    T *data;
    U32 capacity;
    U32 mask;
    U32 write;
    U32 read;
    Allocator allocator;
};

template< typename T >
void init(Ring_Queue< T > *queue, U32 capacity, Allocator allocator)
{
    if ((capacity & (capacity - 1)) != 0)
    {
        U32 new_capacity = 2;
        capacity--;
        while (capacity >>= 1)
        {
            new_capacity <<= 1;
        }
        HE_ASSERT((new_capacity & (new_capacity - 1)) == 0);
        capacity = new_capacity;
    }

    queue->data = HE_ALLOCATOR_ALLOCATE_ARRAY(&allocator, T, capacity);
    queue->capacity = capacity;
    queue->mask = capacity - 1;
    queue->write = 0;
    queue->read = 0;
    queue->allocator = allocator;
}

template< typename T >
void deinit(Ring_Queue< T > *queue)
{
    HE_ALLOCATOR_DEALLOCATE(&queue->allocator, queue->data);
}

template< typename T >
U32 count(Ring_Queue< T > *queue)
{
    return queue->write - queue->read;
}

template< typename T >
bool empty(Ring_Queue< T > *queue)
{
    return count(queue) == 0;
}

template< typename T >
bool full(Ring_Queue< T > *queue)
{
    return count(queue) == queue->capacity;
}

template< typename T >
S32 push(Ring_Queue< T > *queue, const T &item)
{
    if (full(queue))
    {
        return -1;
    }

    U32 index = (queue->write & queue->mask);
    queue->data[index] = item;
    queue->write++;
    return (S32)index;
}

template< typename T >
bool peek_front(Ring_Queue< T > *queue, T *out_datum, U32 *index = nullptr)
{
    HE_ASSERT(out_datum);
    if (empty(queue))
    {
        return false;
    }
    U32 read = (queue->read & queue->mask);
    *out_datum = queue->data[read];
    if (index)
    {
        *index = read;
    }
    return true;
}

template < typename T >
bool peek_back(Ring_Queue< T > *queue, T *out_datum)
{

    HE_ASSERT(out_datum);
    if (empty(queue))
    {
        return false;
    }
    U32 before_write = ((queue->write - 1) & queue->mask);
    *out_datum = queue->data[before_write];
    return true;
}

template< typename T >
void pop_front(Ring_Queue< T > *queue)
{
    HE_ASSERT(!empty(queue));
    queue->read++;
}

template< typename T >
void pop_back(Ring_Queue< T > *queue)
{
    HE_ASSERT(!empty(queue));
    queue->write--;
}