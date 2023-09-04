#pragma once

#include "core/defines.h"

template< typename T >
struct Ring_Queue
{
    T *data;
    U32 capacity;
    U32 mask;
    U32 write;
    U32 read;
};

template< typename T, typename Allocator >
void init_ring_queue(Ring_Queue< T > *queue, U32 capacity, Allocator *allocator)
{
    HOPE_Assert((capacity & (capacity - 1)) == 0);
    queue->data = AllocateArray(allocator, T, capacity);
    queue->capacity = capacity;
    queue->mask = capacity - 1;
    queue->write = 0;
    queue->read = 0;
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
bool push(Ring_Queue< T > *queue, const T &item)
{
    if (full(queue))
    {
        return false;
    }

    queue->data[(queue->write & queue->mask)] = item;
    queue->write++;
    return true;
}

template< typename T >
bool peek_front(Ring_Queue< T > *queue, T *out_datum)
{
    HOPE_Assert(out_datum);
    if (empty(queue))
    {
        return false;
    }
    U32 read = (queue->read & queue->mask);
    *out_datum = queue->data[read];
    return true;
}

template < typename T >
bool peek_back(Ring_Queue< T > *queue, T *out_datum)
{

    HOPE_Assert(out_datum);
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
    HOPE_Assert(!empty(queue));
    queue->read++;
}

template< typename T >
void pop_back(Ring_Queue< T > *queue)
{
    HOPE_Assert(!empty(queue));
    queue->write--;
}