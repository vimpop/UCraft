#ifndef _S2C_H
#define _S2C_H
#include "socketio.h"
#include "enums.h"
#include "blocks/block_states.h"
#include "world.h"
#include "storage.h"

typedef enum S2CPlayPacketId
{
    S2C_PLAY_BUNDLE_DELIMITER = 0x00,
    S2C_PLAY_ADD_ENTITY = 0x01,
    S2C_PLAY_ANIMATE = 0x02,
    S2C_PLAY_AWARD_STATS = 0x03,
    S2C_PLAY_BLOCK_CHANGED_ACK = 0x04,
    S2C_PLAY_BLOCK_DESTRUCTION = 0x05,
    S2C_PLAY_BLOCK_ENTITY_DATA = 0x06,
    S2C_PLAY_BLOCK_EVENT = 0x07,
    S2C_PLAY_BLOCK_UPDATE = 0x08,
    S2C_PLAY_BOSS_EVENT = 0x09,
    S2C_PLAY_CHANGE_DIFFICULTY = 0x0A,
    S2C_PLAY_CHUNK_BATCH_FINISHED = 0x0B,
    S2C_PLAY_CHUNK_BATCH_START = 0x0C,
    S2C_PLAY_CHUNKS_BIOMES = 0x0D,
    S2C_PLAY_CLEAR_TITLES = 0x0E,
    S2C_PLAY_COMMAND_SUGGESTIONS = 0x0F,
    S2C_PLAY_COMMANDS = 0x10,
    S2C_PLAY_CONTAINER_CLOSE = 0x11,
    S2C_PLAY_CONTAINER_SET_CONTENT = 0x12,
    S2C_PLAY_CONTAINER_SET_DATA = 0x13,
    S2C_PLAY_CONTAINER_SET_SLOT = 0x14,
    S2C_PLAY_COOKIE_REQUEST = 0x15,
    S2C_PLAY_COOLDOWN = 0x16,
    S2C_PLAY_CUSTOM_CHAT_COMPLETIONS = 0x17,
    S2C_PLAY_CUSTOM_PAYLOAD = 0x18,
    S2C_PLAY_DAMAGE_EVENT = 0x19,
    S2C_PLAY_DEBUG_BLOCK_VALUE = 0x1A,
    S2C_PLAY_DEBUG_CHUNK_VALUE = 0x1B,
    S2C_PLAY_DEBUG_ENTITY_VALUE = 0x1C,
    S2C_PLAY_DEBUG_EVENT = 0x1D,
    S2C_PLAY_DEBUG_SAMPLE = 0x1E,
    S2C_PLAY_DELETE_CHAT = 0x1F,
    S2C_PLAY_DISCONNECT = 0x20,
    S2C_PLAY_DISGUISED_CHAT = 0x21,
    S2C_PLAY_ENTITY_EVENT = 0x22,
    S2C_PLAY_ENTITY_POSITION_SYNC = 0x23,
    S2C_PLAY_EXPLODE = 0x24,
    S2C_PLAY_FORGET_LEVEL_CHUNK = 0x25,
    S2C_PLAY_GAME_EVENT = 0x26,
    S2C_PLAY_GAME_RULE_VALUES = 0x27,
    S2C_PLAY_GAME_TEST_HIGHLIGHT_POS = 0x28,
    S2C_PLAY_MOUNT_SCREEN_OPEN = 0x29,
    S2C_PLAY_HURT_ANIMATION = 0x2A,
    S2C_PLAY_INITIALIZE_BORDER = 0x2B,
    S2C_PLAY_KEEP_ALIVE = 0x2C,
    S2C_PLAY_LEVEL_CHUNK_WITH_LIGHT = 0x2D,
    S2C_PLAY_LEVEL_EVENT = 0x2E,
    S2C_PLAY_LEVEL_PARTICLES = 0x2F,
    S2C_PLAY_LIGHT_UPDATE = 0x30,
    S2C_PLAY_LOGIN = 0x31,
    S2C_PLAY_LOW_DISK_SPACE_WARNING = 0x32,
    S2C_PLAY_MAP_ITEM_DATA = 0x33,
    S2C_PLAY_MERCHANT_OFFERS = 0x34,
    S2C_PLAY_MOVE_ENTITY_POS = 0x35,
    S2C_PLAY_MOVE_ENTITY_POS_ROT = 0x36,
    S2C_PLAY_MOVE_MINECART_ALONG_TRACK = 0x37,
    S2C_PLAY_MOVE_ENTITY_ROT = 0x38,
    S2C_PLAY_MOVE_VEHICLE = 0x39,
    S2C_PLAY_OPEN_BOOK = 0x3A,
    S2C_PLAY_OPEN_SCREEN = 0x3B,
    S2C_PLAY_OPEN_SIGN_EDITOR = 0x3C,
    S2C_PLAY_PING = 0x3D,
    S2C_PLAY_PONG_RESPONSE = 0x3E,
    S2C_PLAY_PLACE_GHOST_RECIPE = 0x3F,
    S2C_PLAY_PLAYER_ABILITIES = 0x40,
    S2C_PLAY_PLAYER_CHAT = 0x41,
    S2C_PLAY_PLAYER_COMBAT_END = 0x42,
    S2C_PLAY_PLAYER_COMBAT_ENTER = 0x43,
    S2C_PLAY_PLAYER_COMBAT_KILL = 0x44,
    S2C_PLAY_PLAYER_INFO_REMOVE = 0x45,
    S2C_PLAY_PLAYER_INFO_UPDATE = 0x46,
    S2C_PLAY_PLAYER_LOOK_AT = 0x47,
    S2C_PLAY_PLAYER_POSITION = 0x48,
    S2C_PLAY_PLAYER_ROTATION = 0x49,
    S2C_PLAY_RECIPE_BOOK_ADD = 0x4A,
    S2C_PLAY_RECIPE_BOOK_REMOVE = 0x4B,
    S2C_PLAY_RECIPE_BOOK_SETTINGS = 0x4C,
    S2C_PLAY_REMOVE_ENTITIES = 0x4D,
    S2C_PLAY_REMOVE_MOB_EFFECT = 0x4E,
    S2C_PLAY_RESET_SCORE = 0x4F,
    S2C_PLAY_RESOURCE_PACK_POP = 0x50,
    S2C_PLAY_RESOURCE_PACK_PUSH = 0x51,
    S2C_PLAY_RESPAWN = 0x52,
    S2C_PLAY_ROTATE_HEAD = 0x53,
    S2C_PLAY_SECTION_BLOCKS_UPDATE = 0x54,
    S2C_PLAY_SELECT_ADVANCEMENTS_TAB = 0x55,
    S2C_PLAY_SERVER_DATA = 0x56,
    S2C_PLAY_SET_ACTION_BAR_TEXT = 0x57,
    S2C_PLAY_SET_BORDER_CENTER = 0x58,
    S2C_PLAY_SET_BORDER_LERP_SIZE = 0x59,
    S2C_PLAY_SET_BORDER_SIZE = 0x5A,
    S2C_PLAY_SET_BORDER_WARNING_DELAY = 0x5B,
    S2C_PLAY_SET_BORDER_WARNING_DISTANCE = 0x5C,
    S2C_PLAY_SET_CAMERA = 0x5D,
    S2C_PLAY_SET_CHUNK_CACHE_CENTER = 0x5E,
    S2C_PLAY_SET_CHUNK_CACHE_RADIUS = 0x5F,
    S2C_PLAY_SET_CURSOR_ITEM = 0x60,
    S2C_PLAY_SET_DEFAULT_SPAWN_POSITION = 0x61,
    S2C_PLAY_SET_DISPLAY_OBJECTIVE = 0x62,
    S2C_PLAY_SET_ENTITY_DATA = 0x63,
    S2C_PLAY_SET_ENTITY_LINK = 0x64,
    S2C_PLAY_SET_ENTITY_MOTION = 0x65,
    S2C_PLAY_SET_EQUIPMENT = 0x66,
    S2C_PLAY_SET_EXPERIENCE = 0x67,
    S2C_PLAY_SET_HEALTH = 0x68,
    S2C_PLAY_SET_HELD_SLOT = 0x69,
    S2C_PLAY_SET_OBJECTIVE = 0x6A,
    S2C_PLAY_SET_PASSENGERS = 0x6B,
    S2C_PLAY_SET_PLAYER_INVENTORY = 0x6C,
    S2C_PLAY_SET_PLAYER_TEAM = 0x6D,
    S2C_PLAY_SET_SCORE = 0x6E,
    S2C_PLAY_SET_SIMULATION_DISTANCE = 0x6F,
    S2C_PLAY_SET_SUBTITLE_TEXT = 0x70,
    S2C_PLAY_SET_TIME = 0x71,
    S2C_PLAY_SET_TITLE_TEXT = 0x72,
    S2C_PLAY_SET_TITLES_ANIMATION = 0x73,
    S2C_PLAY_SOUND_ENTITY = 0x74,
    S2C_PLAY_SOUND = 0x75,
    S2C_PLAY_START_CONFIGURATION = 0x76,
    S2C_PLAY_STOP_SOUND = 0x77,
    S2C_PLAY_STORE_COOKIE = 0x78,
    S2C_PLAY_SYSTEM_CHAT = 0x79,
    S2C_PLAY_TAB_LIST = 0x7A,
    S2C_PLAY_TAG_QUERY = 0x7B,
    S2C_PLAY_TAKE_ITEM_ENTITY = 0x7C,
    S2C_PLAY_TELEPORT_ENTITY = 0x7D,
    S2C_PLAY_TEST_INSTANCE_BLOCK_STATUS = 0x7E,
    S2C_PLAY_TICKING_STATE = 0x7F,
    S2C_PLAY_TICKING_STEP = 0x80,
    S2C_PLAY_TRANSFER = 0x81,
    S2C_PLAY_UPDATE_ADVANCEMENTS = 0x82,
    S2C_PLAY_UPDATE_ATTRIBUTES = 0x83,
    S2C_PLAY_UPDATE_MOB_EFFECT = 0x84,
    S2C_PLAY_UPDATE_RECIPES = 0x85,
    S2C_PLAY_UPDATE_TAGS = 0x86,
    S2C_PLAY_PROJECTILE_POWER = 0x87,
    S2C_PLAY_CUSTOM_REPORT_DETAILS = 0x88,
    S2C_PLAY_SERVER_LINKS = 0x89,
    S2C_PLAY_WAYPOINT = 0x8A,
    S2C_PLAY_CLEAR_DIALOG = 0x8B,
    S2C_PLAY_SHOW_DIALOG = 0x8C,

    S2C_PLAY_MAPPING_LEN = 0x8D
} S2CPlayPacketId;

typedef enum S2CConfigurationPacketId
{
    S2C_CONFIGURATION_COOKIE_REQUEST = 0x00,
    S2C_CONFIGURATION_CUSTOM_PAYLOAD = 0x01,
    S2C_CONFIGURATION_DISCONNECT = 0x02,
    S2C_CONFIGURATION_FINISH_CONFIGURATION = 0x03,
    S2C_CONFIGURATION_KEEP_ALIVE = 0x04,
    S2C_CONFIGURATION_PING = 0x05,
    S2C_CONFIGURATION_RESET_CHAT = 0x06,
    S2C_CONFIGURATION_REGISTRY_DATA = 0x07,
    S2C_CONFIGURATION_RESOURCE_PACK_POP = 0x08,
    S2C_CONFIGURATION_RESOURCE_PACK_PUSH = 0x09,
    S2C_CONFIGURATION_STORE_COOKIE = 0x0A,
    S2C_CONFIGURATION_TRANSFER = 0x0B,
    S2C_CONFIGURATION_UPDATE_ENABLED_FEATURES = 0x0C,
    S2C_CONFIGURATION_UPDATE_TAGS = 0x0D,
    S2C_CONFIGURATION_SELECT_KNOWN_PACKS = 0x0E,
    S2C_CONFIGURATION_CUSTOM_REPORT_DETAILS = 0x0F,
    S2C_CONFIGURATION_SERVER_LINKS = 0x10,
    S2C_CONFIGURATION_CLEAR_DIALOG = 0x11,
    S2C_CONFIGURATION_SHOW_DIALOG = 0x12,
    S2C_CONFIGURATION_CODE_OF_CONDUCT = 0x13,

    S2C_CONFIGURATION_MAPPING_LEN = 0x14
} S2CConfigurationPacketId;

void StatusS2Cresponse(player_t *currentPlayer);
void StatusS2Cpong(player_t *currentPlayer);
void LoginS2Cdisconnect(player_t *currentPlayer, char *reason);
void LoginS2Cencryptionrequest(player_t *currentPlayer);
void LoginS2Ccompression(player_t *currentPlayer);
void LoginS2Csuccess(player_t *currentPlayer);
void PlayS2Clogin(player_t *currentPlayer);
void PlayS2Ctablist(player_t *currentPlayer, TabListAction action, uint16_t eid);
void PlayS2Cgameevent(GameEvent event, float value);
void PlayS2Cplayerabilities(player_t *currentPlayer, PlayerAbilities abilities);
void PlayS2Ctablistremove(uint16_t eid);
void PlayS2Cspawnentity(player_t *currentPlayer, EntityMetadataType type);
void PlayS2Cpositionrotation(player_t *currentPlayer, double x, double y,
                             double z);
void PlayS2Cchunkcenter(player_t *currentPlayer, int32_t x, int32_t z);
void PlayS2Cchunk(player_t *currentPlayer, int32_t x, int32_t z, int32_t from, int32_t to);
void PlayS2Cheartbeat(player_t *currentPlayer);
void PlayS2Crotation(player_t *currentPlayer);
void PlayS2Cteleport(player_t *currentPlayer, double x, double y, double z);
void PlayS2Cheadrotation(player_t *currentPlayer);
void PlayS2Centityanimation(player_t *currentPlayer, uint8_t animation);
void PlayS2Ckick(player_t *currentPlayer, char *str);

void PlayS2Csysmessage(char *message,
                       size_t len);
void PlayS2Centitydestroy(int32_t eid);
void PlayS2Cblock(blocksDefaultState blockstate, int32_t x, int32_t y, int32_t z);
void PlayS2Cblockchangeack(player_t *currentPlayer, int32_t sequence);
void PlayS2Cbossbar(player_t *currentPlayer, uint16_t uuid, int32_t action,
                    char *title, size_t len, float health);
void PlayS2Centitydata(player_t *currentPlayer, uint8_t entity, EntityDataMetadata type, EntityState state);
void PlayS2Ccompassposition(player_t *currentPlayer, int32_t x, int32_t y, int32_t z);
void PlayS2Cdisconnect(player_t *currentPlayer, char *reason);
void PlayS2Csettime(int64_t time_of_day, uint8_t time_of_day_increasing);
void PlayS2Ccontainersetcontent(player_t *currentPlayer, storage_t *inventory);
void PlayS2Ccontainersetslot(player_t *currentPlayer, int32_t window_id, int16_t slot, int16_t count, int32_t item_id);
void PlayS2Copenscreen(player_t *currentPlayer, int32_t window_id, WindowTypes window_type, char *window_title);

void ConfigurationS2Cfeatures();
void ConfigurationS2Cknownpacks();
void ConfigurationS2Cregistry();
void ConfigurationS2Cupdatetags();
void ConfigurationS2Cready();
void ConfigurationS2Cdisconnect(player_t *currentPlayer, char *reason);

#endif