#include "blocks.h"
#include "wrapper.h"
#include "world.h"
#include "log.h"

#define COORDMAP_INITIAL_CAPACITY 16
#define COORDMAP_MAX_LOAD_PERCENT 70

typedef enum
{
    SLOT_EMPTY,
    SLOT_OCCUPIED,
    SLOT_DELETED
} SlotState;

typedef struct
{
    uint64_t key;
    Blocks *blocks;
    SlotState state;
} HashEntry;

typedef struct
{
    HashEntry *entries;
    size_t capacity;
    size_t count;
} CoordMap;

static CoordMap coordinate_map;

static uint64_t pack_coord(int32_t x, int32_t z)
{
    return ((uint64_t)(uint32_t)x << 32) | (uint32_t)z;
}

static uint64_t hash_u64(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static bool coordmap_init()
{
    coordinate_map.capacity = COORDMAP_INITIAL_CAPACITY;
    coordinate_map.count = 0;

    coordinate_map.entries = U_calloc(coordinate_map.capacity, sizeof(HashEntry));
    if (!coordinate_map.entries)
    {
        coordinate_map.capacity = 0;
        return false;
    }
    return true;
}

static void coordmap_free()
{
    U_free(coordinate_map.entries);

    coordinate_map.entries = NULL;
    coordinate_map.capacity = 0;
    coordinate_map.count = 0;
}

static bool coordmap_insert_raw(uint64_t key, Blocks *blocks)
{
    if (!coordinate_map.capacity) return 0;
    size_t slot = hash_u64(key) % coordinate_map.capacity;
    size_t first_deleted = SIZE_MAX;

    for (size_t probes = 0; probes < coordinate_map.capacity; probes++)
    {
        HashEntry *entry = &coordinate_map.entries[slot];

        if (entry->state == SLOT_EMPTY)
        {
            size_t target = first_deleted != SIZE_MAX ? first_deleted : slot;

            coordinate_map.entries[target].key = key;
            coordinate_map.entries[target].blocks = blocks;
            coordinate_map.entries[target].state = SLOT_OCCUPIED;
            coordinate_map.count++;

            return true;
        }

        if (entry->state == SLOT_DELETED)
        {
            if (first_deleted == SIZE_MAX)
            {
                first_deleted = slot;
            }
        }
        else if (entry->key == key)
        {
            entry->blocks = blocks;
            return true;
        }

        slot = (slot + 1) % coordinate_map.capacity;
    }
    if (first_deleted != SIZE_MAX)
    {
        coordinate_map.entries[first_deleted] = (HashEntry){key, blocks, SLOT_OCCUPIED};
        coordinate_map.count++;
        return true;
    }
    return false;
}

static bool coordmap_grow()
{
    HashEntry *old_entries = coordinate_map.entries;
    size_t old_capacity = coordinate_map.capacity;

    size_t new_capacity = old_capacity * 2;

    HashEntry *new_entries = U_calloc(new_capacity, sizeof(HashEntry));
    if (!new_entries)
    {
        return false;
    }

    coordinate_map.entries = new_entries;
    coordinate_map.capacity = new_capacity;
    coordinate_map.count = 0;

    for (size_t i = 0; i < coordinate_map.capacity; i++)
    {
        coordinate_map.entries[i].key = 0;
        coordinate_map.entries[i].blocks = NULL;
        coordinate_map.entries[i].state = SLOT_EMPTY;
    }

    for (size_t i = 0; i < old_capacity; i++)
    {
        if (old_entries[i].state == SLOT_OCCUPIED)
        {
            coordmap_insert_raw(
                old_entries[i].key,
                old_entries[i].blocks);
        }
    }

    U_free(old_entries);
    return true;
}

static bool coordmap_insert(int32_t x, int32_t z, Blocks *blocks)
{
    if (!coordinate_map.capacity && !coordmap_init()) return false;
    uint64_t key = pack_coord(x, z);

    if ((coordinate_map.count + 1) * 100 / coordinate_map.capacity > COORDMAP_MAX_LOAD_PERCENT)
    {
        if (!coordmap_grow())
        {
            return false;
        }
    }

    return coordmap_insert_raw(key, blocks);
}

static Blocks *coordmap_get(int32_t x, int32_t z)
{
    uint64_t key = pack_coord(x, z);
    if (!coordinate_map.capacity) return 0;
    size_t slot = hash_u64(key) % coordinate_map.capacity;

    for (size_t probes = 0; probes < coordinate_map.capacity; probes++)
    {
        HashEntry *entry = &coordinate_map.entries[slot];

        if (entry->state == SLOT_EMPTY)
        {
            return NULL;
        }
        if (entry->state == SLOT_OCCUPIED && entry->key == key)
        {
            return entry->blocks;
        }
        slot = (slot + 1) % coordinate_map.capacity;
    }
    return NULL;
}

static bool coordmap_remove(int32_t x, int32_t z)
{
    uint64_t key = pack_coord(x, z);
    if (!coordinate_map.capacity) return 0;
    size_t slot = hash_u64(key) % coordinate_map.capacity;

    for (size_t probes = 0; probes < coordinate_map.capacity; probes++)
    {
        HashEntry *entry = &coordinate_map.entries[slot];

        if (entry->state == SLOT_EMPTY)
        {
            return false;
        }
        if (entry->state == SLOT_OCCUPIED && entry->key == key)
        {
            entry->state = SLOT_DELETED;
            entry->blocks = NULL;
            coordinate_map.count--;
            return true;
        }

        slot = (slot + 1) % coordinate_map.capacity;
    }
    return false;
}

void blocksInit()
{

    coordmap_init();
}

void blocksCleanup()
{
    for (size_t i = 0; i < coordinate_map.capacity; i++)
    {
        if (coordinate_map.entries[i].blocks != NULL)
        {
            if (coordinate_map.entries[i].blocks->block != NULL)
            {
                U_free(coordinate_map.entries[i].blocks->block);
            }
            U_free(coordinate_map.entries[i].blocks);
        }
    }
    coordmap_free();
}

static Block *get_block(Blocks *blocks, uint8_t x, int16_t y, uint8_t z, size_t *block_index)
{
    uint32_t coord_hash = (uint32_t)x | ((uint32_t)z << 8) | ((uint32_t)(uint16_t)y << 16);

    if (blocks == NULL)
    {
        return NULL;
    }
    // find the block
    for (size_t i = 0; i < blocks->count; i++)
    {
        if (coord_hash == blocks->block[i].coord_hash)
        {
            if (block_index != NULL)
            {
                *block_index = i;
            }
            return &blocks->block[i];
        }
    }
    return NULL;
}
bool blocksUpdate(blocksDefaultState state, int32_t x, int16_t y, int32_t z)
{
    int32_t chunk_x = x >> 4;
    int32_t chunk_z = z >> 4;
    uint8_t local_x = (uint8_t)((uint32_t)x & 15u);
    uint8_t local_z = (uint8_t)((uint32_t)z & 15u);
    uint32_t terrain_state = worldGetBlock(x, y, z);

    Blocks *blocks = coordmap_get(chunk_x, chunk_z);
    if (blocks == NULL)
    {
        if (terrain_state == state)
        {
            return true;
        }
        blocks = U_calloc(1, sizeof(Blocks));
        if (blocks == NULL)
        {
            return false;
        }
        blocks->block = U_calloc(BLOCKS_INITAL_CAPACITY, sizeof(Block));
        if (!blocks->block) { U_free(blocks); return false; }
        blocks->size = BLOCKS_INITAL_CAPACITY;
        if (!coordmap_insert(chunk_x, chunk_z, blocks))
        {
            U_free(blocks->block);
            U_free(blocks);
            return false;
        }
    }
    size_t block_index = 0;
    Block *block = get_block(blocks, local_x, y, local_z, &block_index);
    if (block != NULL)
    {
        // block needs to be removed from this list
        if (terrain_state == state)
        {
            size_t move_count = blocks->count - block_index - 1;

            if (move_count > 0)
            {
                memmove(&blocks->block[block_index],
                        &blocks->block[block_index + 1],
                        move_count * sizeof(Block));
            }

            blocks->count--;
            // free the memory
            if (blocks->count == 0)
            {
                U_free(blocks->block);
                U_free(blocks);
                coordmap_remove(chunk_x, chunk_z);
            }
            return true;
        }
        else
        {
            block->default_state = state;
            return true;
        }
        return false;
    }
    // block list is full
    if (blocks->count >= blocks->size)
    {
        Block *new_block = NULL;
        size_t new_size = blocks->size + BLOCKS_INITAL_CAPACITY;
        new_block = U_realloc(blocks->block, sizeof(Block) * new_size);
        if (new_block == NULL)
        {
            return false;
        }
        blocks->block = new_block;
        blocks->size = new_size;
    }
    blocks->block[blocks->count].default_state = state;
    blocks->block[blocks->count].c.x = local_x;
    blocks->block[blocks->count].c.y = y;
    blocks->block[blocks->count].c.z = local_z;
    blocks->count++;
    return true;
}

Blocks *blocksGet(int32_t chunk_x, int32_t chunk_z)
{
    Blocks *blocks = coordmap_get(chunk_x, chunk_z);
    if (blocks == NULL)
    {
        return NULL;
    }

    return blocks;
}

int32_t blocksGetBlock(int32_t x, int16_t y, int32_t z)
{
    int32_t chunk_x = x >> 4;
    int32_t chunk_z = z >> 4;
    uint8_t local_x = (uint8_t)((uint32_t)x & 15u);
    uint8_t local_z = (uint8_t)((uint32_t)z & 15u);
    Blocks *blocks = blocksGet(chunk_x, chunk_z);
    if (blocks != NULL)
    {
        size_t block_index = 0;
        Block *block = get_block(blocks, local_x, y, local_z, &block_index);
        if (block != NULL)
        {
            return (block->default_state);
        }
    }
    // maybe its a terrain block
    return worldGetBlock(x, y, z);
}
