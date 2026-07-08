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

// Placeholder player box. 32x64 art bounds; the collider is a small foot box so
// the sprite tucks behind objects and Y-sorts by the feet.
constexpr int kPlayerArtW = 32;
constexpr int kPlayerArtH = 64;
constexpr float kPlayerFootW = 24.0f;
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

entt::entity spawnPlayer(EntityManager& em)
{
    auto& reg = em.registry();
    const entt::entity player = reg.create();

    const float cx = static_cast<float>(em.tile_map.width * em.tile_map.tile_size) * 0.5f;
    const float cy = static_cast<float>(em.tile_map.height * em.tile_map.tile_size) * 0.5f;

    reg.emplace<Transform>(player, Transform{cx, cy});
    reg.emplace<PreviousTransform>(player, PreviousTransform{cx, cy});
    reg.emplace<Velocity>(player);
    reg.emplace<Collider>(player, Collider{kPlayerFootW, kPlayerFootH, true});

    // The protagonist sprite sheet (4 dirs x 4 walk frames, 32x64 cells).
    // With an Animation component, RenderSystem draws the cell the animation
    // selects (dir + frame); src_w/h define the cell size. sort_anchor at the
    // feet so Y-sort orders it by where it stands, not its top.
    Sprite spr{};
    spr.texture_path = "assets/sprites/player_walk.png";
    spr.src_w = kPlayerArtW;
    spr.src_h = kPlayerArtH;
    spr.layer = 2; // characters layer (above ground tiles)
    spr.use_sort_anchor = true;
    spr.sort_anchor = kPlayerFootH * 0.5f;
    reg.emplace<Sprite>(player, spr);

    // Sheet layout: one Walk row, 4 directions, 4 frames each. Column selected
    // by the engine as dir_index * max_frames + frame_index -- matches how the
    // sheet was assembled (see scripts / assets/sprites/player_walk.png).
    // Row 0 = Walk (4 frames/dir), row 1 = Idle (1 standing frame/dir). Column
    // stride is max_frames_per_state (4) so both rows align to dir*4 + frame.
    Animation anim{};
    anim.frame_width = kPlayerArtW;
    anim.frame_height = kPlayerArtH;
    anim.max_frames_per_state = 4;
    anim.row_count = 2;
    anim.direction_count = 4;
    anim.current_row = 1; // start idle (standing)
    anim.current_frames = 1;
    anim.current_duration = 0.0f;
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
