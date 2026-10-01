#include <math.h>
#include <stdarg.h>

#include "game.h"
#include "log.h"
#include "s2c.h"
#include "wrapper.h"
#include "blocks.h"
#include "blocks/items.h"
#include "UCraft.h"
#include "recipes.h"

static game_data_t gameData;

// fired when the server is about to start
void gamePreload()
{
    blocksInit();
    memset(&gameData, 0, sizeof(game_data_t));
    gameData.time = 24000 / 4; // noon
}

void gameCleanup()
{
    storageInventoryCleanup();
    blocksCleanup();
}
// fired when a player leaves
void gamePlayerLeft(player_t *currentPlayer)
{
    if (currentPlayer->gamePlayerData.crafting_menu)
    {
        U_free(currentPlayer->gamePlayerData.crafting_menu);
        currentPlayer->gamePlayerData.crafting_menu = NULL;
    }
}
// fired every tick in the global context
// NOTE: this will run even if there are no players so be careful when sending packets
void gameGlobalTick()
{

    if ((main_tick % ((1000 / TICK_TIME_MS) * 10)) == 0) // update the time on the client after every 10 seconds
    {
        if (playerGetActiveCount() > 0)
        {
            PlayS2Csettime(gameData.time, 0);
        }
    }
    if ((main_tick % ((1000 / TICK_TIME_MS) * 1)) == 0) // should be every second
    {
        gameData.time = (gameData.time + 20) % 24000;
    }
}
// fired every tick in the global context for the current player {so it will replicate for others}
void gamePlayerGlobalTick(player_t *currentPlayer)
{
}

//  fired every tick in the global context for the current player but will announce it to the other players only and ignore the current client
void gamePlayerGlobalTickOthers(player_t *currentPlayer)
{
    if (currentPlayer->gamePlayerData.block_update_event)
    {
        PlayS2Cblock(currentPlayer->gamePlayerData.block_state, currentPlayer->gamePlayerData.block_x, currentPlayer->gamePlayerData.block_y, currentPlayer->gamePlayerData.block_z);
        currentPlayer->gamePlayerData.block_update_event = 0;
    }
}

// fired when the player is spawned in the local context {fired once}
void gamePlayerSpawned(player_t *currentPlayer)
{
    // Local chunk
    currentPlayer->gamePlayerData.chunk_lx = -VIEWDISTANCE;
    currentPlayer->gamePlayerData.chunk_lz = -VIEWDISTANCE;

    currentPlayer->gamePlayerData.chunk_spawn_event = 1;
    currentPlayer->gamePlayerData.chunk_load_event = 1;

    // Restore the inventory
    storageInventoryUpdate(currentPlayer);
    // Update Time
    PlayS2Csettime(gameData.time, 0);
}

// fired in local context
void gamePlayerLocalTick(player_t *currentPlayer)
{
    // chunk generation stuff
    if (currentPlayer->gamePlayerData.chunk_load_event)
    {
        if (currentPlayer->chunk_px != 0 || currentPlayer->chunk_pz != 0)
        {
            // send only the new edge column/row.
            if (currentPlayer->chunk_px != 0)
            {
                int32_t chunk_x = currentPlayer->gamePlayerData.chunk_x + (currentPlayer->chunk_px * VIEWDISTANCE);
                int32_t chunk_z = currentPlayer->gamePlayerData.chunk_z + currentPlayer->gamePlayerData.chunk_lz;
                PlayS2Cchunk(currentPlayer, chunk_x, chunk_z, 3, 7);
                if (currentPlayer->gamePlayerData.chunk_lz < VIEWDISTANCE)
                {
                    currentPlayer->gamePlayerData.chunk_lz++;
                }
                else
                {
                    currentPlayer->gamePlayerData.chunk_lz = -VIEWDISTANCE;
                    currentPlayer->chunk_px = 0;
                }
            }
            else
            {
                int32_t chunk_x = currentPlayer->gamePlayerData.chunk_x + currentPlayer->gamePlayerData.chunk_lx;
                int32_t chunk_z = currentPlayer->gamePlayerData.chunk_z + (currentPlayer->chunk_pz * VIEWDISTANCE);
                PlayS2Cchunk(currentPlayer, chunk_x, chunk_z, 3, 7);
                if (currentPlayer->gamePlayerData.chunk_lx < VIEWDISTANCE)
                {
                    currentPlayer->gamePlayerData.chunk_lx++;
                }
                else
                {
                    currentPlayer->gamePlayerData.chunk_lx = -VIEWDISTANCE;
                    currentPlayer->chunk_pz = 0;
                }
            }
            if (currentPlayer->chunk_px == 0 && currentPlayer->chunk_pz == 0)
            {
                currentPlayer->gamePlayerData.chunk_load_event = 0;
            }
        }
        else
        {
            // full square load
            PlayS2Cchunk(currentPlayer, currentPlayer->gamePlayerData.chunk_lx + currentPlayer->gamePlayerData.chunk_x,
                         currentPlayer->gamePlayerData.chunk_lz + currentPlayer->gamePlayerData.chunk_z,
                         3, 7);
            if (currentPlayer->gamePlayerData.chunk_lx < VIEWDISTANCE)
            {
                currentPlayer->gamePlayerData.chunk_lx++;
            }
            else
            {
                currentPlayer->gamePlayerData.chunk_lx = -VIEWDISTANCE;
                currentPlayer->gamePlayerData.chunk_lz++;
            }
            if (currentPlayer->gamePlayerData.chunk_lz > VIEWDISTANCE)
            {
                // close the 'Loading terrain' screen and set the coordinates of the player
                if (currentPlayer->gamePlayerData.chunk_spawn_event)
                {
                    PlayS2Cpositionrotation(currentPlayer, SPAWN_X, SPAWN_Y, SPAWN_Z);
                    PlayS2Ccompassposition(currentPlayer, SPAWN_X, SPAWN_Y, SPAWN_Z);
                    PlayS2Cgameevent(EVENT_WAIT_LEVEL_CHUNKS, 1.0f);
                    PlayS2Cchunkcenter(currentPlayer, currentPlayer->gamePlayerData.chunk_x, currentPlayer->gamePlayerData.chunk_z);

                    currentPlayer->gamePlayerData.chunk_spawn_event = 0;
                }
                currentPlayer->gamePlayerData.chunk_load_event = 0;
            }
        }
    }
    else
    {
        // check if the conditions for a chunk update are met
        int32_t dx = currentPlayer->chunk_x - currentPlayer->gamePlayerData.chunk_x;
        int32_t dz = currentPlayer->chunk_z - currentPlayer->gamePlayerData.chunk_z;

        if (dx != 0 || dz != 0)
        {
            PlayS2Cchunkcenter(currentPlayer, currentPlayer->chunk_x, currentPlayer->chunk_z);
            currentPlayer->gamePlayerData.chunk_x = currentPlayer->chunk_x;
            currentPlayer->gamePlayerData.chunk_z = currentPlayer->chunk_z;
            currentPlayer->gamePlayerData.chunk_lx = -VIEWDISTANCE;
            currentPlayer->gamePlayerData.chunk_lz = -VIEWDISTANCE;

            if (dx > 1 || dx < -1 || dz > 1 || dz < -1)
            {
                // moved too far
                currentPlayer->chunk_px = 0;
                currentPlayer->chunk_pz = 0;
            }
            else
            {
                // moved one chunk: load only the new border(s)
                currentPlayer->chunk_px = (dx > 0) ? 1 : (dx < 0) ? -1
                                                                  : 0;
                currentPlayer->chunk_pz = (dz > 0) ? 1 : (dz < 0) ? -1
                                                                  : 0;
            }
            currentPlayer->gamePlayerData.chunk_load_event = 1;
        }
    }
    // inventory update
    if (currentPlayer->gamePlayerData.full_inventory_update_event)
    {
        storageInventoryUpdate(currentPlayer);
        currentPlayer->gamePlayerData.full_inventory_update_event = 0;
    }
    // crafting table event
    if (currentPlayer->gamePlayerData.crafting_table_event)
    {
        if (currentPlayer->gamePlayerData.crafting_menu)
        {
            inventory_slots_t slots[9];
            memset(&slots, 0, sizeof(slots));
            for (int i = 1; i <= 9; i++)
            {
                if (currentPlayer->gamePlayerData.crafting_menu[i].item_id)
                {
                    slots[i - 1].item_id = currentPlayer->gamePlayerData.crafting_menu[i].item_id;
                    slots[i - 1].count = currentPlayer->gamePlayerData.crafting_menu[i].count;
                }
            }
            size_t count = 0;
            int32_t crafted_item = get_crafted_item(currentPlayer, slots, &count);
            PlayS2Ccontainersetslot(currentPlayer, 1, 0, count, crafted_item);
        }
        currentPlayer->gamePlayerData.crafting_table_event = 0;
    }
    // inventory crafting event
    if (currentPlayer->gamePlayerData.inventory_crafting_event)
    {
        inventory_slots_t slots[9];
        memset(&slots, 0, sizeof(slots));
        size_t index = 0;
        for (int i = 1; i < 5; i++)
        {
            inventory_slots_t slot;
            storageInventoryGetSlot(currentPlayer, i, &slot);
            if (slot.item_id)
            {
                slots[index].count = slot.count;
                slots[index].item_id = slot.item_id;
            }
            if (i > 1 && i < 3)
            {
                index += 2;
            }
            else
            {
                index++;
            }
        }
        size_t count = 0;
        int32_t crafted_item = get_crafted_item(currentPlayer, slots, &count);
        PlayS2Ccontainersetslot(currentPlayer, 0, 0, count, crafted_item);
        currentPlayer->gamePlayerData.inventory_crafting_event = 0;
    }
    // blocks
    if (currentPlayer->gamePlayerData.action_item_event)
    {
        inventory_slots_t slot;
        storageInventoryGetSlot(currentPlayer, 36 + currentPlayer->gamePlayerData.inventory_slot, &slot);
        int32_t block = blocksGetBlock(currentPlayer->gamePlayerData.block_x, currentPlayer->gamePlayerData.block_y, currentPlayer->gamePlayerData.block_z);
        if (currentPlayer->gamePlayerData.action_item_event == 2) // block placing event
        {

            // 3x3 crafitng state
            if (block == MINECRAFT_CRAFTING_TABLE && currentPlayer->sneaking != 1)
            {
                if (currentPlayer->gamePlayerData.crafting_menu == NULL)
                {
                    currentPlayer->gamePlayerData.crafting_menu = U_calloc(INVENTORY_SIZE, sizeof(inventory_slots_t));
                    storage_t *inventory = storageInventoryGet(currentPlayer);
                    // copy the inventory according to the crafting table mapping
                    for (int i = 10; i < INVENTORY_SIZE; i++)
                    {
                        currentPlayer->gamePlayerData.crafting_menu[i].item_id = inventory->inventory_slots[i - 1].item_id;
                        currentPlayer->gamePlayerData.crafting_menu[i].count = inventory->inventory_slots[i - 1].count;
                    }
                }

                PlayS2Copenscreen(currentPlayer, 1, WINDOW_CRAFTING, "Crafting");
            }
            else
            {
                currentPlayer->gamePlayerData.block_state = minecraft_block_state_from_item(slot.item_id);
                if (currentPlayer->gamePlayerData.block_state && slot.count > 0)
                {
                    if (slot.count)
                    {
                        slot.count--;
                        storageInventoryUpdateSlot(currentPlayer, 36 + currentPlayer->gamePlayerData.inventory_slot, slot.count, slot.item_id);
                        storageInventoryUpdate(currentPlayer);
                    }
                    switch (currentPlayer->gamePlayerData.block_face)
                    {
                    case 0:
                        currentPlayer->gamePlayerData.block_y--;
                        break;

                    case 1:
                        currentPlayer->gamePlayerData.block_y++;
                        break;

                    case 2:
                        currentPlayer->gamePlayerData.block_z--;
                        break;

                    case 3:
                        currentPlayer->gamePlayerData.block_z++;
                        break;

                    case 4:
                        currentPlayer->gamePlayerData.block_x--;
                        break;

                    case 5:
                        currentPlayer->gamePlayerData.block_x++;
                        break;
                    default:
                        break;
                    }
                    blocksUpdate(currentPlayer->gamePlayerData.block_state, currentPlayer->gamePlayerData.block_x, currentPlayer->gamePlayerData.block_y, currentPlayer->gamePlayerData.block_z);
                    PlayS2Cblock(currentPlayer->gamePlayerData.block_state, currentPlayer->gamePlayerData.block_x, currentPlayer->gamePlayerData.block_y, currentPlayer->gamePlayerData.block_z);
                    PlayS2Cblockchangeack(currentPlayer, currentPlayer->gamePlayerData.block_sequence);
                    currentPlayer->gamePlayerData.block_update_event = 1;
                }
            }
        }
        else if (currentPlayer->gamePlayerData.action_item_event == 1) // block breaking
        {
            int32_t item_broken = minecraft_item_from_block_state(block);
            // verify the entry
            static const int32_t tools[] = {MINECRAFT_WOODEN_PICKAXE_ITEM, MINECRAFT_STONE_PICKAXE_ITEM, MINECRAFT_IRON_PICKAXE_ITEM, MINECRAFT_DIAMOND_PICKAXE_ITEM};
            switch (item_broken)
            {
            case MINECRAFT_GRASS_BLOCK_ITEM:
                item_broken = MINECRAFT_DIRT_ITEM;
                break;
            case MINECRAFT_STONE:
            {
                item_broken = MINECRAFT_AIR_ITEM;
                if (slot.count)
                {

                    for (size_t i = 0; i < (size_t)(sizeof(tools) / sizeof(int32_t)); i++)
                    {
                        if (slot.item_id == tools[i])
                        {
                            item_broken = MINECRAFT_COBBLESTONE_ITEM;
                            break;
                        }
                    }
                }
                break;
            }
            case MINECRAFT_COAL_ORE_ITEM:
                item_broken = MINECRAFT_AIR_ITEM;
                if (slot.count)
                {

                    for (size_t i = 0; i < (size_t)(sizeof(tools) / sizeof(int32_t)); i++)
                    {
                        if (slot.item_id == tools[i])
                        {
                            item_broken = MINECRAFT_COAL_ITEM;
                            break;
                        }
                    }
                }
                break;
            case MINECRAFT_IRON_ORE_ITEM:
                item_broken = MINECRAFT_AIR_ITEM;
                if (slot.count)
                {

                    for (size_t i = 0; i < (size_t)(sizeof(tools) / sizeof(int32_t)); i++)
                    {
                        if (tools[i] == MINECRAFT_WOODEN_PICKAXE_ITEM)
                        {
                            continue;
                        }
                        if (slot.item_id == tools[i])
                        {
                            item_broken = MINECRAFT_RAW_IRON_ITEM;
                            break;
                        }
                    }
                }
                break;
            case MINECRAFT_DIAMOND_ORE_ITEM:
                item_broken = MINECRAFT_AIR_ITEM;
                if (slot.count)
                {

                    for (size_t i = 0; i < (size_t)(sizeof(tools) / sizeof(int32_t)); i++)
                    {
                        if (tools[i] == MINECRAFT_WOODEN_PICKAXE_ITEM)
                        {
                            continue;
                        }
                        if (tools[i] == MINECRAFT_STONE_PICKAXE_ITEM)
                        {
                            continue;
                        }
                        if (slot.item_id == tools[i])
                        {
                            item_broken = MINECRAFT_DIAMOND_ITEM;
                            break;
                        }
                    }
                }
                break;
            case MINECRAFT_OAK_LEAVES_ITEM:
            {
                // 93% : air;
                // 2%: stick
                // 5%: sapling
                static const uint8_t weight[] = {93, 2, 5};
                static const int32_t choice[] = {MINECRAFT_AIR_ITEM, MINECRAFT_STICK_ITEM, MINECRAFT_OAK_SAPLING_ITEM};
                size_t selection = rand() % 101;
                for (size_t i = 0; i < 3; i++)
                {
                    if (selection < weight[i])
                    {
                        item_broken = choice[i];
                        break;
                    }
                    selection -= weight[i];
                }
                break;
            }
            default:
                break;
            }
            storageInventoryInsertItem(currentPlayer, item_broken, 1);
            storageInventoryUpdate(currentPlayer);
            blocksUpdate(currentPlayer->gamePlayerData.block_state, currentPlayer->gamePlayerData.block_x, currentPlayer->gamePlayerData.block_y, currentPlayer->gamePlayerData.block_z);
            PlayS2Cblock(currentPlayer->gamePlayerData.block_state, currentPlayer->gamePlayerData.block_x, currentPlayer->gamePlayerData.block_y, currentPlayer->gamePlayerData.block_z);
            PlayS2Cblockchangeack(currentPlayer, currentPlayer->gamePlayerData.block_sequence);
            currentPlayer->gamePlayerData.block_update_event = 1;
        }

        currentPlayer->gamePlayerData.action_item_event = 0;
    }
}
