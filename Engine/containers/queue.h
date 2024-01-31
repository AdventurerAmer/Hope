#pragma once

#include "core/defines.h"
#include "core/memory.h"

template< typename T, typename Allocator >
struct Ring_Queue
{
    T *data;
    U32 capacity;
    U32 mask;
    U32 write;
    U32 read;
    Allocator *allocator;
};

template< typename T, typename Allocator >
void init(Ring_Queue< T, Allocator > *queue, U32 capacity, Allocator *allocator)
{
    HE_ASSERT((capacity & (capacity - 1)) == 0);
    queue->data = HE_ALLOCATE_ARRAY(allocator, T, capacity);
    queue->capacity = capacity;
    queue->mask = capacity - 1;
    queue->write = 0;
    queue->read = 0;
    queue->allocator = allocator;
}

template< typename T, typename Allocator >
void deinit(Ring_Queue< T, Allocator > *queue)
{
    deallocate(queue->allocator, queue->data);
}

template< typename T, typename Allocator >
U32 count(Ring_Queue< T, Allocator > *queue)
{
    return queue->write - queue->read;
}

template< typename T, typename Allocator >
bool empty(Ring_Queue< T, Allocator > *queue)
{
    return count(queue) == 0;
}

template< typename T, typename Allocator >
bool full(Ring_Queue< T, Allocator > *queue)
{
    return count(queue) == queue->capacity;
}

template< typename T, typename Allocator >
bool push(Ring_Queue< T, Allocator > *queue, const T &item)
{
    if (full(queue))
    {
        return false;
    }

    queue->data[(queue->write & queue->mask)] = item;
    queue->write++;
    return true;
}

template< typename T, typename Allocator >
bool peek_front(Ring_Queue< T, Allocator > *queue, T *out_datum)
{
    HE_ASSERT(out_datum);
    if (empty(queue))
    {
        return false;
    }
    U32 read = (queue->read & queue->mask);
    *out_datum = queue->data[read];
    return true;
}

template < typename T, typename Allocator >
bool peek_back(Ring_Queue< T, Allocator > *queue, T *out_datum)
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

template< typename T, typename Allocator >
void pop_front(Ring_Queue< T, Allocator > *queue)
{
    HE_ASSERT(!empty(queue));
    queue->read++;
}

template< typename T, typename Allocator >
void pop_back(Ring_Queue< T, Allocator > *queue)
{
    HE_ASSERT(!empty(queue));
    queue->write--;
}