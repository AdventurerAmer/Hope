#pragma once

#include "core/defines.h"
#include "core/memory.h"

enum class Slot_State : U8
{
    EMPTY,
    OCCUPIED,
    DELETED
};

template< typename Key_Type, typename Value_Type >
struct Hash_Map
{
    void *memory;

    Slot_State *states;
    Key_Type *keys;
    Value_Type *values;

    U32 capacity;
    U32 count;

    Allocator allocator;
};

template< typename Key_Type, typename Value_Type >
void init(Hash_Map< Key_Type, Value_Type > *hash_map, Allocator allocator, U32 capacity)
{
    HE_ASSERT(hash_map);
    HE_ASSERT(capacity);

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

    Size total_size = (sizeof(Slot_State) + sizeof(Key_Type) + sizeof(Value_Type)) * capacity;
    U8 *memory = nullptr; 
    
    std::visit([&](auto &&allocator)
    {
        memory = HE_ALLOCATE_ARRAY(allocator, U8, total_size);
    }, allocator);

    hash_map->memory = memory;
    hash_map->states = (Slot_State*)memory;
    zero_memory(hash_map->states, sizeof(Slot_State) * capacity);
    hash_map->keys = (Key_Type*)(memory + sizeof(Slot_State) * capacity);
    hash_map->values = (Value_Type*)(memory + (sizeof(Slot_State) + sizeof(Key_Type)) * capacity);
    hash_map->capacity = capacity;
    hash_map->count = 0;
    hash_map->allocator = allocator;
}

template< typename Key_Type, typename Value_Type >
void deinit(Hash_Map< Key_Type, Value_Type > *hash_map)
{
    std::visit([&](auto &&allocator)
    {
        deallocate(allocator, hash_map->memory);
    }, hash_map->allocator);
}

template< typename Key_Type, typename Value_Type >
S32 find(Hash_Map< Key_Type, Value_Type > *hash_map, const Key_Type &key, S32 *out_insert_index = nullptr)
{
    HE_ASSERT(hash_map);

    U32 slot_index = hash(key) & (hash_map->capacity - 1);
    U32 start_slot = slot_index;
    S32 insert_index = -1;

    do
    {
        Slot_State *state = &hash_map->states[slot_index];

        if (*state == Slot_State::EMPTY)
        {
            if (insert_index == -1)
            {
                insert_index = slot_index;
                break;
            }
        }
        else if (*state == Slot_State::OCCUPIED)
        {
            if (hash_map->keys[slot_index] == key)
            {
                return slot_index;
            }
        }
        else if (*state == Slot_State::DELETED)
        {
            if (insert_index == -1)
            {
                insert_index = slot_index;
            }
        }

        slot_index++;
    }
    while (slot_index != start_slot);

    if (out_insert_index)
    {
        *out_insert_index = insert_index;
    }

    return -1;
}

template< typename Key_Type, typename Value_Type >
void insert(Hash_Map< Key_Type, Value_Type > *hash_map, const Key_Type &key, const Value_Type &value)
{
    HE_ASSERT(hash_map);
    HE_ASSERT(hash_map->count < hash_map->capacity);

    S32 insert_index = -1;
    S32 slot_index = find(hash_map, key, &insert_index);
    if (slot_index != -1)
    {
        hash_map->values[insert_index] = value;
    }
    else
    {
        HE_ASSERT(insert_index != -1);
        hash_map->states[insert_index] = Slot_State::OCCUPIED;
        hash_map->keys[insert_index] = key;
        hash_map->values[insert_index] = value;
        hash_map->count++;
    }
}

template< typename Key_Type, typename Value_Type >
void remove(Hash_Map< Key_Type, Value_Type > *hash_map, const Key_Type &key)
{
    HE_ASSERT(hash_map);
    HE_ASSERT(hash_map->count);

    S32 slot_index = find(hash_map, key);
    if (slot_index != -1)
    {
        hash_map->states[slot_index] = Slot_State::DELETED;
        hash_map->count--;
    }
}