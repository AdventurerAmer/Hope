#pragma once

#include "core/defines.h"

template< typename T >
struct Queue
{
    T *data;
    U32 capacity;
    U32 mask;
    U32 write;
    U32 read;
};

template< typename T, typename Allocator >
void init_queue(Queue< T > *queue, U32 capacity, Allocator *allocator)
{
    HOPE_Assert((capacity & (capacity - 1)) == 0);
    queue->data = AllocateArray(allocator, T, capacity);
    queue->capacity = capacity;
    queue->mask = capacity - 1;
    queue->write = 0;
    queue->read = 0;
}

template< typename T >
U32 count(Queue< T > *queue)
{
    return (queue->write & queue->mask) - (queue->read & queue->mask);
}

template< typename T >
bool empty(Queue< T > *queue)
{
    return count(queue) == 0;
}

template< typename T >
bool full(Queue< T > *queue)
{
    return count(queue) == queue->capacity;
}

template< typename T >
void push(Queue< T > *queue, const T& item)
{
    HOPE_Assert(!full(queue));
    queue->data[(queue->write & queue->mask)] = item;
    queue->write++;
}

template< typename T >
T pop(Queue< T > *queue)
{
    HOPE_Assert(!empty(queue));
    U32 read = (queue->read & queue->mask);
    queue->read++;
    return queue->data[read];
}