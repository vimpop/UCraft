#include "storage.h"
#include "log.h"
#include "socketio.h"
#include "s2c.h"
#include <stdio.h>

static storage_t *storageHead = NULL;

static storage_t *get_storage_entry(player_t *currentPlayer)
{
    if (currentPlayer == NULL)
    {
        return NULL;
    }
    storage_t *storage = storageHead;
    while (storage != NULL)
    {
        if (sizeof(((player_t *)0)->name) != sizeof(((storage_t *)0)->name))
        {
            // this should be compile time error but whatever
            printl(LOG_ERROR, "name size(s) do not match! - fix this asap\n");
            return NULL;
        }
        if (strncmp(currentPlayer->name, storage->name, sizeof(((storage_t *)0)->name)) == 0)
        {
            return storage;
        }
        storage = storage->next;
    }
    return NULL;
}
static storage_t *get_storage(player_t *currentPlayer)
{
    if (currentPlayer == NULL)
    {
        return NULL;
    }
    storage_t *storage = get_storage_entry(currentPlayer);
    if (storage != NULL)
    {
        return storage;
    }
    storage = U_calloc(1, sizeof(storage_t));
    if (storage == NULL)
    {
        return NULL;
    }
    memcpy(storage->name, currentPlayer->name, sizeof(((player_t *)0)->name));
    storage->next = storageHead;
    storageHead = storage;
    return storage;
}

void storageInventoryUpdateSlot(player_t *currentPlayer, int16_t slot, int16_t count, int32_t item_id)
{
    if (currentPlayer == NULL)
    {
        return;
    }
    storage_t *storage = get_storage(currentPlayer);
    if (storage == NULL)
    {
        return;
    }

    if (slot >= 0 && slot < INVENTORY_SIZE)
    {
        // player updated the crafting fields
        if (slot >= 1 && slot <= 4)
        {
            currentPlayer->gamePlayerData.inventory_crafting_event = 1;
        }
        if (count > 0)
        {
            storage->inventory_slots[slot].count = count;
            storage->inventory_slots[slot].item_id = item_id;
        }
        else
        {
            storage->inventory_slots[slot].item_id = 0;
            storage->inventory_slots[slot].count = 0;
        }
    }
}
void storageInventoryGetSlot(player_t *currentPlayer, int16_t slot, inventory_slots_t *selected_slot)
{
    if (currentPlayer == NULL)
    {
        return;
    }
    if (selected_slot == NULL)
    {
        return;
    }
    selected_slot->count = 0;
    selected_slot->item_id = 0;
    storage_t *storage = get_storage(currentPlayer);
    if (storage == NULL)
    {
        return;
    }

    if (slot >= 0 && slot < INVENTORY_SIZE)
    {
        selected_slot->count = storage->inventory_slots[slot].count;
        selected_slot->item_id = storage->inventory_slots[slot].item_id;
        return;
    }
    return;
}

void storageInventoryInsertItem(player_t *currentPlayer, int32_t item_id, int16_t count)
{
    if (currentPlayer == NULL)
    {
        return;
    }
    if (count <= 0 || item_id == 0)
    {
        // nothing to insert
        return;
    }
    storage_t *storage = get_storage(currentPlayer);
    // try storing it for the slots below
    for (int i = 36; i < 44; i++)
    {
        if (storage->inventory_slots[i].item_id == item_id)
        {
            // full stack
            if (storage->inventory_slots[i].count >= 64)
            {
                continue;
            }
            storage->inventory_slots[i].count += count;
            return;
        }
        if (storage->inventory_slots[i].item_id == 0)
        {
            storage->inventory_slots[i].item_id = item_id;
            storage->inventory_slots[i].count = count;
            return;
        }
    }
    // try the remaining slots
    for (int i = 9; i < 35; i++)
    {
        if (storage->inventory_slots[i].item_id == item_id)
        {
            // full stack
            if (storage->inventory_slots[i].count >= 64)
            {
                continue;
            }
            storage->inventory_slots[i].count += count;
            return;
        }
        if (storage->inventory_slots[i].item_id == 0)
        {
            storage->inventory_slots[i].item_id = item_id;
            storage->inventory_slots[i].count = count;
            return;
        }
    }
    // well i dont know what to do in this case.
    // TODO: implement the case when inventory is full
}

void storageInventoryUpdate(player_t *currentPlayer)
{
    if (currentPlayer == NULL)
    {
        return;
    }
    storage_t *storage = get_storage(currentPlayer);
    // crafting entries cannot be there
    for (int i = 1; i <= 4; i++)
    {
        storageInventoryInsertItem(currentPlayer, storage->inventory_slots[i].item_id, storage->inventory_slots[i].count);
        storage->inventory_slots[i].item_id = 0;
        storage->inventory_slots[i].count = 0;
    }
    PlayS2Ccontainersetcontent(currentPlayer, storage);
}

storage_t *storageInventoryGet(player_t *currentPlayer)
{
    if (currentPlayer == NULL)
    {
        return NULL;
    }
    return get_storage(currentPlayer);
}
void storageInventoryCleanup()
{
    storage_t *storage = storageHead;
    while (storage != NULL)
    {
        storage_t *tmp = storage->next;
        U_free(storage);
        storage = tmp;
    }
    storageHead = NULL;
}
