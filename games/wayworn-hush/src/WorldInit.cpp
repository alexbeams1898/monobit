#include "WorldInit.h"

#include "Observations.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <string>

namespace world_init
{
namespace
{
// Placeholder tile IDs. Real tile definitions (and CC0 art) arrive later; for now
// each observable renders as what it IS (by its `kind`), so the world matches the
// data -- one source of truth (config/observations.json), no hand-synced tiles.
constexpr int kGrass = 0;    // walkable interior
constexpr int kRock = 1;     // solid stone (kind: inanimate)
constexpr int kChannel = 2;  // a dry streambed -- WALKABLE (kind: water)
constexpr int kClearing = 3; // an open quiet patch -- WALKABLE (kind: clearing)
constexpr int kRuin = 4;     // hand-squared fieldstone (kind: remains)

constexpr int kRegionW = 48;
constexpr int kRegionH = 48;

// Map an observable's `kind` to its placeholder tile (id + walkable). Unknown
// kinds fall back to a solid rock so a new observable is at least visible.
struct KindTile
{
    int id;
    bool walkable;
};
KindTile tileForKind(const std::string& kind)
{
    if (kind == "water")
        return {kChannel, true};
    if (kind == "clearing")
        return {kClearing, true};
    if (kind == "remains")
        return {kRuin, false};
    return {kRock, false}; // inanimate / unknown
}

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

    // Flat-color visuals (no atlas yet), one per tile id. Muted per the aesthetic
    // placeholder register -- superseded by CC0/authored art later. Observable
    // tiles themselves are stamped by placeObservableTiles() from the config, so
    // there is ONE source of truth (config/observations.json) for where they are.
    TileConfig& cfg = em.tile_config;
    cfg = TileConfig{};
    cfg.tiles[kGrass] = {"", true};
    cfg.tiles[kRock] = {"", false};
    cfg.tiles[kChannel] = {"", true};
    cfg.tiles[kClearing] = {"", true};
    cfg.tiles[kRuin] = {"", false};
    cfg.tile_visuals[kGrass] = {0, 0, 0.36f, 0.44f, 0.31f};    // muted grass green
    cfg.tile_visuals[kRock] = {0, 0, 0.34f, 0.35f, 0.38f};     // cool stone grey
    cfg.tile_visuals[kChannel] = {0, 0, 0.30f, 0.38f, 0.46f};  // dry-channel blue-grey
    cfg.tile_visuals[kClearing] = {0, 0, 0.48f, 0.55f, 0.40f}; // brighter open green
    cfg.tile_visuals[kRuin] = {0, 0, 0.52f, 0.47f, 0.38f};     // weathered sandstone
}

void placeObservableTiles(EntityManager& em, const observations::State& obs)
{
    // Stamp a tile per authored observable at its config coords, chosen by kind.
    // Config is the single source of truth: add an observable and its tile appears
    // -- no hand-synced WorldInit entry (the ruin's invisibility was exactly that
    // dual-source bug). Hidden observables (visible_when) still get a tile; the
    // glimmer/observe logic gates interaction, not the terrain.
    TileMap& map = em.tile_map;
    const float ts = static_cast<float>(map.tile_size);
    for (const auto& o : obs.observables)
    {
        const int c = static_cast<int>(o.x / ts);
        const int r = static_cast<int>(o.y / ts);
        if (c <= 0 || r <= 0 || c >= map.width - 1 || r >= map.height - 1)
            continue; // skip anything on/outside the border ring
        const KindTile kt = tileForKind(o.kind);
        map.at(c, r) = TileMap::Tile{kt.id, kt.walkable};
    }
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
