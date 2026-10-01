#include "game.h"
#include "blocks.h"
#include "s2c.h"
#include "storage.h"
#include "world.h"
#include "blocks/items.h"

/* CHECK must stay enabled in Release builds (unlike assert). */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

typedef enum { CHUNK, CENTER, POSITION, COMPASS, EVENT, TIME, CONTENT, SLOT, BLOCK, ACK, SCREEN } message_kind;
typedef struct {
    message_kind kind;
    player_t *player;
    int64_t a, b, c, d;
} message;

static message messages[1024];
static size_t message_count;
static inventory_slots_t inventory_snapshot[INVENTORY_SIZE];
static player_t player, other;
static size_t active_players;
static int32_t terrain;
size_t main_tick;

static void record(message_kind kind, player_t *p, int64_t a, int64_t b, int64_t c, int64_t d)
{
    CHECK(message_count < sizeof(messages) / sizeof(messages[0]));
    messages[message_count++] = (message){kind, p, a, b, c, d};
}

static size_t count(message_kind kind)
{
    size_t result = 0;
    for (size_t i = 0; i < message_count; i++)
        if (messages[i].kind == kind) result++;
    return result;
}

static message last(message_kind kind)
{
    for (size_t i = message_count; i > 0; i--)
        if (messages[i - 1].kind == kind) return messages[i - 1];
    CHECK(0 && "expected message was not sent");
    return (message){0};
}

/* Link-time replacements: gameplay, inventory, and block storage remain real. */
size_t playerGetActiveCount(void) { return active_players; }
int worldGetBlock(int32_t x, int32_t y, int32_t z) { return terrain; }
void PlayS2Cchunk(player_t *p, int32_t x, int32_t z, int32_t from, int32_t to)
{ record(CHUNK, p, x, z, from, to); }
void PlayS2Cchunkcenter(player_t *p, int32_t x, int32_t z)
{ record(CENTER, p, x, z, 0, 0); }
void PlayS2Cpositionrotation(player_t *p, double x, double y, double z)
{
    CHECK(x == SPAWN_X && y == SPAWN_Y && z == SPAWN_Z);
    record(POSITION, p, 0, 0, 0, 0);
}
void PlayS2Ccompassposition(player_t *p, int32_t x, int32_t y, int32_t z)
{
    CHECK(x == (int32_t)SPAWN_X && y == (int32_t)SPAWN_Y && z == (int32_t)SPAWN_Z);
    record(COMPASS, p, x, y, z, 0);
}
void PlayS2Cgameevent(GameEvent event, float value)
{
    CHECK(value == 1.0f);
    record(EVENT, NULL, event, 1, 0, 0);
}
void PlayS2Csettime(int64_t time, uint8_t increasing)
{ record(TIME, NULL, time, increasing, 0, 0); }
void PlayS2Ccontainersetcontent(player_t *p, storage_t *inventory)
{
    memcpy(inventory_snapshot, inventory->inventory_slots, sizeof(inventory_snapshot));
    record(CONTENT, p, 0, 0, 0, 0);
}
void PlayS2Ccontainersetslot(player_t *p, int32_t window, int16_t slot, int16_t n, int32_t item)
{ record(SLOT, p, window, slot, n, item); }
void PlayS2Cblock(blocksDefaultState state, int32_t x, int32_t y, int32_t z)
{ record(BLOCK, NULL, state, x, y, z); }
void PlayS2Cblockchangeack(player_t *p, int32_t sequence)
{ record(ACK, p, sequence, 0, 0, 0); }
void PlayS2Copenscreen(player_t *p, int32_t window, WindowTypes type, char *title)
{
    CHECK(strcmp(title, "Crafting") == 0);
    record(SCREEN, p, window, type, 0, 0);
}

static void begin(void)
{
    memset(&player, 0, sizeof(player));
    memset(&other, 0, sizeof(other));
    strcpy(player.name, "Alice");
    strcpy(other.name, "Bob");
    message_count = 0;
    memset(inventory_snapshot, 0, sizeof(inventory_snapshot));
    main_tick = 0;
    active_players = 0;
    terrain = MINECRAFT_AIR;
    gamePreload();
}

static void end(void)
{
    gamePlayerLeft(&player);
    gamePlayerLeft(&other);
    gameCleanup();
}

static inventory_slots_t slot(player_t *p, int index)
{
    inventory_slots_t result;
    storageInventoryGetSlot(p, (int16_t)index, &result);
    return result;
}

static void drain_chunks(void)
{
    int limit = (2 * VIEWDISTANCE + 1) * (2 * VIEWDISTANCE + 1) + 10;
    while (player.gamePlayerData.chunk_load_event && limit-- > 0)
        gamePlayerLocalTick(&player);
    CHECK(!player.gamePlayerData.chunk_load_event);
}

static void test_spawn(void)
{
    begin();
    storageInventoryUpdateSlot(&player, 36, 7, MINECRAFT_DIRT_ITEM);
    gamePlayerSpawned(&player);
    CHECK(count(CONTENT) == 1 && last(CONTENT).player == &player);
    CHECK(inventory_snapshot[36].count == 7);
    CHECK(inventory_snapshot[36].item_id == MINECRAFT_DIRT_ITEM);
    CHECK(last(TIME).a == 6000 && last(TIME).b == 0);
    CHECK(player.gamePlayerData.chunk_spawn_event);
    drain_chunks();
    int width = 2 * VIEWDISTANCE + 1;
    CHECK(count(CHUNK) == (size_t)(width * width));
    for (int x = -VIEWDISTANCE; x <= VIEWDISTANCE; x++)
        for (int z = -VIEWDISTANCE; z <= VIEWDISTANCE; z++) {
            int matches = 0;
            for (size_t i = 0; i < message_count; i++)
                if (messages[i].kind == CHUNK && messages[i].a == x && messages[i].b == z) {
                    CHECK(messages[i].player == &player);
                    CHECK(messages[i].c == 3 && messages[i].d == 7);
                    matches++;
                }
            CHECK(matches == 1);
        }
    CHECK(count(POSITION) == 1 && count(COMPASS) == 1 && count(EVENT) == 1);
    CHECK(last(EVENT).a == EVENT_WAIT_LEVEL_CHUNKS);
    CHECK(count(CENTER) == 1 && last(CENTER).a == 0 && last(CENTER).b == 0);
    CHECK(!player.gamePlayerData.chunk_spawn_event);
    size_t before = message_count;
    gamePlayerLocalTick(&player);
    CHECK(message_count == before);
    end();
}

static void test_chunks(void)
{
    const int moves[][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {-1,-1}, {1,-1}, {-1,1}, {3,-4}};
    for (size_t move = 0; move < sizeof(moves) / sizeof(moves[0]); move++) {
        begin();
        int dx = moves[move][0], dz = moves[move][1];
        player.chunk_x = dx;
        player.chunk_z = dz;
        gamePlayerLocalTick(&player);
        CHECK(count(CENTER) == 1 && last(CENTER).a == dx && last(CENTER).b == dz);
        drain_chunks();
        int full = abs(dx) > 1 || abs(dz) > 1;
        int width = 2 * VIEWDISTANCE + 1;
        CHECK(count(CHUNK) == (size_t)(full ? width * width : width * ((dx != 0) + (dz != 0))));
        for (size_t i = 0; i < message_count; i++) {
            message m = messages[i];
            if (m.kind != CHUNK) continue;
            CHECK(m.a >= dx - VIEWDISTANCE && m.a <= dx + VIEWDISTANCE);
            CHECK(m.b >= dz - VIEWDISTANCE && m.b <= dz + VIEWDISTANCE);
            CHECK(full || m.a < -VIEWDISTANCE || m.a > VIEWDISTANCE ||
                  m.b < -VIEWDISTANCE || m.b > VIEWDISTANCE);
        }
        for (int x = dx - VIEWDISTANCE; x <= dx + VIEWDISTANCE; x++)
            for (int z = dz - VIEWDISTANCE; z <= dz + VIEWDISTANCE; z++) {
                if (!full && abs(x) <= VIEWDISTANCE && abs(z) <= VIEWDISTANCE) continue;
                int seen = 0;
                for (size_t i = 0; i < message_count; i++)
                    if (messages[i].kind == CHUNK && messages[i].a == x && messages[i].b == z) seen++;
                CHECK(seen >= 1);
            }
        CHECK(count(POSITION) == 0 && count(EVENT) == 0);
        size_t before = message_count;
        gamePlayerLocalTick(&player);
        CHECK(message_count == before);
        end();
    }
}

static void action(int kind, int face)
{
    player.gamePlayerData.block_x = 10;
    player.gamePlayerData.block_y = 20;
    player.gamePlayerData.block_z = 30;
    player.gamePlayerData.block_state = MINECRAFT_AIR;
    player.gamePlayerData.block_sequence = 1234;
    player.gamePlayerData.block_face = (uint8_t)face;
    player.gamePlayerData.action_item_event = (uint8_t)kind;
}

static void check_action(int state, int x, int y, int z)
{
    CHECK(blocksGetBlock(x, y, z) == state);
    CHECK(count(BLOCK) == 1 && count(ACK) == 1);
    message m = last(BLOCK);
    CHECK(m.a == state && m.b == x && m.c == y && m.d == z);
    CHECK(last(ACK).player == &player && last(ACK).a == 1234);
    CHECK(player.gamePlayerData.block_update_event);
    CHECK(!player.gamePlayerData.action_item_event);
    gamePlayerGlobalTickOthers(&player);
    CHECK(count(BLOCK) == 2);
    message broadcast = last(BLOCK);
    CHECK(broadcast.a == m.a && broadcast.b == m.b && broadcast.c == m.c && broadcast.d == m.d);
    CHECK(!player.gamePlayerData.block_update_event);
    size_t before = message_count;
    gamePlayerGlobalTickOthers(&player);
    gamePlayerLocalTick(&player);
    CHECK(message_count == before);
}

static void test_placement(void)
{
    const int offsets[6][3] = {{0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}};
    for (int face = 0; face < 6; face++) {
        begin();
        storageInventoryUpdateSlot(&player, 36, 2, MINECRAFT_DIRT_ITEM);
        action(2, face);
        gamePlayerLocalTick(&player);
        CHECK(slot(&player, 36).count == 1 && slot(&player, 36).item_id == MINECRAFT_DIRT_ITEM);
        CHECK(count(CONTENT) == 1 && inventory_snapshot[36].count == 1);
        check_action(MINECRAFT_DIRT, 10 + offsets[face][0], 20 + offsets[face][1], 30 + offsets[face][2]);
        end();
    }
    begin();
    storageInventoryUpdateSlot(&player, 36, 1, MINECRAFT_DIRT_ITEM);
    action(2, 1);
    gamePlayerLocalTick(&player);
    CHECK(slot(&player, 36).count == 0 && slot(&player, 36).item_id == MINECRAFT_AIR_ITEM);
    CHECK(inventory_snapshot[36].count == 0);
    check_action(MINECRAFT_DIRT, 10, 21, 30);
    end();
}

static void test_placement_rejected(void)
{
    const int items[] = {MINECRAFT_AIR_ITEM, MINECRAFT_STICK_ITEM};
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        begin();
        if (items[i]) storageInventoryUpdateSlot(&player, 36, 2, items[i]);
        action(2, 1);
        gamePlayerLocalTick(&player);
        CHECK(!player.gamePlayerData.action_item_event);
        CHECK(!player.gamePlayerData.block_update_event);
        CHECK(blocksGetBlock(10, 21, 30) == MINECRAFT_AIR);
        CHECK(slot(&player, 36).item_id == items[i]);
        CHECK(slot(&player, 36).count == (items[i] ? 2 : 0));
        gamePlayerGlobalTickOthers(&player);
        CHECK(count(BLOCK) == 0 && count(CONTENT) == 0);
        end();
    }
}

static void test_breaking(void)
{
    const int tools[] = {MINECRAFT_AIR_ITEM, MINECRAFT_WOODEN_PICKAXE_ITEM,
        MINECRAFT_STONE_PICKAXE_ITEM, MINECRAFT_IRON_PICKAXE_ITEM, MINECRAFT_DIAMOND_PICKAXE_ITEM};
    const int blocks[] = {MINECRAFT_GRASS_BLOCK, MINECRAFT_STONE, MINECRAFT_COAL_ORE,
        MINECRAFT_IRON_ORE, MINECRAFT_DIAMOND_ORE};
    const int drops[] = {MINECRAFT_DIRT_ITEM, MINECRAFT_COBBLESTONE_ITEM, MINECRAFT_COAL_ITEM,
        MINECRAFT_RAW_IRON_ITEM, MINECRAFT_DIAMOND_ITEM};
    const int tiers[] = {0, 1, 1, 2, 3};
    for (size_t b = 0; b < sizeof(blocks) / sizeof(blocks[0]); b++)
        for (size_t t = 0; t < sizeof(tools) / sizeof(tools[0]); t++) {
            begin();
            terrain = blocks[b];
            if (tools[t]) storageInventoryUpdateSlot(&player, 36, 1, tools[t]);
            action(1, 1);
            gamePlayerLocalTick(&player);
            int found = 0;
            for (int i = 0; i < INVENTORY_SIZE; i++) {
                inventory_slots_t s = slot(&player, i);
                CHECK(s.item_id != MINECRAFT_AIR_ITEM || s.count == 0);
                if (s.item_id == drops[b]) found += s.count;
            }
            CHECK(found == ((int)t >= tiers[b] ? 1 : 0));
            if (tools[t]) CHECK(slot(&player, 36).item_id == tools[t] && slot(&player, 36).count == 1);
            CHECK(count(CONTENT) == 1);
            check_action(MINECRAFT_AIR, 10, 20, 30);
            end();
        }
}

static void expect_recipe(int window, int item, int n)
{
    gamePlayerLocalTick(&player);
    CHECK(count(SLOT) == 1);
    message result = last(SLOT);
    CHECK(result.player == &player && result.a == window && result.b == 0);
    CHECK(result.c == n && result.d == item);
    CHECK(!player.gamePlayerData.inventory_crafting_event && !player.gamePlayerData.crafting_table_event);
    gamePlayerLocalTick(&player);
    CHECK(count(SLOT) == 1);
}

static void test_crafting_inventory(void)
{
    /* A shapeless recipe must work in every position of the 2x2 grid. */
    for (int i = 1; i <= 4; i++) {
        begin();
        storageInventoryUpdateSlot(&player, (int16_t)i, 1, MINECRAFT_OAK_LOG_ITEM);
        expect_recipe(0, MINECRAFT_OAK_PLANKS_ITEM, 4);
        CHECK(slot(&player, i).count == 1); /* computing output does not consume ingredients */
        end();
    }
    for (int column = 1; column <= 2; column++) {
        begin();
        storageInventoryUpdateSlot(&player, (int16_t)column, 1, MINECRAFT_OAK_PLANKS_ITEM);
        storageInventoryUpdateSlot(&player, (int16_t)(column + 2), 1, MINECRAFT_OAK_PLANKS_ITEM);
        expect_recipe(0, MINECRAFT_STICK_ITEM, 4);
        end();
    }
    begin();
    for (int i = 1; i <= 4; i++) storageInventoryUpdateSlot(&player, (int16_t)i, 1, MINECRAFT_OAK_PLANKS_ITEM);
    expect_recipe(0, MINECRAFT_CRAFTING_TABLE_ITEM, 1);
    end();
    begin();
    storageInventoryUpdateSlot(&player, 1, 1, MINECRAFT_DIRT_ITEM);
    expect_recipe(0, MINECRAFT_AIR_ITEM, 0);
    end();
}

static void test_crafting_table(void)
{
    begin();
    terrain = MINECRAFT_CRAFTING_TABLE;
    storageInventoryUpdateSlot(&player, 9, 3, MINECRAFT_OAK_PLANKS_ITEM);
    storageInventoryUpdateSlot(&player, 44, 2, MINECRAFT_STICK_ITEM);
    action(2, 1);
    gamePlayerLocalTick(&player);
    CHECK(count(SCREEN) == 1 && last(SCREEN).a == 1 && last(SCREEN).b == WINDOW_CRAFTING);
    CHECK(last(SCREEN).player == &player);
    CHECK(count(BLOCK) == 0 && !player.gamePlayerData.block_update_event);
    inventory_slots_t *menu = player.gamePlayerData.crafting_menu;
    CHECK(menu != NULL && menu[10].item_id == MINECRAFT_OAK_PLANKS_ITEM && menu[10].count == 3);
    CHECK(menu[45].item_id == MINECRAFT_STICK_ITEM && menu[45].count == 2);
    action(2, 1);
    gamePlayerLocalTick(&player);
    CHECK(player.gamePlayerData.crafting_menu == menu); /* reuse the open menu */
    for (int i = 1; i <= 3; i++) menu[i] = (inventory_slots_t){1, MINECRAFT_OAK_PLANKS_ITEM};
    menu[5] = menu[8] = (inventory_slots_t){1, MINECRAFT_STICK_ITEM};
    message_count = 0;
    player.gamePlayerData.crafting_table_event = 1;
    expect_recipe(1, MINECRAFT_WOODEN_PICKAXE_ITEM, 1);
    menu[9] = (inventory_slots_t){1, MINECRAFT_DIRT_ITEM};
    message_count = 0;
    player.gamePlayerData.crafting_table_event = 1;
    expect_recipe(1, MINECRAFT_AIR_ITEM, 0);
    gamePlayerLeft(&player);
    CHECK(player.gamePlayerData.crafting_menu == NULL);
    gamePlayerLeft(&player);
    end();

    begin();
    terrain = MINECRAFT_CRAFTING_TABLE;
    player.sneaking = 1;
    storageInventoryUpdateSlot(&player, 36, 2, MINECRAFT_DIRT_ITEM);
    action(2, 1);
    gamePlayerLocalTick(&player);
    CHECK(count(SCREEN) == 0 && player.gamePlayerData.crafting_menu == NULL);
    check_action(MINECRAFT_DIRT, 10, 21, 30);
    end();
}

static void test_storage(void)
{
    begin();
    storageInventoryUpdateSlot(&player, 0, 2, MINECRAFT_DIRT_ITEM);
    storageInventoryUpdateSlot(&player, INVENTORY_SIZE - 1, 3, MINECRAFT_STICK_ITEM);
    storageInventoryUpdateSlot(&other, 0, 4, MINECRAFT_OAK_LOG_ITEM);
    CHECK(slot(&player, 0).count == 2 && slot(&other, 0).count == 4);
    CHECK(slot(&player, INVENTORY_SIZE - 1).item_id == MINECRAFT_STICK_ITEM);
    const int invalid[] = {-1, INVENTORY_SIZE, INVENTORY_SIZE + 1};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        storageInventoryUpdateSlot(&player, (int16_t)invalid[i], 9, MINECRAFT_DIRT_ITEM);
        CHECK(slot(&player, invalid[i]).item_id == 0 && slot(&player, invalid[i]).count == 0);
    }
    CHECK(slot(&other, 0).item_id == MINECRAFT_OAK_LOG_ITEM);
    storageInventoryUpdateSlot(&player, 0, 0, MINECRAFT_DIRT_ITEM);
    CHECK(slot(&player, 0).item_id == 0 && slot(&player, 0).count == 0);
    storageInventoryUpdateSlot(&player, 36, 6, MINECRAFT_DIRT_ITEM);
    storageInventoryInsertItem(&player, MINECRAFT_DIRT_ITEM, 2);
    CHECK(slot(&player, 36).count == 8);
    player.gamePlayerData.full_inventory_update_event = 1;
    gamePlayerLocalTick(&player);
    CHECK(count(CONTENT) == 1 && inventory_snapshot[36].count == 8);
    storageInventoryUpdateSlot(&player, 36, 1, MINECRAFT_DIRT_ITEM);
    CHECK(inventory_snapshot[36].count == 8); /* packet recorder owns a copy */
    CHECK(!player.gamePlayerData.full_inventory_update_event);
    gamePlayerLocalTick(&player);
    CHECK(count(CONTENT) == 1);
    end();
}

static void test_lifecycle(void)
{
    for (int i = 0; i < 3; i++) {
        begin();
        CHECK(slot(&player, 36).count == 0);
        storageInventoryUpdateSlot(&player, 36, 8, MINECRAFT_DIRT_ITEM);
        CHECK(blocksUpdate(MINECRAFT_DIRT, 0, 20, 0));
        player.gamePlayerData.crafting_menu = U_calloc(INVENTORY_SIZE, sizeof(inventory_slots_t));
        CHECK(player.gamePlayerData.crafting_menu != NULL);
        end();
        gameCleanup(); /* repeated cleanup must be safe */
    }
}

static void test_time(void)
{
    begin();
    const size_t second = 1000 / TICK_TIME_MS;
    for (main_tick = 1; main_tick <= 10 * second; main_tick++) gameGlobalTick();
    CHECK(count(TIME) == 0);
    gamePlayerSpawned(&player);
    CHECK(last(TIME).a == 6200);
    end();

    begin();
    active_players = 1;
    for (main_tick = 1; main_tick < 10 * second; main_tick++) gameGlobalTick();
    CHECK(count(TIME) == 0);
    gameGlobalTick();
    CHECK(count(TIME) == 1 && last(TIME).a == 6180);
    for (main_tick++; main_tick <= 900 * second; main_tick++) gameGlobalTick();
    CHECK(count(TIME) == 90);
    gamePlayerSpawned(&player);
    CHECK(last(TIME).a == 0); /* 6000 + 900 * 20 wraps at 24000 */
    end();
}

static void test_blocks(void)
{
    begin();
    CHECK(minecraft_block_state_from_item(MINECRAFT_DIRT_ITEM) == MINECRAFT_DIRT);
    CHECK(minecraft_item_from_block_state(MINECRAFT_DIRT) == MINECRAFT_DIRT_ITEM);
    terrain = MINECRAFT_STONE;
    const int coords[] = {-33, -17, -16, -1, 0, 15, 16, 31};
    for (size_t i = 0; i < sizeof(coords) / sizeof(coords[0]); i++) {
        CHECK(blocksGetBlock(coords[i], -5, coords[i]) == MINECRAFT_STONE);
        CHECK(blocksUpdate(MINECRAFT_DIRT, coords[i], -5, coords[i]));
        CHECK(blocksUpdate(MINECRAFT_AIR, coords[i], 20, coords[i]));
    }
    for (size_t i = 0; i < sizeof(coords) / sizeof(coords[0]); i++) {
        CHECK(blocksGetBlock(coords[i], -5, coords[i]) == MINECRAFT_DIRT);
        CHECK(blocksGetBlock(coords[i], 20, coords[i]) == MINECRAFT_AIR);
        CHECK(blocksUpdate(MINECRAFT_STONE, coords[i], -5, coords[i]));
        CHECK(blocksGetBlock(coords[i], -5, coords[i]) == MINECRAFT_STONE);
        CHECK(blocksGetBlock(coords[i], 20, coords[i]) == MINECRAFT_AIR);
        CHECK(blocksUpdate(MINECRAFT_STONE, coords[i], 20, coords[i]));
    }
    end();
}

int main(int argc, char **argv)
{
    const struct { const char *name; void (*run)(void); } cases[] = {
        {"spawn", test_spawn}, {"chunks", test_chunks}, {"placement", test_placement},
        {"placement_rejected", test_placement_rejected}, {"breaking", test_breaking},
        {"crafting_inventory", test_crafting_inventory}, {"crafting_table", test_crafting_table},
        {"storage", test_storage}, {"lifecycle", test_lifecycle}, {"time", test_time}, {"blocks", test_blocks}
    };
    CHECK(argc == 2);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        if (strcmp(argv[1], cases[i].name) == 0) {
            cases[i].run();
            printf("PASS %s\n", cases[i].name);
            return EXIT_SUCCESS;
        }
    fprintf(stderr, "Unknown test case: %s\n", argv[1]);
    return EXIT_FAILURE;
}
