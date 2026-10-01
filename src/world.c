#include "world.h"
#include "s2c.h"
#include "config.h"
#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"

// Chunk generation currently populates only sections 3 through 7 (inclusive).
#define WORLD_SURFACE_MIN 48 - (2 * 16)
#define WORLD_SURFACE_MAX 95 - (2 * 16)
#define SEA_LEVEL 56 - (2 * 16)

static int8_t terrain_heightmap[16][16]; // 16*16*1 = 256 bytes
static int8_t basin_depthmap[16][16];    // 16*16*1 = 256 bytes
static uint8_t decoration_map[16][16];   // 16*16*1 = 256 bytes
static uint8_t tree_origin_map[16][16];  // 16*16*1 = 256 bytes
static uint8_t cave_map[16][16];         // 16*16*1 = 256 bytes
static uint8_t ore_map[3][16][16];       // 3*16*16 = 768 bytes
static uint64_t block_data[256];         // 256*8 = 2048 bytes

static const int32_t surface_section_palette[] = {
    MINECRAFT_AIR,
    MINECRAFT_GRASS_BLOCK,
    MINECRAFT_DIRT,
    MINECRAFT_STONE,
    MINECRAFT_WATER,
    MINECRAFT_SHORT_GRASS,
    MINECRAFT_DANDELION,
    MINECRAFT_POPPY,
    MINECRAFT_WILDFLOWERS,
    MINECRAFT_OAK_LOG,
    MINECRAFT_OAK_LEAVES};
static const int32_t cave_section_palette[] = {
    MINECRAFT_AIR,
    MINECRAFT_STONE,
    MINECRAFT_COAL_ORE,
    MINECRAFT_IRON_ORE,
    MINECRAFT_DIAMOND_ORE,
    MINECRAFT_BEDROCK};

static float lerp(float a, float b, float t)
{
    return a + ((b - a) * t);
}

static int32_t clamp_i32(int32_t value, int32_t min, int32_t max)
{
    if (value < min)
    {
        return min;
    }
    if (value > max)
    {
        return max;
    }
    return value;
}

static uint8_t derive_seed(int32_t base_seed, uint32_t stream_id)
{
    uint32_t mixed = (uint32_t)base_seed;
    mixed ^= (stream_id + 1u) * 0x9E3779B9u;
    mixed ^= mixed >> 16;
    mixed *= 0x85EBCA6Bu;
    mixed ^= mixed >> 13;
    return (uint8_t)(mixed & 0xFFu);
}

static uint32_t hash_position_3d(int32_t x, int32_t y, int32_t z, uint32_t seed)
{
    uint32_t h = ((uint32_t)x * 0x27D4EB2Du) ^ ((uint32_t)y * 0x85EBCA77u) ^ ((uint32_t)z * 0x165667B1u) ^ seed;
    h ^= h >> 15;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

static void generate_terrain_heightmap(int32_t chunk_x, int32_t chunk_z, int32_t world_seed)
{
    float height_grid[5][5];
    const int32_t chunk_world_x = chunk_x * 16;
    const int32_t chunk_world_z = chunk_z * 16;
    const float continental_frequency = 0.0125f; // 1 / 80
    const float hills_frequency = 0.03125f;      // 1 / 32
    const float detail_frequency = 0.1f;         // 1 / 10
    const float mountain_frequency = 0.007f;     // 1 / 142.86
    const float ridge_frequency = 0.045f;        // 1 / 22.22
    const uint8_t continental_seed = derive_seed(world_seed, 0u);
    const uint8_t hills_seed = derive_seed(world_seed, 1u);
    const uint8_t detail_seed = derive_seed(world_seed, 2u);
    const uint8_t mountain_seed = derive_seed(world_seed, 3u);
    const uint8_t ridge_seed = derive_seed(world_seed, 4u);

    for (int32_t gz = 0; gz < 5; gz++)
    {
        for (int32_t gx = 0; gx < 5; gx++)
        {
            int32_t sample_world_x = chunk_world_x + (gx << 2);
            int32_t sample_world_z = chunk_world_z + (gz << 2);

            float continental = stb_perlin_noise3_seed(
                (float)sample_world_x * continental_frequency,
                (float)sample_world_z * continental_frequency,
                0.0f,
                0, 0, 0, continental_seed);

            float hills = stb_perlin_noise3_seed(
                (float)sample_world_x * hills_frequency,
                (float)sample_world_z * hills_frequency,
                37.0f,
                0, 0, 0, hills_seed);

            float detail = stb_perlin_noise3_seed(
                (float)sample_world_x * detail_frequency,
                (float)sample_world_z * detail_frequency,
                73.0f,
                0, 0, 0, detail_seed);

            float mountain_control = stb_perlin_noise3_seed(
                (float)sample_world_x * mountain_frequency,
                (float)sample_world_z * mountain_frequency,
                111.0f,
                0, 0, 0, mountain_seed);

            float ridge = 1.0f - fabs(stb_perlin_noise3_seed(
                                     (float)sample_world_x * ridge_frequency,
                                     (float)sample_world_z * ridge_frequency,
                                     173.0f,
                                     0, 0, 0, ridge_seed));

            float mountain_mask = 0.0f;
            if (mountain_control > 0.05f)
            {
                mountain_mask = (mountain_control - 0.05f) / 0.95f;
            }
            mountain_mask *= mountain_mask;
            float ridge_sharp = ridge * ridge;
            float mountain = mountain_mask * (12.0f + (ridge_sharp * 30.0f));

            height_grid[gz][gx] = SEA_LEVEL + (continental * 7.0f) + (hills * 4.0f) + (detail * 1.5f) + mountain;
        }
    }

    for (int32_t bz = 0; bz < 16; bz++)
    {
        int32_t gz = bz >> 2;
        float tz = (float)(bz & 0x3) * 0.25f;

        for (int32_t bx = 0; bx < 16; bx++)
        {
            int32_t gx = bx >> 2;
            float tx = (float)(bx & 0x3) * 0.25f;

            float h00 = height_grid[gz][gx];
            float h10 = height_grid[gz][gx + 1];
            float h01 = height_grid[gz + 1][gx];
            float h11 = height_grid[gz + 1][gx + 1];

            float hx0 = lerp(h00, h10, tx);
            float hx1 = lerp(h01, h11, tx);
            float blended_height = lerp(hx0, hx1, tz);

            terrain_heightmap[bz][bx] = (int8_t)clamp_i32((int32_t)blended_height, WORLD_SURFACE_MIN, WORLD_SURFACE_MAX);
        }
    }
}

static void generate_basin_depth_map(int32_t chunk_x, int32_t chunk_z, int32_t world_seed)
{
    float basin_grid[5][5];
    const int32_t chunk_world_x = chunk_x * 16;
    const int32_t chunk_world_z = chunk_z * 16;
    const float basin_frequency = 0.010f;  // 1 / 100
    const float detail_frequency = 0.035f; // 1 / 28.57
    const uint8_t basin_seed = derive_seed(world_seed, 5u);
    const uint8_t basin_detail_seed = derive_seed(world_seed, 6u);

    for (int32_t gz = 0; gz < 5; gz++)
    {
        for (int32_t gx = 0; gx < 5; gx++)
        {
            int32_t sample_world_x = chunk_world_x + (gx << 2);
            int32_t sample_world_z = chunk_world_z + (gz << 2);

            float basin_base = stb_perlin_noise3_seed(
                (float)sample_world_x * basin_frequency,
                (float)sample_world_z * basin_frequency,
                211.0f,
                0, 0, 0, basin_seed);

            float basin_detail = stb_perlin_noise3_seed(
                (float)sample_world_x * detail_frequency,
                (float)sample_world_z * detail_frequency,
                307.0f,
                0, 0, 0, basin_detail_seed);

            basin_grid[gz][gx] = (basin_base * 0.8f) + (basin_detail * 0.2f);
        }
    }

    for (int32_t bz = 0; bz < 16; bz++)
    {
        int32_t gz = bz >> 2;
        float tz = (float)(bz & 0x3) * 0.25f;

        for (int32_t bx = 0; bx < 16; bx++)
        {
            int32_t gx = bx >> 2;
            float tx = (float)(bx & 0x3) * 0.25f;

            float b00 = basin_grid[gz][gx];
            float b10 = basin_grid[gz][gx + 1];
            float b01 = basin_grid[gz + 1][gx];
            float b11 = basin_grid[gz + 1][gx + 1];

            float bx0 = lerp(b00, b10, tx);
            float bx1 = lerp(b01, b11, tx);
            float blended_basin = lerp(bx0, bx1, tz);

            float carve = 0.0f;
            if (blended_basin < 0.20f)
            {
                float t = (0.20f - blended_basin) / 1.20f;
                carve = t * t * 8.0f;
            }

            basin_depthmap[bz][bx] = (int8_t)clamp_i32((int32_t)carve, 0, 8);
        }
    }
}

static void generate_tree_origin_map(int32_t chunk_x, int32_t chunk_z, int32_t world_seed)
{
    const int32_t chunk_world_x = chunk_x * 16;
    const int32_t chunk_world_z = chunk_z * 16;
    const float forest_frequency = 0.040f; // 1 / 25
    const uint8_t forest_seed = derive_seed(world_seed, 10u);
    const uint32_t hash_seed = derive_seed(world_seed, 11u);

    memset(tree_origin_map, 0, sizeof(uint8_t) * 16 * 16);

    for (int32_t bz = 0; bz < 16; bz++)
    {
        for (int32_t bx = 0; bx < 16; bx++)
        {
            // making sure that trees dont spawn next to the chunk borders
            if (bx < 2 || bx > 13 || bz < 2 || bz > 13)
            {
                continue;
            }

            int32_t center_height = clamp_i32(terrain_heightmap[bz][bx] - basin_depthmap[bz][bx], WORLD_SURFACE_MIN, WORLD_SURFACE_MAX);
            if (center_height <= SEA_LEVEL || basin_depthmap[bz][bx] > 4)
            {
                continue;
            }

            int32_t min_height = center_height;
            int32_t max_height = center_height;
            for (int32_t nz = bz - 1; nz <= bz + 1; nz++)
            {
                for (int32_t nx = bx - 1; nx <= bx + 1; nx++)
                {
                    int32_t neighbor_height = clamp_i32(terrain_heightmap[nz][nx] - basin_depthmap[nz][nx], WORLD_SURFACE_MIN, WORLD_SURFACE_MAX);
                    if (neighbor_height < min_height)
                    {
                        min_height = neighbor_height;
                    }
                    if (neighbor_height > max_height)
                    {
                        max_height = neighbor_height;
                    }
                }
            }
            if (max_height - min_height > 1)
            {
                continue;
            }

            int32_t world_x = chunk_world_x + bx;
            int32_t world_z = chunk_world_z + bz;
            float forest_noise = stb_perlin_noise3_seed(
                (float)world_x * forest_frequency,
                (float)world_z * forest_frequency,
                853.0f,
                0, 0, 0, forest_seed);
            if (forest_noise < 0.12f)
            {
                continue;
            }

            uint32_t hash = hash_position_3d(world_x, 0, world_z, hash_seed);
            uint32_t roll = hash & 0xFFu;
            uint32_t chance_threshold = (forest_noise > 0.55f) ? 64u : 40u;
            if (roll >= chance_threshold)
            {
                continue;
            }

            int overlaps_existing_tree = 0;
            int32_t min_spacing_z = clamp_i32(bz - 3, 0, 15);
            int32_t max_spacing_z = clamp_i32(bz + 3, 0, 15);
            int32_t min_spacing_x = clamp_i32(bx - 3, 0, 15);
            int32_t max_spacing_x = clamp_i32(bx + 3, 0, 15);

            for (int32_t sz = min_spacing_z; sz <= max_spacing_z && !overlaps_existing_tree; sz++)
            {
                for (int32_t sx = min_spacing_x; sx <= max_spacing_x; sx++)
                {
                    if (tree_origin_map[sz][sx] != 0)
                    {
                        overlaps_existing_tree = 1;
                        break;
                    }
                }
            }
            if (overlaps_existing_tree)
            {
                continue;
            }

            tree_origin_map[bz][bx] = (uint8_t)(4 + ((hash >> 8) & 0x1u)); // trunk height: 4 or 5
        }
    }
}

static int get_tree_palette_for_voxel(int32_t bx, int32_t bz, int32_t world_y)
{
    int has_leaves = 0;
    int32_t min_z = clamp_i32(bz - 2, 0, 15);
    int32_t max_z = clamp_i32(bz + 2, 0, 15);
    int32_t min_x = clamp_i32(bx - 2, 0, 15);
    int32_t max_x = clamp_i32(bx + 2, 0, 15);

    for (int32_t rz = min_z; rz <= max_z; rz++)
    {
        for (int32_t rx = min_x; rx <= max_x; rx++)
        {
            uint8_t trunk_height = tree_origin_map[rz][rx];
            if (trunk_height == 0)
            {
                continue;
            }

            int32_t trunk_base_y = terrain_heightmap[rz][rx] + 1;
            int32_t trunk_top_y = trunk_base_y + (int32_t)trunk_height - 1;

            if (bx == rx && bz == rz && world_y >= trunk_base_y && world_y <= trunk_top_y)
            {
                return 9; // oak log
            }

            int32_t dx = bx - rx;
            int32_t dz = bz - rz;
            if (dx < 0)
            {
                dx = -dx;
            }
            if (dz < 0)
            {
                dz = -dz;
            }

            int32_t dy = world_y - trunk_top_y;
            if (dy == -1)
            {
                if (dx <= 2 && dz <= 2 && !(dx == 2 && dz == 2))
                {
                    has_leaves = 1;
                }
            }
            else if (dy == 0)
            {
                if (dx <= 1 && dz <= 1)
                {
                    has_leaves = 1;
                }
            }
            else if (dy == 1)
            {
                if ((dx + dz) <= 1)
                {
                    has_leaves = 1;
                }
            }
        }
    }

    return has_leaves ? 10 : 0; // oak leaves or no tree
}

static void generate_surface_decoration_map(int32_t chunk_x, int32_t chunk_z, int32_t world_seed)
{
    float density_grid[5][5];
    float type_grid[5][5];
    const int32_t chunk_world_x = chunk_x * 16;
    const int32_t chunk_world_z = chunk_z * 16;
    const float density_frequency = 0.070f; // 1 / 14.29
    const float type_frequency = 0.030f;    // 1 / 33.33
    const float detail_frequency = 0.180f;  // 1 / 5.56
    const uint8_t density_seed = derive_seed(world_seed, 7u);
    const uint8_t type_seed = derive_seed(world_seed, 8u);
    const uint8_t detail_seed = derive_seed(world_seed, 9u);

    for (int32_t gz = 0; gz < 5; gz++)
    {
        for (int32_t gx = 0; gx < 5; gx++)
        {
            int32_t sample_world_x = chunk_world_x + (gx << 2);
            int32_t sample_world_z = chunk_world_z + (gz << 2);

            float density_base = stb_perlin_noise3_seed(
                (float)sample_world_x * density_frequency,
                (float)sample_world_z * density_frequency,
                503.0f,
                0, 0, 0, density_seed);

            float density_detail = stb_perlin_noise3_seed(
                (float)sample_world_x * detail_frequency,
                (float)sample_world_z * detail_frequency,
                557.0f,
                0, 0, 0, detail_seed);

            density_grid[gz][gx] = (density_base * 0.80f) + (density_detail * 0.20f);

            type_grid[gz][gx] = stb_perlin_noise3_seed(
                (float)sample_world_x * type_frequency,
                (float)sample_world_z * type_frequency,
                601.0f,
                0, 0, 0, type_seed);
        }
    }

    for (int32_t bz = 0; bz < 16; bz++)
    {
        int32_t gz = bz >> 2;
        float tz = (float)(bz & 0x3) * 0.25f;

        for (int32_t bx = 0; bx < 16; bx++)
        {
            int32_t gx = bx >> 2;
            float tx = (float)(bx & 0x3) * 0.25f;

            float d00 = density_grid[gz][gx];
            float d10 = density_grid[gz][gx + 1];
            float d01 = density_grid[gz + 1][gx];
            float d11 = density_grid[gz + 1][gx + 1];

            float t00 = type_grid[gz][gx];
            float t10 = type_grid[gz][gx + 1];
            float t01 = type_grid[gz + 1][gx];
            float t11 = type_grid[gz + 1][gx + 1];

            float dx0 = lerp(d00, d10, tx);
            float dx1 = lerp(d01, d11, tx);
            float blended_density = lerp(dx0, dx1, tz);

            float tx0 = lerp(t00, t10, tx);
            float tx1 = lerp(t01, t11, tx);
            float blended_type = lerp(tx0, tx1, tz);

            if (blended_density < 0.45f)
            {
                decoration_map[bz][bx] = 0;
            }
            else if (blended_type < 0.27f)
            {
                decoration_map[bz][bx] = 5; // short grass
            }
            else if (blended_type < 0.50f)
            {
                decoration_map[bz][bx] = 6; // dandelion
            }
            else if (blended_type < 0.82f)
            {
                decoration_map[bz][bx] = 7; // poppy
            }
            else
            {
                decoration_map[bz][bx] = 8; // wildflowers
            }
        }
    }
}

static void generate_cave_map(int32_t chunk_x, int32_t chunk_z, int32_t world_seed)
{
    float cave_base_grid[5][5];
    float cave_detail_grid[5][5];
    const float base_frequency = 0.020f;
    const float detail_frequency = 0.075f;
    const uint8_t cave_base_seed = derive_seed(world_seed, 11u);
    const uint8_t cave_detail_seed = derive_seed(world_seed, 12u);

    for (int32_t gz = 0; gz < 5; gz++)
    {
        for (int32_t gx = 0; gx < 5; gx++)
        {
            int32_t sample_world_x = (chunk_x * 16) + (gx << 2);
            int32_t sample_world_z = (chunk_z * 16) + (gz << 2);

            cave_base_grid[gz][gx] = stb_perlin_noise3_seed(
                (float)sample_world_x * base_frequency,
                (float)sample_world_z * base_frequency,
                0,
                0, 0, 0, cave_base_seed);

            cave_detail_grid[gz][gx] = stb_perlin_noise3_seed(
                (float)sample_world_x * detail_frequency,
                (float)sample_world_z * detail_frequency,
                211.0f,
                0, 0, 0, cave_detail_seed);
        }
    }

    for (int32_t bz = 0; bz < 16; bz++)
    {
        int32_t gz = bz >> 2;
        float tz = (float)(bz & 0x3) * 0.25f;

        for (int32_t bx = 0; bx < 16; bx++)
        {
            int32_t gx = bx >> 2;
            float tx = (float)(bx & 0x3) * 0.25f;

            float b00 = cave_base_grid[gz][gx];
            float b10 = cave_base_grid[gz][gx + 1];
            float b01 = cave_base_grid[gz + 1][gx];
            float b11 = cave_base_grid[gz + 1][gx + 1];

            float d00 = cave_detail_grid[gz][gx];
            float d10 = cave_detail_grid[gz][gx + 1];
            float d01 = cave_detail_grid[gz + 1][gx];
            float d11 = cave_detail_grid[gz + 1][gx + 1];

            float bx0 = lerp(b00, b10, tx);
            float bx1 = lerp(b01, b11, tx);
            float blended_base = lerp(bx0, bx1, tz);

            float dx0 = lerp(d00, d10, tx);
            float dx1 = lerp(d01, d11, tx);
            float blended_detail = lerp(dx0, dx1, tz);

            float warped_base = blended_base + (blended_detail * 0.35f);
            float cave_band = 1.0f - (float)(fabs(warped_base) / 0.30f);
            if (cave_band < 0.0f)
            {
                cave_band = 0.0f;
            }

            float detail_mask = (blended_detail * 0.5f) + 0.5f;
            float cave_strength = cave_band * (0.70f + (detail_mask * 0.30f));
            cave_map[bz][bx] = (uint8_t)clamp_i32((int32_t)(cave_strength * 255.0f), 0, 255);
        }
    }
}
static void generate_ore_map(int32_t chunk_x, int32_t chunk_z, int32_t world_seed)
{
    float coal_grid[5][5];
    float iron_grid[5][5];
    float diamond_grid[5][5];
    const float coal_frequency = 0.065f;
    const float iron_frequency = 0.048f;
    const float diamond_frequency = 0.015f;
    const float detail_frequency = 0.110f;
    const uint8_t coal_seed = derive_seed(world_seed, 13u);
    const uint8_t iron_seed = derive_seed(world_seed, 14u);
    const uint8_t diamond_seed = derive_seed(world_seed, 15u);
    const uint8_t detail_seed = derive_seed(world_seed, 16u);

    for (int32_t gz = 0; gz < 5; gz++)
    {
        for (int32_t gx = 0; gx < 5; gx++)
        {
            int32_t sample_world_x = (chunk_x * 16) + (gx << 2);
            int32_t sample_world_z = (chunk_z * 16) + (gz << 2);

            float detail_noise = stb_perlin_noise3_seed(
                (float)sample_world_x * detail_frequency,
                (float)sample_world_z * detail_frequency,
                719.0f,
                0, 0, 0, detail_seed);

            float coal_base = stb_perlin_noise3_seed(
                (float)sample_world_x * coal_frequency,
                (float)sample_world_z * coal_frequency,
                607.0f,
                0, 0, 0, coal_seed);

            float iron_base = stb_perlin_noise3_seed(
                (float)sample_world_x * iron_frequency,
                (float)sample_world_z * iron_frequency,
                631.0f,
                0, 0, 0, iron_seed);

            float diamond_base = stb_perlin_noise3_seed(
                (float)sample_world_x * diamond_frequency,
                (float)sample_world_z * diamond_frequency,
                661.0f,
                0, 0, 0, diamond_seed);

            coal_grid[gz][gx] = (coal_base * 0.80f) + (detail_noise * 0.20f);
            iron_grid[gz][gx] = (iron_base * 0.75f) + (detail_noise * 0.25f);
            diamond_grid[gz][gx] = (diamond_base * 0.83f) + (detail_noise * 0.07f);
        }
    }

    for (int32_t bz = 0; bz < 16; bz++)
    {
        int32_t gz = bz >> 2;
        float tz = (float)(bz & 0x3) * 0.25f;

        for (int32_t bx = 0; bx < 16; bx++)
        {
            int32_t gx = bx >> 2;
            float tx = (float)(bx & 0x3) * 0.25f;

            float c00 = coal_grid[gz][gx];
            float c10 = coal_grid[gz][gx + 1];
            float c01 = coal_grid[gz + 1][gx];
            float c11 = coal_grid[gz + 1][gx + 1];

            float i00 = iron_grid[gz][gx];
            float i10 = iron_grid[gz][gx + 1];
            float i01 = iron_grid[gz + 1][gx];
            float i11 = iron_grid[gz + 1][gx + 1];

            float d00 = diamond_grid[gz][gx];
            float d10 = diamond_grid[gz][gx + 1];
            float d01 = diamond_grid[gz + 1][gx];
            float d11 = diamond_grid[gz + 1][gx + 1];

            float cx0 = lerp(c00, c10, tx);
            float cx1 = lerp(c01, c11, tx);
            float coal_strength = lerp(cx0, cx1, tz);

            float ix0 = lerp(i00, i10, tx);
            float ix1 = lerp(i01, i11, tx);
            float iron_strength = lerp(ix0, ix1, tz);

            float dx0 = lerp(d00, d10, tx);
            float dx1 = lerp(d01, d11, tx);
            float diamond_strength = lerp(dx0, dx1, tz);

            float coal_mask = ((coal_strength * 0.5f) + 0.5f - 0.52f) / 0.48f;
            float iron_mask = ((iron_strength * 0.5f) + 0.5f - 0.57f) / 0.43f;
            float diamond_mask = ((diamond_strength * 0.5f) + 0.5f - 0.58f) / 0.42f;

            ore_map[0][bz][bx] = (uint8_t)clamp_i32((int32_t)(coal_mask * 255.0f), 0, 255);
            ore_map[1][bz][bx] = (uint8_t)clamp_i32((int32_t)(iron_mask * 255.0f), 0, 255);
            ore_map[2][bz][bx] = (uint8_t)clamp_i32((int32_t)(diamond_mask * 255.0f), 0, 255);
        }
    }
}

static int get_surface_block_palette(uint8_t local_chunk_x, uint8_t local_chunk_z, int32_t world_y)
{
    int palette = 0;
    int8_t terrain_height = terrain_heightmap[local_chunk_z][local_chunk_x];
    uint8_t decoration = decoration_map[local_chunk_z][local_chunk_x];
    int32_t depth = terrain_height - world_y;
    if ((world_y >= ((5 << 4) + 0) - 64) && (world_y < ((5 << 4) + 16) - 64)) // section 5: surface level
    {

        if (depth == 0)
        {
            palette = (terrain_height >= SEA_LEVEL) ? 1 : 2; // underwater surface becomes dirt
        }
        else if (depth > 0 && depth <= 2)
        {
            palette = 2; // dirt just below the top
        }
        else if (depth > 2)
        {
            palette = 3; // stone inside terrain
        }
        else if (world_y > terrain_height && world_y <= SEA_LEVEL)
        {
            palette = 4; // water above low terrain
        }
        else if (world_y == terrain_height + 1 && terrain_height >= SEA_LEVEL && decoration != 0)
        {
            palette = (int)decoration; // surface decoration above grass
        }

        int tree_palette = get_tree_palette_for_voxel(local_chunk_x, local_chunk_z, world_y);
        if (tree_palette != 0)
        {
            palette = tree_palette;
        }
    }
    else if ((world_y >= ((6 << 4) + 0) - 64) && (world_y < ((6 << 4) + 16) - 64)) // section 6: surface level + 1 (hills, mountains)
    {
        if (depth == 0)
        {
            palette = 1; // grass block on the top
        }
        else if (depth > 0 && depth <= 2)
        {
            palette = 2; // dirt just below the top
        }
        else if (depth > 2)
        {
            palette = 3; // stone inside hills/mountains
        }
        else if (world_y == terrain_height + 1 && terrain_height >= SEA_LEVEL && decoration != 0)
        {
            palette = (int)decoration; // surface decoration above grass
        }

        int tree_palette = get_tree_palette_for_voxel(local_chunk_x, local_chunk_z, world_y);
        if (tree_palette != 0)
        {
            palette = tree_palette;
        }
    }
    else if ((world_y >= ((7 << 4) + 0) - 64) && (world_y < ((7 << 4) + 16) - 64)) // section 7: mountain continuation
    {

        if (depth == 0)
        {
            palette = 1; // grass on mountain top
        }
        else if (depth > 0 && depth <= 2)
        {
            palette = 2; // topsoil
        }
        else if (depth > 2)
        {
            palette = 3; // mountain stone mass
        }
        else if (world_y == terrain_height + 1 && terrain_height >= SEA_LEVEL && decoration != 0)
        {
            palette = (int)decoration; // seeded surface decoration above grass
        }

        int tree_palette = get_tree_palette_for_voxel(local_chunk_x, local_chunk_z, world_y);
        if (tree_palette != 0)
        {
            palette = tree_palette;
        }
    }
    return palette;
}

static int get_cave_block_palette(uint8_t local_chunk_x, uint8_t local_chunk_z, int32_t world_y)
{
    int palette = 0;
    // TODO: simplify this later
    if ((world_y >= ((3 << 4) + 0) - 64) && (world_y < ((3 << 4) + 16) - 64)) // section 3: boundry
    {
        if ((world_y + 64) - (3 << 4) == 13)
        {
            palette = 5; // bedrock (layer 14)
        }
        else if ((world_y + 64) - (3 << 4) >= 14)
        {
            palette = 1; // stone (layers 15 and 16)
        }
    }
    else if ((world_y >= ((4 << 4) + 0) - 64) && (world_y < ((4 << 4) + 16) - 64)) // section 4: cave level
    {
        uint8_t threshold = (uint8_t)lerp(145.0f, 85.0f, 1.0f - (world_y * 0.1f)); // pointiness factor

        if (cave_map[local_chunk_z][local_chunk_x] < threshold)
        {
            int32_t coal_score = ore_map[0][local_chunk_z][local_chunk_x] + ((world_y * 40) / 15);
            int32_t iron_score = ore_map[1][local_chunk_z][local_chunk_x] + (52 * (world_y) / 8);
            int32_t diamond_score = ore_map[2][local_chunk_z][local_chunk_x] + (((15 - world_y) * 70) / 15);

            palette = 1; // stone

            if (diamond_score > 152)
            {
                palette = 4; // diamond
            }
            else if (iron_score > 140)
            {
                palette = 3; // iron
            }
            else if (coal_score > 130)
            {
                palette = 2; // coal
            }
        }
    }
    return palette;
}

static void populate_surface_sections(int32_t section_y)
{
    int16_t block_count = 0;
    int16_t fluid_count = 0;
    switch (section_y)
    {
    case 5: // surface level
    case 6: // surface level + 1 (hills, mountains)
    case 7: // mountain continuation
    {
        for (int by = 0; by < 16; by++)
        {
            int32_t world_y = ((section_y << 4) + by) - 64;
            for (int bz = 0; bz < 16; bz++)
            {
                for (int bx = 0; bx < 16; bx++)
                {
                    int palette = get_surface_block_palette(bx, bz, world_y);
                    if (palette == 4)
                    {
                        fluid_count++; // water
                    }
                    if (palette != 0)
                    {
                        int index = (by << 8) | (bz << 4) | bx;
                        uint8_t data_index = index >> 4;
                        int bit_index = (index & 0xF) << 2;
                        block_data[data_index] |= (uint64_t)palette << bit_index;
                        block_count++;
                    }
                }
            }
        }
        sendShort(block_count); // block count
        sendShort(fluid_count); // fluid count

        sendByte(4); // blocks per entry
        sendVarInt(sizeof(surface_section_palette) / sizeof(surface_section_palette[0]));
        for (size_t palette_idx = 0; palette_idx < (sizeof(surface_section_palette) / sizeof(surface_section_palette[0])); palette_idx++)
        {
            sendVarInt(surface_section_palette[palette_idx]);
        }

        for (size_t j = 0; j < 256; j++)
        {
            sendLong((int64_t)block_data[j]);
        }
        sendByte(0);   // biome singular
        sendVarInt(0); // biome id
        break;
    }
    }
}

static void populate_cave_sections(int32_t chunk_x, int32_t chunk_z, int32_t section_y)
{
    uint16_t block_count = 0;
    switch (section_y)
    {
    case 3: // boundry
    case 4: // cave level
        block_count = 0;

        for (int by = 0; by < 16; by++)
        {
            int32_t world_y = ((section_y << 4) + by) - 64;
            for (int bz = 0; bz < 16; bz++)
            {
                for (int bx = 0; bx < 16; bx++)
                {
                    int palette = get_cave_block_palette(bx, bz, world_y);

                    if (palette != 0)
                    {
                        int index = (by << 8) | (bz << 4) | bx;
                        uint8_t data_index = index >> 4;
                        int bit_index = (index & 0xF) << 2;
                        block_data[data_index] |= (uint64_t)palette << bit_index;
                        block_count++;
                    }
                }
            }
        }
        sendShort((int16_t)block_count); // block count
        sendShort(0);                    // fluid count

        sendByte(4); // blocks per entry
        sendVarInt(sizeof(cave_section_palette) / sizeof(cave_section_palette[0]));
        for (size_t palette_idx = 0; palette_idx < (sizeof(cave_section_palette) / sizeof(cave_section_palette[0])); palette_idx++)
        {
            sendVarInt(cave_section_palette[palette_idx]);
        }

        for (size_t j = 0; j < 256; j++)
        {
            sendLong((int64_t)block_data[j]);
        }
        sendByte(0);   // biome singular
        sendVarInt(0); // biome id
        break;
    }
}
static void generate_surface_heightmaps(int32_t chunk_x, int32_t chunk_z)
{
    generate_terrain_heightmap(chunk_x, chunk_z, WORLD_SEED);
    generate_basin_depth_map(chunk_x, chunk_z, WORLD_SEED);
    generate_tree_origin_map(chunk_x, chunk_z, WORLD_SEED);
    generate_surface_decoration_map(chunk_x, chunk_z, WORLD_SEED);

    for (int32_t bz = 0; bz < 16; bz++)
    {
        for (int32_t bx = 0; bx < 16; bx++)
        {
            int32_t carved_height = terrain_heightmap[bz][bx] - basin_depthmap[bz][bx];
            terrain_heightmap[bz][bx] = (int8_t)clamp_i32(carved_height, WORLD_SURFACE_MIN, WORLD_SURFACE_MAX);
        }
    }
}
static void generate_cave_heightmaps(int32_t chunk_x, int32_t chunk_z)
{
    generate_cave_map(chunk_x, chunk_z, WORLD_SEED);
    generate_ore_map(chunk_x, chunk_z, WORLD_SEED);
}

int worldGetBlock(int32_t x, int32_t y, int32_t z)
{
    if (y < -64 || y >= 320) return MINECRAFT_AIR;
    // TODO: Add caching later
    int palette;
    int32_t section_y = (y + 64) >> 4;
    int32_t chunk_x = x >> 4;
    int32_t chunk_z = z >> 4;
    uint8_t local_chunk_x = (uint8_t)((uint32_t)x & 15u);
    uint8_t local_chunk_z = (uint8_t)((uint32_t)z & 15u);

    if (section_y >= 5 && section_y <= 7)
    {
        generate_surface_heightmaps(chunk_x, chunk_z);
        palette = get_surface_block_palette(local_chunk_x, local_chunk_z, y);
        if (palette >= 0 && palette < (int)(sizeof(surface_section_palette) / sizeof(surface_section_palette[0])))
        {
            return surface_section_palette[palette];
        }
        return MINECRAFT_AIR;
    }

    if (section_y >= 3 && section_y <= 4)
    {
        generate_cave_heightmaps(chunk_x, chunk_z);
        palette = get_cave_block_palette(local_chunk_x, local_chunk_z, y);
        if (palette >= 0 && palette < (int)(sizeof(cave_section_palette) / sizeof(cave_section_palette[0])))
        {
            return cave_section_palette[palette];
        }
        return MINECRAFT_AIR;
    }

    return MINECRAFT_AIR;
}

void worldGenerateChunk(int32_t chunk_x, int32_t chunk_z, size_t from, size_t to)
{

    if (from > to || to >= 24)
    {
        return;
    }
    generate_surface_heightmaps(chunk_x, chunk_z);
    generate_cave_heightmaps(chunk_x, chunk_z);

    for (size_t i = 0; i < 24; i++)
    {
        if (i >= from && i <= to && i >= 3 && i <= 7)
        {
            memset(block_data, 0, sizeof(block_data));
            populate_cave_sections(chunk_x, chunk_z, i);
            populate_surface_sections(i);
        }
        else
        {
            sendShort(0);              // block count
            sendShort(0);              // fluid count
            sendByte(0);               // blocks singular
            sendVarInt(MINECRAFT_AIR); // block id
            sendByte(0);               // biome singular
            sendVarInt(0);             // biome id
        }
    }
    sendByte(0);
    sendByte(0);
    sendByte(0);
    sendVarInt(0);
}
