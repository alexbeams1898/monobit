#include "WorldInit.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"

namespace world_init
{
entt::entity spawnPlayer(EntityManager& em, const PlayerConfig& cfg)
{
    auto& reg = em.registry();
    const entt::entity player = reg.create();

    const float cx = static_cast<float>(em.tile_map.width * em.tile_map.tile_size) * 0.5f;
    const float cy = static_cast<float>(em.tile_map.height * em.tile_map.tile_size) * 0.5f;

    reg.emplace<Transform>(player, Transform{cx, cy});
    reg.emplace<PreviousTransform>(player, PreviousTransform{cx, cy});
    reg.emplace<Velocity>(player);
    // Foot collider: a small box at the sprite's base (narrower than the art frame) so the
    // character tucks behind objects and Y-sorts by where it stands. Sized in player.json.
    reg.emplace<Collider>(player, Collider{cfg.collider_w, cfg.collider_h, true});

    // Animated sprite sheet. RenderSystem draws the cell the Animation selects
    // (dir + frame); src_w/h are the cell size. NO sort_anchor override: with a
    // Collider present, RenderSystem sorts by the FEET's world Y (drawY +
    // collider.height/2), so the player orders correctly against other layer-2
    // sprites (tree-canopy props, NPCs) by where it stands. (The old override set a
    // CONSTANT 6 -- a latent bug that only surfaced once other layer-2 props existed:
    // sort_anchor is an ABSOLUTE world-Y key, not the feet offset the value implied.)
    Sprite spr{};
    spr.texture_path = cfg.texture;
    spr.src_w = cfg.frame_width;
    spr.src_h = cfg.frame_height;
    spr.layer = 2; // characters layer (above ground tiles)
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

    // Facing drives the walk animation's direction. With this present, AnimationSystem snaps
    // the sprite's cardinal from render_dx/dy through snapFacing (hysteresis) -- the ONE writer
    // of anim.dir, and the anti-jitter path. Without it, AnimationSystem falls back to raw
    // velocity, which flips cardinals frame-to-frame when collision oscillates the velocity
    // near a corner. The game sets render_dx/dy from move intent each frame (see GameLoop).
    // Start facing south, matching the idle pose.
    FacingDirection facing{};
    facing.dy = 1.0f;
    facing.render_dy = 1.0f;
    reg.emplace<FacingDirection>(player, facing);

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
