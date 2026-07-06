#pragma once

// ---------------------------------------------------------------------------
// Debug flags — session-only toggles for diagnostic logs, overlays, and
// visualization helpers. Distinct from Tunables (which holds gameplay-feel
// numbers that ship to players and serialize to disk):
//
//   * Tunables : floats/ints the player or designer wants to tune. Saved to
//                config/tunables.json. Survives across runs.
//   * Flags    : bools developers flip while diagnosing a bug. Never saved.
//                Reset to defaults at every launch. Renders in a separate
//                "Debug" section of the F1 panel so it can't be confused
//                with shipping config.
//
// Why the split? Three reasons (per the conversation that motivated this
// refactor):
//   1. Conceptual leak: a designer scrolling the F1 panel shouldn't see
//      debug_collision_log next to walk_speed -- it implies the debug toggle
//      is part of the game.
//   2. Shipping cost: every `if (tun.debug_X)` read is a memory load on
//      the hot path that production builds shouldn't pay for. Centralizing
//      under Flags lets a future #ifdef ENABLE_DEV_FLAGS strip the entire
//      struct from release builds without breaking Tunables serialization.
//   3. Save-file stability: adding/removing a debug toggle today changes
//      the Tunables NLOHMANN macro, which subtly affects what tunables.json
//      reads back. Flags being session-only means churning them costs
//      nothing at the file-format layer.
//
// Add a new debug toggle:
//   1. Add the bool here with a comment explaining what it logs.
//   2. Add a checkbox in selva::ui::renderDebugFlagsPanel() (TuningPanel.cpp).
//   3. Read it as `selva::debug::flags().<name>` in the hot-path gate.
// No serialization plumbing; no JSON; no test changes.
// ---------------------------------------------------------------------------

namespace selva::debug
{

struct Flags
{
    // ---- AI ----
    // F1-toggleable debug overlay: draw vision cone + awareness label
    // above each AI actor.
    bool ai_perception = false;

    // Log each [ai-tick] firing so you can verify the scheduling math from
    // combat-debug.log. Off in normal play; on when working on AI infra.
    bool ai_tick_log = false;

    // Log [ai-decision] lines (one per decision tick) with awareness,
    // target pos, intent. Useful while iterating on behavior trees.
    bool ai_decision_log = false;

    // Catch-all for enemy lifecycle stderr noise: spawn / reset-cycle /
    // archetype-swap / hazard-violation / wolf-state / wolf-motion /
    // hitbox-update / scripted-death / PoseSampler bind-loco / hip-
    // classify / loco-splice. All silent by default; flip when diagnosing
    // animation or AI lifecycle issues.
    bool enemy_lifecycle = false;

    // ---- Animation / FPV ----
    // render/WorldRenderer.cpp writes per-frame FPV camera + head bone
    // state to fpv-roll-debug.log during rolls (and ~1s after).
    bool fpv_roll_log = false;

    // gameplay/Footsteps.cpp opens footstep-debug.log and writes per-frame
    // trajectory rows + FIRE/SUPPRESS events. The per-frame fprintf is
    // hot enough to cost a frame or two when running.
    bool footstep_log = false;

    // ---- Rendering / shadows ----
    // render/ShadowPass.cpp opens shadow-debug.log and writes per-frame
    // snap state (throttled to every 30 frames, but keep gated for
    // cleanliness).
    bool shadow_log = false;

    // The per-frame ImGui overlay draws every world collider (cylinders
    // + boxes) as wireframe outlines.
    bool show_colliders = false;

    // The per-frame ImGui overlay draws every Jolt body (static
    // trimeshes, static boxes, character capsules) as wireframe AABBs
    // colored by surface tag.
    bool show_physics_bodies = false;

    // Top-right "region: <id>" chip. Useful when multiple regions
    // exist (verifying transitions hit the expected target).
    bool show_region_chip = false;

    // Per-frame ImGui overlay: every registered territory volume as
    // wireframe AABB with its debug_name + owning region. Use to
    // verify territory geometry matches authoring intent (e.g.
    // confirm the descent corridor's south face sits at the expected
    // world Z).
    bool show_territories = false;

    // Floating combo-step + rhythm-window debug overlay. Useful while
    // tuning attack chain timings; otherwise occludes the bottom of
    // the screen.
    bool show_combo_hud = false;

    // Scene fragment shader outputs a flat constant color (uBaseColor)
    // per primitive -- skipping all lighting, atmosphere, shadows,
    // exposure, tonemap. Use to bisect flicker: if flicker DISAPPEARS
    // with this on, the cause is shader math. If flicker CONTINUES,
    // the cause is geometry / rasterization / depth precision.
    bool flat_shading = false;

    // Print actual GL MSAA state every 60 frames so we can confirm
    // whether MSAA is still enabled at scene-pass time.
    bool msaa_state_log = false;

    // Every frame, raycast from camera position through camera forward
    // direction; log the first 5 bodies the ray hits to
    // crosshair-debug.log.
    bool crosshair_raycast_log = false;

    // Paint each chapel mesh primitive a unique color (deterministic
    // hash of its draw index). Combined with flat_shading skipping
    // lighting, flickering pixels visibly alternate between TWO colors
    // which decode to TWO primitive indices -- pinpointing z-fighting
    // pairs instantly. Requires flat_shading also ON.
    bool primitive_id_colors = false;

    // Skeletal fragment shader: for any pixel whose post-lighting RGB
    // looks near-red (R high, G+B low), paint instead with
    // (uv.x, uv.y, 0) so the UV that's sampling the bad pixel reads
    // off the screen as a color you can decode. Plus pixels that ARE
    // sampling specific suspect UV regions get painted bright cyan to
    // distinguish them. Use to pinpoint the source of unexplained red
    // patches on characters (e.g. the placeholder iris blobs in skin
    // packs that haven't been authored over yet).
    bool skeletal_uv_debug = false;

    // ---- World / physics ----
    // world/Collision.cpp opens collision-debug.log and writes
    // per-frame pre/post body XZ + per-pass push events (which collider
    // was hit, the push vector).
    bool collision_log = false;

    // buildViewProj writes per-frame camera pull-in state (origin,
    // direction, desired/target/smoothed separation, hit distance) to
    // camera-debug.log.
    bool camera_pull_in_log = false;

    // world/Terrain.cpp's groundHeight writes per-call state to
    // ground-debug.log: query XZ, current Y, the resulting ground Y
    // from the downward Jolt raycast, and the name of the physics
    // body hit.
    bool ground_height_log = false;

    // The Jolt physics layer writes diagnostics to physics-debug.log:
    // scene-init body counts, character creation, per-frame player
    // capsule pos/velocity/ground-state.
    bool physics_log = false;
};

// Single global instance. Session-only -- never serialized, reset to
// struct defaults at every launch. The F1 panel edits this in place.
Flags& flags();

} // namespace selva::debug
