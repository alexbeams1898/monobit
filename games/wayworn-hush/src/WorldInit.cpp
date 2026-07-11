#include "WorldInit.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"

namespace world_init
{
namespace
{
// Placeholder tile IDs. Real tile definitions arrive with the LDtk importer.
constexpr int kGrass = 0; // walkable interior
constexpr int kRock = 1;  // solid

constexpr int kRegionW = 48;
constexpr int kRegionH = 48;

// Foot collider: a small box at the sprite's base so the character tucks behind
// objects and Y-sorts by where it stands. Sized to the humanoid's feet (much
// narrower than the 64px art frame). Fixed geometry.
constexpr float kPlayerFootW = 22.0f;
constexpr float kPlayerFootH = 12.0f;
} // namespace

void buildPlaceholderRegion(EntityManager& em)
{
    TileMap& map = em.tile_map;
    map.tile_size = 32;
    map.width = kRegionW;
    map.height = kRegionH;
    map.tiles.assign(static_cast<std::size_t>(kRegionW) * static_cast<std::size_t>(kRegionH),
                     TileMap::Tile{kGrass, true});

    // Solid border ring; a couple of interior rocks so depth/collision have
    // something to read against once the player exists.
    auto setTile = [&](int c, int r, int id, bool walkable)
    { map.at(c, r) = TileMap::Tile{id, walkable}; };

    for (int r = 0; r < kRegionH; ++r)
        for (int c = 0; c < kRegionW; ++c)
            if (r == 0 || c == 0 || r == kRegionH - 1 || c == kRegionW - 1)
                setTile(c, r, kRock, false);

    setTile(14, 12, kRock, false);
    setTile(30, 20, kRock, false);
    setTile(20, 32, kRock, false);

    // Flat-color visuals (no atlas yet). Muted greens/greys per the aesthetic
    // placeholder register -- superseded by an authored tileset later.
    TileConfig& cfg = em.tile_config;
    cfg = TileConfig{};
    cfg.tiles[kGrass] = {"", true};
    cfg.tiles[kRock] = {"", false};
    cfg.tile_visuals[kGrass] = {0, 0, 0.36f, 0.44f, 0.31f}; // muted grass green
    cfg.tile_visuals[kRock] = {0, 0, 0.34f, 0.35f, 0.38f};  // cool stone grey
}

entt::entity spawnPlayer(EntityManager& em, const PlayerConfig& cfg)
{
    auto& reg = em.registry();
    const entt::entity player = reg.create();

    const float cx = static_cast<float>(em.tile_map.width * em.tile_map.tile_size) * 0.5f;
    const float cy = static_cast<float>(em.tile_map.height * em.tile_map.tile_size) * 0.5f;

    reg.emplace<Transform>(player, Transform{cx, cy});
    reg.emplace<PreviousTransform>(player, PreviousTransform{cx, cy});
    reg.emplace<Velocity>(player);
    reg.emplace<Collider>(player, Collider{kPlayerFootW, kPlayerFootH, true});

    // Animated sprite sheet. RenderSystem draws the cell the Animation selects
    // (dir + frame); src_w/h are the cell size. sort_anchor at the feet so
    // Y-sort orders the sprite by where it stands, not its top.
    Sprite spr{};
    spr.texture_path = cfg.texture;
    spr.src_w = cfg.frame_width;
    spr.src_h = cfg.frame_height;
    spr.layer = 2; // characters layer (above ground tiles)
    spr.use_sort_anchor = true;
    spr.sort_anchor = kPlayerFootH * 0.5f;
    reg.emplace<Sprite>(player, spr);

    // Column = dir_index * max_frames_per_state + frame_index, so every state
    // row aligns to the same per-direction stride. Starts idle (standing).
    Animation anim{};
    anim.frame_width = cfg.frame_width;
    anim.frame_height = cfg.frame_height;
    anim.max_frames_per_state = cfg.max_frames_per_state;
    anim.direction_count = cfg.direction_count;
    anim.row_count = 2;
    anim.current_row = cfg.idle.row;
    anim.current_frames = cfg.idle.frames;
    anim.current_duration = cfg.idle.duration;
    reg.emplace<Animation>(player, anim);

    Camera cam{};
    cam.x = cx;
    cam.y = cy;
    cam.prev_x = cx;
    cam.prev_y = cy;
    cam.active = true;
    reg.emplace<Camera>(player, cam);

    return player;
}
} // namespace world_init
