#include "c2s.h"
#include <math.h>
#include <limits.h>
#include "storage.h"
#include "blocks/block_states.h"

static void PlayC2S_accept_teleportation(player_t *currentPlayer) {}
static void PlayC2S_attack(player_t *currentPlayer) {}
static void PlayC2S_block_entity_tag_query(player_t *currentPlayer) {}
static void PlayC2S_bundle_item_selected(player_t *currentPlayer) {}
static void PlayC2S_change_difficulty(player_t *currentPlayer) {}
static void PlayC2S_change_game_mode(player_t *currentPlayer) {}
static void PlayC2S_chat_ack(player_t *currentPlayer) {}
static void PlayC2S_chat_command(player_t *currentPlayer) {}
static void PlayC2S_chat_command_signed(player_t *currentPlayer) {}
static char chat_buffer[300];
uint8_t chat_inuse = 0;
static void PlayC2S_chat(player_t *currentPlayer)
{
    if (!chat_inuse)
    {
        int32_t length = readVarInt();
        if (!readFailed() && length >= 0 && length <= 255)
        {
            memset(chat_buffer, 0, sizeof(chat_buffer));
            // add the decorators, very ugly but trying to avoid using snprintf here
            chat_buffer[0] = '<';
            char *name = strncpy(&chat_buffer[1], currentPlayer->name, sizeof(((player_t *)0)->name));
            char *footer = &name[strnlen(name, sizeof(((player_t *)0)->name))];
            *footer++ = '>';
            *footer++ = ' ';
            for (int i = 0; i < length; i++)
            {
                readBuffer(&footer[i], 1);
                if (footer[i] < ' ')
                {
                    chat_inuse = 0;
                    return;
                }
                if (footer[i] == '\"')
                {
                    chat_inuse = 0;
                    return;
                }
                // remove '_' since its valid
                if ((footer[i] >= '[' && footer[i] <= '`') && footer[i] != '_')
                {
                    chat_inuse = 0;
                    return;
                }
                if (footer[i] >= '{')
                {
                    chat_inuse = 0;
                    return;
                }
                if (footer[i] == 0)
                {
                    i--;
                    break;
                }
            }
            currentPlayer->chat_timestamp = readLong();
            if (readFailed()) return;
            currentPlayer->chat_event = 1;
            currentPlayer->chat_len = strnlen(chat_buffer, sizeof(chat_buffer)); // do not count the null char
            currentPlayer->chat_ptr = chat_buffer;
            chat_inuse = 1;
        }
    }
}
static void PlayC2S_chat_session_update(player_t *currentPlayer) {}
static void PlayC2S_chunk_batch_received(player_t *currentPlayer) {}
static void PlayC2S_client_command(player_t *currentPlayer) {}
static void PlayC2S_client_tick_end(player_t *currentPlayer) {}
static void PlayC2S_client_information(player_t *currentPlayer)
{
    char buf[17];
    readString(buf, sizeof(buf));           // Locale
    readByte();                             // View Distance
    readVarInt();                           // Chat Mode
    readByte();                             // Chat Colors
    currentPlayer->skin_parts = readByte(); // Displayed Skin Parts
    readVarInt();                           // Main Hand
    readByte();                             // Disable Text Filter
    readByte();                             // Allow server listing
    currentPlayer->settings_changed_event = 1;
}
static void PlayC2S_command_suggestion(player_t *currentPlayer) {}
static void PlayC2S_configuration_acknowledged(player_t *currentPlayer) {}
static void PlayC2S_container_button_click(player_t *currentPlayer) {}
static void PlayC2S_container_click(player_t *currentPlayer)
{
    int32_t window_id = readVarInt();
    readVarInt(); // State ID
    readShort(); // Slot
    readByte(); // Button
    readVarInt(); // Mode
    int32_t changed_slots = readVarInt();
    if (readFailed() || changed_slots < 0 || changed_slots > INVENTORY_SIZE ||
        (window_id != 0 && window_id != 1) ||
        (window_id == 1 && !currentPlayer->gamePlayerData.crafting_menu))
    { readReject(); return; }
    inventory_slots_t changes[INVENTORY_SIZE];
    int16_t indices[INVENTORY_SIZE];
    for (int32_t i = 0; i < changed_slots; i++)
    {
        indices[i] = readShort();
        int present = readByte();
        if (readFailed() || indices[i] < 0 || indices[i] >= INVENTORY_SIZE || present > 1)
        { readReject(); return; }
        changes[i] = (inventory_slots_t){0};
        if (present)
        {
            int32_t item = readVarInt(), count = readVarInt();
            int32_t added = readVarInt(), removed = readVarInt();
            if (readFailed() || item <= 0 || count <= 0 || count > 64 || added || removed)
            { readReject(); return; } // Item components are not implemented.
            changes[i] = (inventory_slots_t){(int16_t)count, item};
        }
    }
    for (int32_t i = 0; i < changed_slots; i++)
    {
        int16_t slot = indices[i];
        if (window_id == 0)
            storageInventoryUpdateSlot(currentPlayer, slot, changes[i].count, changes[i].item_id);
        else
        {
            currentPlayer->gamePlayerData.crafting_menu[slot] = changes[i];
            if (slot >= 1 && slot <= 9) currentPlayer->gamePlayerData.crafting_table_event = 1;
        }
    }

}
static void PlayC2S_container_close(player_t *currentPlayer)
{
    int32_t window_id = readVarInt(); // Window ID
    if (readFailed()) return;
    if (window_id == 1)
    {
        if (currentPlayer->gamePlayerData.crafting_menu)
        {
            storage_t *inventory = storageInventoryGet(currentPlayer);
            // restore the inventory
            if (!inventory) { currentPlayer->remove_player_event = 1; return; }
            inventory_slots_t offhand = inventory->inventory_slots[45];
            inventory_slots_t armor[4];
            // copy the armor
            for (int i = 0; i < 4; i++)
            {
                armor[i].count = inventory->inventory_slots[i + 5].count;
                armor[i].item_id = inventory->inventory_slots[i + 5].item_id;
            }
            memset(inventory->inventory_slots, 0, sizeof(inventory_slots_t) * INVENTORY_SIZE);
            for (int i = 10; i < INVENTORY_SIZE; i++)
            {
                inventory->inventory_slots[i - 1].item_id = currentPlayer->gamePlayerData.crafting_menu[i].item_id;
                inventory->inventory_slots[i - 1].count = currentPlayer->gamePlayerData.crafting_menu[i].count;
            }
            // the stuff in the crafting grid must be re-inserted back in the inventory
            for (int i = 1; i <= 9; i++)
            {
                if (currentPlayer->gamePlayerData.crafting_menu[i].item_id)
                {
                    storageInventoryInsertItem(currentPlayer, currentPlayer->gamePlayerData.crafting_menu[i].item_id, currentPlayer->gamePlayerData.crafting_menu[i].count);
                }
            }
            // restore armor
            for (int i = 0; i < 4; i++)
            {
                inventory->inventory_slots[i + 5].count = armor[i].count;
                inventory->inventory_slots[i + 5].item_id = armor[i].item_id;
            }
            inventory->inventory_slots[45] = offhand;
            U_free(currentPlayer->gamePlayerData.crafting_menu);
            currentPlayer->gamePlayerData.crafting_menu = NULL;
        }
    }
    currentPlayer->gamePlayerData.full_inventory_update_event = 1;
}
static void PlayC2S_container_slot_state_changed(player_t *currentPlayer) {}
static void PlayC2S_cookie_response(player_t *currentPlayer) {}
static void PlayC2S_custom_payload(player_t *currentPlayer) {}
static void PlayC2S_debug_subscription_request(player_t *currentPlayer) {}
static void PlayC2S_edit_book(player_t *currentPlayer) {}
static void PlayC2S_entity_tag_query(player_t *currentPlayer) {}
static void PlayC2S_interact(player_t *currentPlayer) {}
static void PlayC2S_jigsaw_generate(player_t *currentPlayer) {}
static void PlayC2S_keep_alive(player_t *currentPlayer)
{
    currentPlayer->heartbeat = 1;
}
static void PlayC2S_lock_difficulty(player_t *currentPlayer) {}
static void move_player(player_t *p, int position, int rotation)
{
    double x=p->x, y=p->y, z=p->z;
    float yaw=p->yaw, pitch=p->pitch;
    if (position) { x=readDouble(); y=readDouble(); z=readDouble(); }
    if (rotation) { yaw=readFloat(); pitch=readFloat(); }
    uint8_t flags=readByte();
    if (readFailed() || !isfinite(x) || !isfinite(y) || !isfinite(z) ||
        !isfinite(yaw) || !isfinite(pitch) || fabs(x)>30000000 || fabs(z)>30000000 || fabs(y)>30000000)
    { readReject(); return; }
    p->x=x; p->y=y; p->z=z;
    p->yaw=remainderf(yaw,360.0f); p->pitch=remainderf(pitch,360.0f);
    p->onground=flags & 1;
    p->position_event=1;
}
static void PlayC2S_move_player_pos(player_t *p) { move_player(p,1,0); }
static void PlayC2S_move_player_pos_rot(player_t *p) { move_player(p,1,1); }
static void PlayC2S_move_player_rot(player_t *p) { move_player(p,0,1); }
static void PlayC2S_move_player_status_only(player_t *currentPlayer) {}
static void PlayC2S_move_vehicle(player_t *currentPlayer) {}
static void PlayC2S_paddle_boat(player_t *currentPlayer) {}
static void PlayC2S_pick_item_from_block(player_t *currentPlayer) {}
static void PlayC2S_pick_item_from_entity(player_t *currentPlayer) {}
static void PlayC2S_ping_request(player_t *currentPlayer) {}
static void PlayC2S_place_recipe(player_t *currentPlayer) {}
static void PlayC2S_player_abilities(player_t *currentPlayer) {}
static void PlayC2S_player_action(player_t *currentPlayer)
{
    if (readVarInt() == 2) // Status
    {
        currentPlayer->gamePlayerData.block_state = MINECRAFT_AIR;
        readPosition(&currentPlayer->gamePlayerData.block_x, &currentPlayer->gamePlayerData.block_y, &currentPlayer->gamePlayerData.block_z); // Location
        currentPlayer->gamePlayerData.block_face = readByte();                                                                                // Face
        currentPlayer->gamePlayerData.block_sequence = readVarInt();                                                                          // Sequence
        if (readFailed() || currentPlayer->gamePlayerData.block_face > 5) { readReject(); return; }
        currentPlayer->gamePlayerData.action_item_event = 1;
    }
}
static void PlayC2S_player_command(player_t *currentPlayer) {}
static void PlayC2S_player_input(player_t *currentPlayer)
{
    currentPlayer->entity_action_id = readByte();
    currentPlayer->entity_action_event = 1;
}
static void PlayC2S_player_loaded(player_t *currentPlayer) {}
static void PlayC2S_pong(player_t *currentPlayer) {}
static void PlayC2S_recipe_book_change_settings(player_t *currentPlayer) {}
static void PlayC2S_recipe_book_seen_recipe(player_t *currentPlayer) {}
static void PlayC2S_rename_item(player_t *currentPlayer) {}
static void PlayC2S_resource_pack(player_t *currentPlayer) {}
static void PlayC2S_seen_advancements(player_t *currentPlayer) {}
static void PlayC2S_select_trade(player_t *currentPlayer) {}
static void PlayC2S_set_beacon(player_t *currentPlayer) {}
static void PlayC2S_set_carried_item(player_t *currentPlayer)
{
    int16_t slot = readShort();
    if (!readFailed() && slot >= 0 && slot < 9)
    {
        currentPlayer->gamePlayerData.inventory_slot = slot;
    }
    else readReject();
}
static void PlayC2S_set_command_block(player_t *currentPlayer) {}
static void PlayC2S_set_command_minecart(player_t *currentPlayer) {}
static void PlayC2S_set_creative_mode_slot(player_t *currentPlayer) {}
static void PlayC2S_set_game_rule(player_t *currentPlayer) {}
static void PlayC2S_set_jigsaw_block(player_t *currentPlayer) {}
static void PlayC2S_set_structure_block(player_t *currentPlayer) {}
static void PlayC2S_set_test_block(player_t *currentPlayer) {}
static void PlayC2S_sign_update(player_t *currentPlayer) {}
static void PlayC2S_spectate_entity(player_t *currentPlayer) {}
static void PlayC2S_swing(player_t *currentPlayer)
{
    currentPlayer->swing_arm_animation = readVarInt();
    currentPlayer->swing_arm_event = 1;
}
static void PlayC2S_teleport_to_entity(player_t *currentPlayer) {}
static void PlayC2S_test_instance_block_action(player_t *currentPlayer) {}
static void PlayC2S_use_item_on(player_t *currentPlayer)
{
    readVarInt();                                                                                                                         // Hand
    readPosition(&currentPlayer->gamePlayerData.block_x, &currentPlayer->gamePlayerData.block_y, &currentPlayer->gamePlayerData.block_z); // Location
    int32_t face = readVarInt();
    if (readFailed() || face < 0 || face > 5) { readReject(); return; }
    currentPlayer->gamePlayerData.block_face = (uint8_t)face;                                                                              // Face

    readFloat();                                                 // Cursor Position X
    readFloat();                                                 // Cursor Position Y
    readFloat();                                                 // Cursor Position Z
    readByte();                                                  // Inside block
    readByte();                                                  // World Border Hit
    currentPlayer->gamePlayerData.block_sequence = readVarInt(); // Sequence
    if (readFailed()) return;
    currentPlayer->gamePlayerData.action_item_event = 2;
}
static void PlayC2S_use_item(player_t *currentPlayer) {}
static void PlayC2S_custom_click_action(player_t *currentPlayer) {}

static void ConfigurationC2S_client_information(player_t *currentPlayer)
{
    char buf[17];
    readString(buf, sizeof(buf));           // Locale
    readByte();                             // View Distance
    readVarInt();                           // Chat Mode
    readByte();                             // Chat Colors
    currentPlayer->skin_parts = readByte(); // Displayed Skin Parts
    readVarInt();                           // Main Hand
    readByte();                             // Disable Text Filter
    readByte();                             // Allow server listing
    currentPlayer->settings_changed_event = 1;
}
static void ConfigurationC2S_cookie_response(player_t *currentPlayer) {}
static void ConfigurationC2S_custom_payload(player_t *currentPlayer) {}
static void ConfigurationC2S_finish_configuration(player_t *currentPlayer)
{
    currentPlayer->active = 1;
    currentPlayer->spawn_event = 1;
    currentPlayer->handshake_status = 4;
}
static void ConfigurationC2S_keep_alive(player_t *currentPlayer) {}
static void ConfigurationC2S_pong(player_t *currentPlayer) {}
static void ConfigurationC2S_resource_pack(player_t *currentPlayer) {}
static void ConfigurationC2S_select_known_packs(player_t *currentPlayer)
{
    currentPlayer->configuration_known_packs_ack_event = 1;
}
static void ConfigurationC2S_custom_click_action(player_t *currentPlayer) {}
static void ConfigurationC2S_accept_code_of_conduct(player_t *currentPlayer) {}

void (*c2s_play_26_1_2[])(player_t *currentPlayer) = {
    &PlayC2S_accept_teleportation,         // 0x00
    &PlayC2S_attack,                       // 0x01
    &PlayC2S_block_entity_tag_query,       // 0x02
    &PlayC2S_bundle_item_selected,         // 0x03
    &PlayC2S_change_difficulty,            // 0x04
    &PlayC2S_change_game_mode,             // 0x05
    &PlayC2S_chat_ack,                     // 0x06
    &PlayC2S_chat_command,                 // 0x07
    &PlayC2S_chat_command_signed,          // 0x08
    &PlayC2S_chat,                         // 0x09
    &PlayC2S_chat_session_update,          // 0x0A
    &PlayC2S_chunk_batch_received,         // 0x0B
    &PlayC2S_client_command,               // 0x0C
    &PlayC2S_client_tick_end,              // 0x0D
    &PlayC2S_client_information,           // 0x0E
    &PlayC2S_command_suggestion,           // 0x0F
    &PlayC2S_configuration_acknowledged,   // 0x10
    &PlayC2S_container_button_click,       // 0x11
    &PlayC2S_container_click,              // 0x12
    &PlayC2S_container_close,              // 0x13
    &PlayC2S_container_slot_state_changed, // 0x14
    &PlayC2S_cookie_response,              // 0x15
    &PlayC2S_custom_payload,               // 0x16
    &PlayC2S_debug_subscription_request,   // 0x17
    &PlayC2S_edit_book,                    // 0x18
    &PlayC2S_entity_tag_query,             // 0x19
    &PlayC2S_interact,                     // 0x1A
    &PlayC2S_jigsaw_generate,              // 0x1B
    &PlayC2S_keep_alive,                   // 0x1C
    &PlayC2S_lock_difficulty,              // 0x1D
    &PlayC2S_move_player_pos,              // 0x1E
    &PlayC2S_move_player_pos_rot,          // 0x1F
    &PlayC2S_move_player_rot,              // 0x20
    &PlayC2S_move_player_status_only,      // 0x21
    &PlayC2S_move_vehicle,                 // 0x22
    &PlayC2S_paddle_boat,                  // 0x23
    &PlayC2S_pick_item_from_block,         // 0x24
    &PlayC2S_pick_item_from_entity,        // 0x25
    &PlayC2S_ping_request,                 // 0x26
    &PlayC2S_place_recipe,                 // 0x27
    &PlayC2S_player_abilities,             // 0x28
    &PlayC2S_player_action,                // 0x29
    &PlayC2S_player_command,               // 0x2A
    &PlayC2S_player_input,                 // 0x2B
    &PlayC2S_player_loaded,                // 0x2C
    &PlayC2S_pong,                         // 0x2D
    &PlayC2S_recipe_book_change_settings,  // 0x2E
    &PlayC2S_recipe_book_seen_recipe,      // 0x2F
    &PlayC2S_rename_item,                  // 0x30
    &PlayC2S_resource_pack,                // 0x31
    &PlayC2S_seen_advancements,            // 0x32
    &PlayC2S_select_trade,                 // 0x33
    &PlayC2S_set_beacon,                   // 0x34
    &PlayC2S_set_carried_item,             // 0x35
    &PlayC2S_set_command_block,            // 0x36
    &PlayC2S_set_command_minecart,         // 0x37
    &PlayC2S_set_creative_mode_slot,       // 0x38
    &PlayC2S_set_game_rule,                // 0x39
    &PlayC2S_set_jigsaw_block,             // 0x3A
    &PlayC2S_set_structure_block,          // 0x3B
    &PlayC2S_set_test_block,               // 0x3C
    &PlayC2S_sign_update,                  // 0x3D
    &PlayC2S_spectate_entity,              // 0x3E
    &PlayC2S_swing,                        // 0x3F
    &PlayC2S_teleport_to_entity,           // 0x40
    &PlayC2S_test_instance_block_action,   // 0x41
    &PlayC2S_use_item_on,                  // 0x42
    &PlayC2S_use_item,                     // 0x43
    &PlayC2S_custom_click_action,          // 0x44
};
void (*c2s_configuration_26_1_2[])(player_t *currentPlayer) = {
    &ConfigurationC2S_client_information,     // 0x00
    &ConfigurationC2S_cookie_response,        // 0x01
    &ConfigurationC2S_custom_payload,         // 0x02
    &ConfigurationC2S_finish_configuration,   // 0x03
    &ConfigurationC2S_keep_alive,             // 0x04
    &ConfigurationC2S_pong,                   // 0x05
    &ConfigurationC2S_resource_pack,          // 0x06
    &ConfigurationC2S_select_known_packs,     // 0x07
    &ConfigurationC2S_custom_click_action,    // 0x08
    &ConfigurationC2S_accept_code_of_conduct, // 0x09
};
