#include <stdint.h>
#include <stdio.h>

#include "s2c.h"
#include "enums.h"
#include "log.h"
#include "encryption.h"
#include "wrapper.h"
#include "util.h"
#include "blocks.h"

#ifdef ONLINE_MODE
#include "mbedtls/base64.h"
#endif /*ONLINE_MODE*/

// Status packets
void StatusS2Cresponse(player_t *currentPlayer)
{
  char buffer[300];
  char scratch[24];
  static const char json1[] = "{\"description\":\"" MOTD "\", \"players\":{\"max\":";
  static const char json2[] = ",\"online\":";
  static const char json3[] = "},\"version\":{\"name\":\"" LONG_PROTOCOL_VERSION "\",\"protocol\":";
  static const char json4[] = "}}";
  sendStart();
  sendByte(0);
  sendSwitchToLocalBuffer(buffer, sizeof(buffer));
  sendBuffer(json1, strlen(json1));
  snprintf(scratch, sizeof(scratch), "%d", MAX_PLAYERS);
  sendBuffer(scratch, strnlen(scratch, sizeof(scratch)));
  sendBuffer(json2, strlen(json2));
  snprintf(scratch, sizeof(scratch), "%ld", playerGetActiveCount());
  sendBuffer(scratch, strnlen(scratch, sizeof(scratch)));
  sendBuffer(json3, strlen(json3));
  snprintf(scratch, sizeof(scratch), "%d", PROTOCOL_VERSION);
  sendBuffer(scratch, strnlen(scratch, sizeof(scratch)));
  sendBuffer(json4, strlen(json4));
  int size = sendRevertFromLocalBuffer();
  sendString(buffer, size);
  sendDone();
}
void StatusS2Cpong(player_t *currentPlayer)
{

  sendStart();
  sendByte(0x01);
  sendBuffer(currentPlayer->ping_payload, sizeof(currentPlayer->ping_payload));
  sendDone();
}
// Login packets
void LoginS2Cdisconnect(player_t *currentPlayer, char *reason)
{
  char buffer[300];
  sendStart();
  sendByte(0x00);
  size_t len = snprintf(buffer, sizeof(buffer), "{\"text\":\"%s\"}", reason);
  sendString(buffer, len);
  sendDone();
}
#ifdef ONLINE_MODE
void LoginS2Cencryptionrequest(player_t *currentPlayer)
{
  uint8_t key_buf[256];
  char serverid[20];
  char token[4];
  sendStart();
  sendByte(0x01);
  // send the server id {scratch buffer}
  memset(serverid, 0, sizeof(serverid));
  sendString((char *)serverid, 20);
  int ret = mbedtls_pk_write_pubkey_der(&encryptionGetData()->key, key_buf, sizeof(key_buf));
  if (ret < 0)
  {
    printl(LOG_ERROR, "mbedtls_pk_write_pubkey_der returned %d\n", ret);
    return;
  }
  // send the public key
  sendVarInt(ret);
  sendBuffer((char *)&key_buf[sizeof(key_buf) - ret], ret);
  // generate a random token
  ret = mbedtls_ctr_drbg_random(&encryptionGetData()->ctr_drbg, (unsigned char *)&token, sizeof(token));
  if (ret < 0)
  {
    printl(LOG_ERROR, "mbedtls_ctr_drbg_random returned %d\n", ret);
    return;
  }
  memcpy(currentPlayer->verify_token, token, sizeof(token));
  sendVarInt(4);
  sendBuffer(token, 4);
  sendByte(1);
  sendDone();
}
#endif /*ONLINE_MODE*/
void LoginS2Ccompression(player_t *currentPlayer)
{
  sendStart();
  sendByte(0x3);
  sendVarInt(COMPRESSION_THRESHOLD);
  sendDone();
  currentPlayer->compression_flag = 1;
}
void LoginS2Csuccess(player_t *currentPlayer)
{
  sendStart();
  sendByte(0x02);
  sendUUID(currentPlayer->id); // UUID
  // sendBuffer((char*)currentPlayer->uuid, 16);
  sendString(currentPlayer->name, -1); // Username
#ifdef ONLINE_MODE_AUTH
  if (currentPlayer->texture_value && currentPlayer->texture_signature)
  {
    sendVarInt(1);                                                              // Number Of Properties
    sendString("textures", -1);                                                 // Property Name
    sendString(currentPlayer->texture_value, currentPlayer->texture_value_len); // Value
    sendByte(1);                                                                // Is Signed
    sendString(currentPlayer->texture_signature, currentPlayer->texture_signature_len);
  }
  else
  {
    sendVarInt(0); // Number Of Properties
  }
#else
  sendVarInt(0); // Number Of Properties
#endif /*ONLINE_MODE_AUTH*/
  sendDone();
}
// Play packets

void PlayS2Clogin(player_t *currentPlayer)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_LOGIN);
  sendInt(currentPlayer->id);
  sendByte(0);                 // Is hardcore
  sendVarInt(1);               // World Count
  sendString("overworld", -1); // Dimension Names
  sendVarInt(MAX_PLAYERS);     // Max Players
  sendVarInt(VIEWDISTANCE);    // viewdistance
  sendVarInt(VIEWDISTANCE);    // simulationdistance
  sendByte(0);                 // Reduced Debug Info
  sendByte(1);                 // Enable respawn screen
  sendByte(0);                 // Do limited crafting
  sendVarInt(0);               // Dimension Type
  sendString("overworld", -1); // Dimension Name
  sendLong(0x482304890);       //{hashedSeed} random bytes
  sendByte(GAMEMODE);          // gamemode
  currentPlayer->gamemode = GAMEMODE;
  sendByte(-1);   // previous gamemode
  sendByte(0);    // Is Debug
  sendByte(0);    // Is Flat
  sendByte(0);    // Has death location
  sendVarInt(0);  // Portal cooldown
  sendVarInt(64); // Sea level
  sendByte(0);    // Enforces Secure Chat
  sendDone();
}
void PlayS2Ctablist(player_t *currentPlayer, TabListAction action, uint16_t eid)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_PLAYER_INFO_UPDATE);
  // TODO: handle more actions
  sendByte(action);
  sendVarInt(1); // Number Of Actions
  sendUUID(eid);
  if (action & TABLIST_ACTION_ADDPLAYER)
  {
    sendString(currentPlayer->name, -1);
#ifdef ONLINE_MODE_AUTH
    if (currentPlayer->texture_value && currentPlayer->texture_signature)
    {
      sendVarInt(1);                                                              // Number Of Properties
      sendString("textures", -1);                                                 // Property Name
      sendString(currentPlayer->texture_value, currentPlayer->texture_value_len); // Value
      sendByte(1);                                                                // Is Signed
      sendString(currentPlayer->texture_signature, currentPlayer->texture_signature_len);
    }
    else
    {
      sendVarInt(0); // Number Of Properties
    }
#else
    sendVarInt(0); // Number Of Properties
#endif /*ONLINE_MODE_AUTH*/
  }
  if (action & TABLIST_ACTION_GAMEMODE)
  {
    sendVarInt(currentPlayer->gamemode);
  }
  if (action & TABLIST_ACTION_LISTED)
  {
    sendByte(1);
  }
  if (action & TABLIST_ACTION_LATENCY)
  {
    sendVarInt(0);
  }
  if (action & TABLIST_ACTION_NAME)
  {
    printl(LOG_WARN, "tablist action name is not implemented!\n");
    sendByte(0x0);
  }
  sendDone();
}
void PlayS2Cgameevent(GameEvent event, float value)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_GAME_EVENT);
  sendByte(event);
  sendFloat(value);
  sendDone();
}
void PlayS2Cplayerabilities(player_t *currentPlayer, PlayerAbilities abilities)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_PLAYER_ABILITIES);
  sendByte(abilities);
  sendFloat(0.05); // Flying Speed
  sendFloat(0.1);  // FOV Modifier
  sendDone();
}
void PlayS2Ctablistremove(uint16_t eid)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_PLAYER_INFO_REMOVE);
  sendVarInt(1);
  sendUUID(eid);
  sendDone();
}
void PlayS2Cspawnentity(player_t *currentPlayer, EntityMetadataType type)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_ADD_ENTITY);
  sendVarInt(currentPlayer->id); // EID
  sendUUID(currentPlayer->id);   // UUID
  sendVarInt(type);              // Entity Type
  sendDouble(0);
  sendDouble(0);
  sendDouble(0);
  sendByte(0);
  sendByte(currentPlayer->npitch);
  sendByte(currentPlayer->nyaw);
  sendByte(currentPlayer->nyaw);
  sendVarInt(0);
  sendDone();
}
void PlayS2Cpositionrotation(player_t *currentPlayer, double x, double y, double z)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_PLAYER_POSITION);
  sendVarInt(0); // teleportId
  sendDouble(x);
  sendDouble(y);
  sendDouble(z);
  // TODO: velocity fields, unknown for now
  sendDouble(0); // x
  sendDouble(0); // y
  sendDouble(0); // z
  sendFloat(currentPlayer->yaw);
  sendFloat(currentPlayer->pitch);
  sendInt(0); // xyz absolute
  sendDone();
}
void PlayS2Cchunkcenter(player_t *currentPlayer, int32_t x, int32_t z)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_SET_CHUNK_CACHE_CENTER);
  sendVarInt(x);
  sendVarInt(z);
  sendDone();
}
void PlayS2Cchunk(player_t *currentPlayer, int32_t x, int32_t z, int32_t from, int32_t to)
{

  sendStart();
  sendPlayPacketHeader(S2C_PLAY_LEVEL_CHUNK_WITH_LIGHT);
  sendInt(x);
  sendInt(z);
  sendVarInt(0); // Heightmap count
  sendPrefixedStart();
  worldGenerateChunk(x, z, from, to);
  sendPrefixedEnd();
  sendVarInt(0); // block entiies
  sendVarInt(1); // Sky Light Mask
  const int light_section_count = to + 1;
  const uint64_t light_mask = (light_section_count >= 64) ? UINT64_MAX : ((1ULL << light_section_count) - 1);
  sendLong((int64_t)light_mask);
  // Block Light Mask
  sendVarInt(1);
  sendLong(0);
  // Empty Sky Light Mask
  sendVarInt(1);
  sendLong(0);
  // Empty Block Light Mask
  sendVarInt(1);
  sendLong((int64_t)light_mask);

  sendVarInt(light_section_count); // Sky Light array count
  for (int i = 0; i < light_section_count; i++)
  {
    sendVarInt(2048);
    for (int b = 0; b < 2048; b++)
    {
      sendByte(0xFF);
    }
  }
  sendVarInt(0); // Block Light array count
  sendDone();

  Blocks *blocks = blocksGet(x, z);
  if (blocks != NULL)
  {
    for (size_t i = 0; i < blocks->count; i++)
    {
      PlayS2Cblock(blocks->block[i].default_state, blocks->block[i].c.x + (x << 4), blocks->block[i].c.y, blocks->block[i].c.z + (z << 4));
    }
  }
}
void PlayS2Cheartbeat(player_t *currentPlayer)
{
  extern size_t main_tick;
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_KEEP_ALIVE);
  sendLong(main_tick);
  sendDone();
}
void PlayS2Crotation(player_t *currentPlayer)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_MOVE_ENTITY_ROT);
  sendVarInt(currentPlayer->id);
  sendByte(currentPlayer->nyaw);
  sendByte(currentPlayer->npitch);
  sendByte(currentPlayer->onground);
  sendDone();
}
void PlayS2Cteleport(player_t *currentPlayer, double x, double y, double z)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_ENTITY_POSITION_SYNC);
  sendVarInt(currentPlayer->id);
  sendDouble(x);
  sendDouble(y);
  sendDouble(z);
  // TODO: Velocity fields
  sendDouble(0);
  sendDouble(0);
  sendDouble(0);
  sendFloat(currentPlayer->yaw);
  sendFloat(currentPlayer->pitch);
  sendByte(currentPlayer->onground);
  sendDone();
  currentPlayer->x = x;
  currentPlayer->y = y;
  currentPlayer->z = z;
}
void PlayS2Cheadrotation(player_t *currentPlayer)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_ROTATE_HEAD);
  sendVarInt(currentPlayer->id);
  sendByte(currentPlayer->nyaw);
  sendDone();
}
void PlayS2Centityanimation(player_t *currentPlayer, uint8_t animation)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_ANIMATE);
  sendVarInt(currentPlayer->id);
  sendByte(animation);
  sendDone();
}

void PlayS2Csysmessage(char *message, size_t len)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_SYSTEM_CHAT);
  sendFormattedString(message, len);
  sendByte(0);
  sendDone();
}
void PlayS2Centitydestroy(int32_t eid)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_REMOVE_ENTITIES);
  sendVarInt(1);
  sendVarInt(eid);
  sendDone();
}
void PlayS2Cblock(blocksDefaultState blockstate, int32_t x, int32_t y, int32_t z)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_BLOCK_UPDATE);
  sendPosition(x, y, z);
  sendVarInt(blockstate);
  sendDone();
}
// Acknowledge Block Change
void PlayS2Cblockchangeack(player_t *currentPlayer, int32_t sequence)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_BLOCK_CHANGED_ACK);
  sendVarInt(sequence);
  sendDone();
}
// TODO: make this packet more cleaner
void PlayS2Cbossbar(player_t *currentPlayer, uint16_t uuid, int32_t action, char *title, size_t len, float health)
{
  char buf[300];
  size_t size;
  static const char json1[] = "{\"text\":\"";
  static const char json2[] = "\"}";
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_BOSS_EVENT);
  sendUUID(uuid);
  sendVarInt(action);
  switch (action)
  {
  case 0:
    sendSwitchToLocalBuffer(buf, sizeof(buf));
    sendBuffer(json1, strlen(json1));
    if (len < sizeof(buf) - 13)
    {
      if (title)
      {
        sendBuffer(title, len);
      }
    }
    sendBuffer(json2, strlen(json2));
    size = sendRevertFromLocalBuffer();
    sendString(buf, size);
    sendFloat(health);
    sendVarInt(4);
    sendVarInt(0); // 20 notches
    sendByte(0);
    break;
  case 2:
    sendFloat(health);
    break;
  case 3:
    sendSwitchToLocalBuffer(buf, sizeof(buf));
    sendBuffer(json1, strlen(json1));
    if (len < sizeof(buf) - 13)
    {
      if (title)
      {
        sendBuffer(title, len);
      }
    }
    sendBuffer(json2, strlen(json2));
    size = sendRevertFromLocalBuffer();
    sendString(buf, size);
  default:
    break;
  }
  sendDone();
}
void PlayS2Centitydata(player_t *currentPlayer, uint8_t entity, EntityDataMetadata type, EntityState state)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_SET_ENTITY_DATA);
  sendVarInt(currentPlayer->id);
  sendByte(entity); // Entity base class "Pose" field ENTITY_POSE
  sendVarInt(type); // Metadata
  sendVarInt(state);
  sendByte(0xff);
  sendDone();
}
void PlayS2Ccompassposition(player_t *currentPlayer, int32_t x, int32_t y, int32_t z)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_SET_DEFAULT_SPAWN_POSITION);
  sendString("overworld", -1);
  sendPosition(x, y, z);
  sendFloat(0);
  sendFloat(0);
  sendDone();
}
void PlayS2Cdisconnect(player_t *currentPlayer, char *reason)
{
  size_t len = strnlen(reason, sizeof(((player_t *)0)->disconnect_reason));
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_DISCONNECT);
  sendFormattedString(reason, len);
  sendDone();
}
void PlayS2Csettime(int64_t time_of_day, uint8_t time_of_day_increasing)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_SET_TIME);
  sendLong(time_of_day);
  sendByte(time_of_day_increasing ? 1 : 0);
  sendDone();
}

void PlayS2Ccontainersetcontent(player_t *currentPlayer, storage_t *inventory)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_CONTAINER_SET_CONTENT);
  sendVarInt(0); // Window ID
  sendVarInt(0); // State ID
  sendVarInt(INVENTORY_SIZE);
  for (int i = 0; i < INVENTORY_SIZE; i++)
  {
    sendVarInt(inventory->inventory_slots[i].count); // Item count
    if (inventory->inventory_slots[i].count != 0)
    {
      sendVarInt(inventory->inventory_slots[i].item_id); // Item ID
      sendVarInt(0);                                     // Add Data component array
      sendVarInt(0);                                     // Remove  Data component array
    }
  }
  sendVarInt(0); // Dragged by mouse Item Count
  sendDone();
}

void PlayS2Ccontainersetslot(player_t *currentPlayer, int32_t window_id, int16_t slot, int16_t count, int32_t item_id)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_CONTAINER_SET_SLOT);
  sendVarInt(window_id); // window id
  sendVarInt(0);         // State ID
  sendShort(slot);       // Slot
  sendVarInt(count);     // Item count
  if (count)
  {
    sendVarInt(item_id);
    sendVarInt(0);
    sendVarInt(0);
  }
  sendDone();
}

void PlayS2Copenscreen(player_t *currentPlayer, int32_t window_id, WindowTypes window_type, char *window_title)
{

  sendStart();
  sendPlayPacketHeader(S2C_PLAY_OPEN_SCREEN);
  sendVarInt(window_id);
  sendVarInt(window_type);
  sendFormattedString(window_title, -1);
  sendDone();
}
void ConfigurationS2Cfeatures()
{
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_UPDATE_ENABLED_FEATURES);
  sendVarInt(1);
  sendString("vanilla", -1);
  sendDone();
}
void ConfigurationS2Cknownpacks()
{
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_SELECT_KNOWN_PACKS);
  sendVarInt(1);
  sendString("minecraft", -1);
  sendString("core", -1);
  sendString(CLIENT_VERSION, -1);
  sendDone();
}

void ConfigurationS2Cregistry()
{
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("dimension_type", -1);
  sendVarInt(1);
  sendString("overworld", -1);
  sendByte(0);
  sendDone();

  static const char *biomes[] = {
      "plains",
      "snowy_taiga",
  };
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("worldgen/biome", -1);
  sendVarInt(sizeof(biomes) / sizeof(char *));
  for (size_t i = 0; i < (size_t)(sizeof(biomes) / sizeof(char *)); i++)
  {
    sendString(biomes[i], -1);
    sendByte(0);
  }
  sendDone();

  static const char *damage_types[] = {
      "arrow",
      "bad_respawn_point",
      "cactus",
      "campfire",
      "cramming",
      "dragon_breath",
      "drown",
      "dry_out",
      "ender_pearl",
      "explosion",
      "fall",
      "falling_anvil",
      "falling_block",
      "falling_stalactite",
      "fireball",
      "fireworks",
      "fly_into_wall",
      "freeze",
      "generic",
      "generic_kill",
      "hot_floor",
      "in_fire",
      "in_wall",
      "indirect_magic",
      "lava",
      "lightning_bolt",
      "mace_smash",
      "magic",
      "mob_attack",
      "mob_attack_no_aggro",
      "mob_projectile",
      "on_fire",
      "out_of_world",
      "outside_border",
      "player_attack",
      "player_explosion",
      "sonic_boom",
      "spit",
      "stalagmite",
      "starve",
      "sting",
      "sweet_berry_bush",
      "thorns",
      "thrown",
      "trident",
      "unattributed_fireball",
      "wind_charge",
      "wither",
      "wither_skull",
      "spear"};
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("damage_type", -1);
  sendVarInt(sizeof(damage_types) / sizeof(char *));
  for (size_t i = 0; i < (size_t)(sizeof(damage_types) / sizeof(char *)); i++)
  {
    sendString(damage_types[i], -1);
    sendByte(0);
  }
  sendDone();

  static const char *timeline[] = {
      "day",
      "early_game",
      "moon",
      "villager_schedule"};
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("timeline", -1);
  sendVarInt(sizeof(timeline) / sizeof(char *));
  for (size_t i = 0; i < (size_t)(sizeof(timeline) / sizeof(char *)); i++)
  {
    sendString(timeline[i], -1);
    sendByte(0);
  }
  sendDone();

  static const char *world_clock[] = {
      "overworld",
      "the_end",
  };
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("world_clock", -1);
  sendVarInt(sizeof(world_clock) / sizeof(char *));
  for (size_t i = 0; i < (size_t)(sizeof(world_clock) / sizeof(char *)); i++)
  {
    sendString(world_clock[i], -1);
    sendByte(0);
  }
  sendDone();

  static const char *trim_materials[] = {
      "diamond",
      "redstone",
      "emerald",
      "lapis",
      "quartz",
      "resin",
      "netherite",
      "amethyst",
      "copper",
      "gold",
      "iron"};
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("trim_material", -1);
  sendVarInt(sizeof(trim_materials) / sizeof(char *));
  for (size_t i = 0; i < (size_t)(sizeof(trim_materials) / sizeof(char *)); i++)
  {
    sendString(trim_materials[i], -1);
    sendByte(0);
  }
  sendDone();

  static const char *chicken_variant[] = {
      "cold",
      "temperate",
      "warm"};
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("chicken_variant", -1);
  sendVarInt(sizeof(chicken_variant) / sizeof(char *));
  for (size_t i = 0; i < (size_t)(sizeof(chicken_variant) / sizeof(char *)); i++)
  {
    sendString(chicken_variant[i], -1);
    sendByte(0);
  }
  sendDone();

  static const char *jukebox_song[] = {
      "11",
      "13",
      "5",
      "blocks",
      "cat",
      "chirp",
      "creator",
      "creator_music_box",
      "far",
      "lava_chicken",
      "mall",
      "mellohi",
      "otherside",
      "pigstep",
      "precipice",
      "relic",
      "stal",
      "strad",
      "tears",
      "wait",
      "ward"};
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("jukebox_song", -1);
  sendVarInt(sizeof(jukebox_song) / sizeof(char *));
  for (size_t i = 0; i < (size_t)(sizeof(jukebox_song) / sizeof(char *)); i++)
  {
    sendString(jukebox_song[i], -1);
    sendByte(0);
  }
  sendDone();

  static const char *instruments[] = {
      "admire_goat_horn",
      "call_goat_horn",
      "dream_goat_horn",
      "feel_goat_horn",
      "ponder_goat_horn",
      "seek_goat_horn",
      "sing_goat_horn",
      "yearn_goat_horn"};
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("instrument", -1);
  sendVarInt(sizeof(instruments) / sizeof(char *));
  for (size_t i = 0; i < (size_t)(sizeof(instruments) / sizeof(char *)); i++)
  {
    sendString(instruments[i], -1);
    sendByte(0);
  }
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("chicken_sound_variant", -1);
  sendVarInt(1);
  sendString("classic", -1);
  sendByte(0);
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("cat_sound_variant", -1);
  sendVarInt(1);
  sendString("classic", -1);
  sendByte(0);
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("cow_sound_variant", -1);
  sendVarInt(1);
  sendString("classic", -1);
  sendByte(0);
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("pig_sound_variant", -1);
  sendVarInt(1);
  sendString("classic", -1);
  sendByte(0);
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("zombie_nautilus_variant", -1);
  sendVarInt(1);
  sendString("temperate", -1);
  sendByte(0);
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("wolf_variant", -1);
  sendVarInt(1);
  sendString("ashen", -1);
  sendByte(0);
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("painting_variant", -1);
  sendVarInt(1);
  sendString("alban", -1);
  sendByte(0);
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("pig_variant", -1);
  sendVarInt(1);
  sendString("cold", -1);
  sendByte(0);
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("cat_variant", -1);
  sendVarInt(1);
  sendString("black", -1);
  sendByte(0);
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("cow_variant", -1);
  sendVarInt(1);
  sendString("cold", -1);
  sendByte(0);
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("frog_variant", -1);
  sendVarInt(1);
  sendString("cold", -1);
  sendByte(0);
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
  sendString("wolf_sound_variant", -1);
  sendVarInt(1);
  sendString("angry", -1);
  sendByte(0);
  sendDone();
}
void ConfigurationS2Cupdatetags()
{

  static const char *damage_types_tag[] = {
      "is_fire",
      "is_explosion",
      "bypasses_shield"};
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_UPDATE_TAGS);
  sendByte(1);
  sendString("damage_type", -1);
  sendVarInt(sizeof(damage_types_tag) / sizeof(char *));
  for (size_t i = 0; i < (size_t)(sizeof(damage_types_tag) / sizeof(char *)); i++)
  {
    sendString(damage_types_tag[i], -1);
    sendByte(0);
  }
  sendDone();

  static const char *banner_pattern_tag[] = {
      "pattern_item/creeper",
      "pattern_item/flower",
      "pattern_item/skull",
      "pattern_item/mojang",
      "pattern_item/skull",
      "pattern_item/globe",
      "pattern_item/piglin",
      "pattern_item/flow",
      "pattern_item/guster",
      "pattern_item/field_masoned",
      "pattern_item/bordure_indented"};
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_UPDATE_TAGS);
  sendByte(1);
  sendString("banner_pattern", -1);
  sendVarInt(sizeof(banner_pattern_tag) / sizeof(char *));
  for (size_t i = 0; i < (size_t)(sizeof(banner_pattern_tag) / sizeof(char *)); i++)
  {
    sendString(banner_pattern_tag[i], -1);
    sendByte(0);
  }
  sendDone();

  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_UPDATE_TAGS);
  sendByte(1);
  sendString("timeline", -1);
  sendByte(1);
  sendString("in_overworld", -1);
  sendByte(4);
  sendByte(3);
  sendByte(0);
  sendByte(2);
  sendByte(1);
  sendDone();
}
void ConfigurationS2Cready()
{
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_FINISH_CONFIGURATION);
  sendDone();
}
void ConfigurationS2Cdisconnect(player_t *currentPlayer, char *reason)
{
  char buffer[300];
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_DISCONNECT);
  size_t len = snprintf(buffer, sizeof(buffer), "{\"text\":\"%s\"}", reason);
  sendString(buffer, len);
  sendDone();
}
