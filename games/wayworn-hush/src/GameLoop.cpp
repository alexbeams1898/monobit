#include "GameLoop.h"

#include "Engine.h"
#include "Footsteps.h"
#include "Glimmer.h"
#include "Interaction.h"
#include "Notify.h"
#include "PausePage.h"
#include "PlayerMovement.h"
#include "ReadingColor.h"
#include "ScreenInput.h"
#include "ScreenToWorld.h"
#include "ThoughtBox.h"
#include "TunePanel.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "gl/PixelRenderTarget.h"
#include "systems/AnimationSystem.h"
#include "systems/AudioSystem.h"
#include "systems/CameraSystem.h"
#include "systems/RenderSystem.h"
#include "systems/TileMapRenderer.h"
#include "utils/DirectionUtils.h"

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <random>

namespace
{
// Forward decls -- helpers below reference these before their definitions appear.
bool pressedThisFrame(const EntityManager& em, int scancode);
bool clickedThisFrame(EntityManager& em, uint8_t button);
void enactConfirm(EntityManager& em, GameState& gs, const thought_box::ConfirmResult& r);
void attemptCraft(GameState& gs);

// The surface name of the tile at world (wx,wy): the tile's id looked up in the
// region's tile->surface map. Empty if off-map or the tile is untagged (footsteps then
// use the default pool). Drives per-surface footfalls.
std::string surfaceUnder(const EntityManager& em, const GameState& gs, float wx, float wy)
{
    const TileMap& tm = em.tile_map;
    if (!tm.valid())
        return {};
    const int col = static_cast<int>(wx) / tm.tile_size;
    const int row = static_cast<int>(wy) / tm.tile_size;
    if (!tm.in_bounds(col, row))
        return {};
    // Per-cell override first (a bridge deck sounds like wood even over water); then the
    // tile-type surface (grass/water/sand) keyed by the tile id under the player.
    const std::size_t idx = tm.cellIndex(col, row);
    if (const auto cit = gs.cell_surface.find(idx); cit != gs.cell_surface.end())
        return cit->second;
    const auto it = gs.tile_surface.find(tm.at(col, row).tile_id);
    return it == gs.tile_surface.end() ? std::string{} : it->second;
}

// Step the pause page from this frame's input, returning its action. F=back/toggle,
// A/D=tabs, W/S=move, Space=confirm; RMB is back-only (keeps it free as a world verb).
// The action menu (menuUp) is modal and captures those keys, so the page ignores input
// while a menu is up. Also muffles the soundtrack while the page is open.
pause_page::Action stepPausePage(EntityManager& em, GameState& gs, bool menuUp)
{
    const bool rmbBack = gs.pause.open && clickedThisFrame(em, SDL_BUTTON_RIGHT);
    const bool toggle = !menuUp && (pressedThisFrame(em, SDL_SCANCODE_F) || rmbBack);
    const bool left = !menuUp && pressedThisFrame(em, SDL_SCANCODE_A);
    const bool right = !menuUp && pressedThisFrame(em, SDL_SCANCODE_D);
    const bool up = !menuUp && pressedThisFrame(em, SDL_SCANCODE_W);
    const bool down = !menuUp && pressedThisFrame(em, SDL_SCANCODE_S);
    const bool confirm = !menuUp && pressedThisFrame(em, SDL_SCANCODE_SPACE);
    const std::vector<std::string> craftMats = pause_page::craftMaterials(gs.satchel, gs.items);
    const pause_page::Action action =
        pause_page::step(gs.pause, toggle, left, right, up, down, confirm, craftMats);
    if (action == pause_page::Action::Craft)
        attemptCraft(gs);

    // "New" item badges clear when the player LEAVES the satchel view (switched tab or closed
    // the page while on it) -- they saw the fresh finds, so they're no longer new.
    const bool viewingSatchel = gs.pause.open && gs.pause.tab == PauseState::Tab::Satchel;
    static bool sWasViewingSatchel = false;
    if (sWasViewingSatchel && !viewingSatchel)
        inventory::markAllSeen(gs.satchel);
    sWasViewingSatchel = viewingSatchel;

    // Muffle the soundtrack while the page is open (world frozen behind a held breath).
    AudioSystem::setMusicLowPass(gs.pause.open ? 800.0f : 0.0f);
    return action;
}

// Drive the player's animation state from its velocity: moving -> face the movement
// direction (diagonals snap to the dominant cardinal) + walk/fast-walk clip; still ->
// hold the idle pose for the last-faced direction. Cadence is per-clip, decoupled from
// speed.
void updatePlayerAnim(Animation& anim, const Velocity& vel, const PlayerConfig& pc, bool fast)
{
    if (vel.dx != 0.0f || vel.dy != 0.0f)
    {
        anim.dir = engine::direction::snapMovement(vel.dx, vel.dy, anim.direction_count);
        const PlayerConfig::AnimState& st = fast ? pc.fast_walk : pc.walk;
        anim.current_row = st.row;
        anim.current_frames = st.frames;
        anim.current_duration = st.duration;
    }
    else
    {
        anim.current_row = pc.idle.row;
        anim.current_frames = pc.idle.frames;
        anim.current_duration = pc.idle.duration;
    }
}

bool pressedThisFrame(const EntityManager& em, int scancode)
{
    // Edge-triggered: only on the first fixed tick of the frame, and only if the
    // key-down event is buffered this frame (fires once per physical press).
    if (em.ticks_this_frame != 0)
        return false;
    const auto& kd = em.key_down_events;
    return std::find(kd.begin(), kd.end(), scancode) != kd.end();
}

// The random nudge for observation rolls: a uniform int in [0, n]. Seeded once;
// the observation logic stays testable by taking the RNG as a parameter.
int observeNudge(int n)
{
    static std::mt19937 gen{std::random_device{}()};
    if (n <= 0)
        return 0;
    return std::uniform_int_distribution<int>{0, n}(gen);
}

// Edge-triggered mouse-button press, same first-tick guard as pressedThisFrame
// so a multi-tick frame can't fire it twice. Consumes the click (via the engine
// consume flag) so gameplay doesn't also act on it.
bool clickedThisFrame(EntityManager& em, uint8_t button)
{
    if (em.ticks_this_frame != 0)
        return false;
    return engine::ui::mouseClicked(em, button);
}

// Toast color for "new observation / action available" (EXP toasts are emitted
// by the box when the earning line displays -- see ThoughtBox loadLine).
constexpr Color kUnlockColor{0.70f, 0.82f, 0.95f, 1.0f};

// Pre-mark a just-observed spot's already-offered actions as announced (no toast)
// -- they're shown in the menu right there. Only actions that unlock LATER (via
// growth / a flag) toast as a "1 new action" pull-back. Tiers stay un-seeded.
void seedObservedActionsAsKnown(GameState& gs, const std::string& spot)
{
    for (const auto& id : observations::availableUnlocks(gs.observations, gs.growth))
        if (id.rfind(spot + ":", 0) == 0) // "<spot>:<actionid>"
            gs.announced_unlocks.insert(id);
}

// Re-run the engine over the stat keys only when the stats actually changed (a
// cheap sum-of-levels dirty check), so growth can land a thought / re-open a
// miss without a per-frame scan. Fired thoughts queue their own EXP-carrying
// lines; the box toasts the reward when it displays them.
void pumpStatChangeThoughts(GameState& gs)
{
    int statSum = 0;
    for (const auto& [name, level] : gs.growth.stat_levels)
        statSum += level;
    static int sLastStatSum = -1;
    if (statSum == sLastStatSum)
        return;
    sLastStatSum = statSum;
    gs.growth.spirit_exp +=
        observations::evaluateStats(gs.observations, gs.growth, observeNudge).earned;
}

// Conclusions are just deeper observations -- they surface the same way.
void pumpUnlockNotifications(GameState& gs)
{
    const std::unordered_set<std::string> now =
        observations::availableUnlocks(gs.observations, gs.growth);
    for (const auto& id : now)
        if (gs.announced_unlocks.insert(id).second) // first time we've seen it
            notify::push(id.find('@') != std::string::npos ? "1 new observation available"
                                                           : "1 new action available",
                         kUnlockColor);
    // Drop announcements no longer available (consumed), so if the same unlock
    // legitimately re-appears later it announces again.
    for (auto it = gs.announced_unlocks.begin(); it != gs.announced_unlocks.end();)
        it = now.count(*it) ? std::next(it) : gs.announced_unlocks.erase(it);
}

// Observe / read / act input. A reading Line is non-modal (you can keep walking)
// but only Space advances it, so a queued thought is never skipped. Only the
// The cursor's world position: window pixel -> internal-res pixel (undo the pixel-target's
// aspect-fit blit) -> world (camera-centered, zoom 1). Returns false if there's no active
// camera yet (nothing to anchor to). See ScreenToWorld.
bool mouseWorld(const Engine& engine, const EntityManager& em, float& out_x, float& out_y)
{
    entt::entity cam = entt::null;
    for (auto [e, c] : em.registry().view<Camera>().each())
        if (c.active)
            cam = e;
    if (cam == entt::null)
        return false;
    const auto& c = em.registry().get<Camera>(cam);
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    const engine::gl::BlitRect b = engine::gl::computeBlitRect(
        kInternalWidth, kInternalHeight, engine.windowWidth(), engine.windowHeight());
    const auto r =
        screen_to_world::map(static_cast<float>(mx), static_cast<float>(my), b.x, b.y, b.width,
                             b.height, kInternalWidth, kInternalHeight, c.x, c.y);
    out_x = r.x;
    out_y = r.y;
    return true;
}

// Handle the MODAL reading/menu states, which capture input when up. Returns true if a
// modal consumed this frame's input (so the world interaction below is skipped). Click =
// Space everywhere: a click anywhere advances a reading; the action menu is confirmed by
// clicking a hovered option (menuMouse, at the render layer) OR Space. The deliberate
// observe verb itself lives in the InteractionSystem. `clicked` is a consumed left-press
// this frame (so it won't also fire an interactable / leak into the pause page).
bool handleReadingInput(EntityManager& em, GameState& gs, bool clicked)
{
    if (thought_box::menuActive())
    {
        if (pressedThisFrame(em, SDL_SCANCODE_SPACE))
            // A deed can fire a thought (EXP) and/or grant an item -- enact both.
            enactConfirm(em, gs, thought_box::confirm(gs.observations, gs.growth, observeNudge));
        if (pressedThisFrame(em, SDL_SCANCODE_W))
            thought_box::moveUp();
        if (pressedThisFrame(em, SDL_SCANCODE_S))
            thought_box::moveDown();
        if (pressedThisFrame(em, SDL_SCANCODE_F))
            thought_box::back();
        return true;
    }
    if (thought_box::active()) // a reading Line: Space OR a click anywhere advances it
    {
        if (pressedThisFrame(em, SDL_SCANCODE_SPACE) || clicked)
            enactConfirm(em, gs, thought_box::confirm(gs.observations, gs.growth, observeNudge));
        return true;
    }
    return false;
}

// Toast each deposited item in its rarity color (one line per item so a rare find pops in
// its own color). Stacked quantities show a count.
void toastFinds(const GameState& gs, const std::vector<inventory::ItemInstance>& items)
{
    for (const auto& inst : items)
    {
        const inventory::ItemDef* def = gs.items.find(inst.id);
        if (!def)
            continue;
        // Always show the amount (even 1x) so the format is consistent -- "2x Wild Thyme",
        // "1x Worn River Stone".
        const std::string line = std::to_string(inst.quantity) + "x " + def->name;
        notify::push(line, reading_color::rarityColor(def->rarity));
    }
}

// Deposit item ids + rolled loot tables into the satchel, toasting each find. The ONE place
// an item enters the bag -- shared by the direct-pickup path (interactable action) and the
// deed path (a "pick up"/"gather" deed). `items` are item ids (one each); `tables` are loot
// table ids to roll.
void grantAndToast(GameState& gs, const std::vector<std::string>& items,
                   const std::vector<std::string>& tables)
{
    std::vector<inventory::ItemInstance> deposited;
    for (const auto& id : items)
    {
        inventory::add(gs.satchel, gs.items, inventory::ItemInstance{id});
        deposited.push_back(inventory::ItemInstance{id});
    }
    for (const auto& tableId : tables)
        if (const loot::Table* table = gs.loot_tables.find(tableId))
            for (auto& inst : loot::roll(*table, observeNudge))
            {
                inventory::add(gs.satchel, gs.items, inst);
                deposited.push_back(inst);
            }
    toastFinds(gs, deposited);
}

// Run a craft attempt from the Craft tab's selected materials: match them against the recipe
// table (gated by the same knowledge observations use), and on an exact match make it -- the
// craft consumes inputs + grants the output, and here we bank its XP, set any first-craft
// reveal flag, and toast the find. No match -> a near-miss / nothing feedback line. Either way
// the outcome shows in `craft_result` and the selection clears.
void attemptCraft(GameState& gs)
{
    std::unordered_set<std::string> observedOut;
    std::unordered_map<std::string, int> statsOut;
    const unlock::Knowledge k =
        observations::buildKnowledge(gs.observations, gs.growth, observedOut, statsOut);

    const std::vector<std::string> selected(gs.pause.craft_selected.begin(),
                                            gs.pause.craft_selected.end());
    const crafting::Match m = crafting::match(selected, gs.recipes, k);

    if (m.recipe == nullptr)
    {
        // No recipe. A near-miss (some ingredients of a real, realized recipe) nudges; nothing
        // in common stays silent about what's missing (you're on your own -- see CRAFTING.md).
        gs.pause.craft_result = (m.nearest != nullptr && m.closeness >= 0.5f)
                                    ? "Something almost forms..."
                                    : "These don't belong together.";
        gs.pause.craft_selected.clear();
        return;
    }

    const int craftStat = growth::statLevel(gs.growth, m.recipe->scaling.stat);
    const crafting::Outcome out =
        crafting::craft(*m.recipe, gs.satchel, gs.items, craftStat, observeNudge,
                        gs.crafting_config, gs.crafting_state);
    if (!out.made)
    {
        gs.pause.craft_result = "Not enough to work with.";
        gs.pause.craft_selected.clear();
        return;
    }
    // Bank the craft XP + fire any first-craft reveal, toast the made item. NOTE: there is no
    // per-secondary-stat XP pool yet (issue #142), so the doing-layer XP is banked into Spirit
    // EXP for now -- the crafting model already ATTRIBUTES it to xp_stat, ready to route once
    // the progression system lands.
    gs.growth.spirit_exp += out.xp;
    if (!out.revealed_flag.empty())
        gs.observations.flags.insert(out.revealed_flag);
    if (const inventory::ItemDef* def = gs.items.find(out.output_item))
    {
        gs.pause.craft_result = "Made: " + def->name;
        notify::push(std::to_string(out.output_qty) + "x " + def->name,
                     reading_color::rarityColor(def->rarity));
    }
    gs.pause.craft_selected.clear();
}

// Despawn the world entity for an observable id (its glimmer + interactable), e.g. when a
// consumes_spot take removes the whole thing. No-op if the spot has no world entity.
void despawnObservableEntity(EntityManager& em, const std::string& observableId)
{
    auto& reg = em.registry();
    for (auto [e, inter] : reg.view<interaction::Interactable>().each())
        if (inter.observe_id == observableId)
        {
            reg.destroy(e);
            return;
        }
}

// Bank the EXP a deed earned + grant any items/tables it declared, and despawn the spot if
// the take consumed it. The chokepoint for a menu confirm's effects (Space or click), so
// both input paths enact them identically.
void enactConfirm(EntityManager& em, GameState& gs, const thought_box::ConfirmResult& r)
{
    gs.growth.spirit_exp += r.earned;
    if (!r.granted.empty() || !r.gathered.empty())
        grantAndToast(gs, r.granted, r.gathered);
    if (!r.consumed_spot.empty())
        despawnObservableEntity(em, r.consumed_spot);
}

// Follow-up after the InteractionSystem fired. A direct actionable-only item already deposited
// its find -> toast it. For an observable spot, the STANCE is the verb: WALKING observes (the
// reading only), RUNNING acts (the deed menu only). The stance badge teaches this; there is no
// per-spot prompt and no reading->menu handoff.
void onInteractionFired(GameState& gs, const interaction::Outcome& out)
{
    if (out.observe_target.empty())
    {
        toastFinds(gs, out.items); // direct pickup/gather -- items already in the satchel
        return;
    }
    const std::string& spot = out.observe_target;
    if (out.act)
    {
        // Running (Act stance) -> the deed menu. Seed baseline deeds as announced (the spot
        // was observed on a prior visit, so they're available) so they don't toast as "new".
        seedObservedActionsAsKnown(gs, spot);
        thought_box::openDeedMenu(gs.observations, gs.growth, spot);
        return;
    }
    // Walking (Observe stance) -> the reading only. Bank its EXP. Seed the baseline deeds AFTER
    // observing (observing is what makes them available -- observed_tier is set by pushObserve)
    // so they don't toast "1 new action available"; only later-unlocked deeds announce.
    gs.growth.spirit_exp +=
        thought_box::pushObserve(gs.observations, gs.growth, spot, observeNudge);
    seedObservedActionsAsKnown(gs, spot);
}
} // namespace

void gameUpdate(Engine& engine, EntityManager& em, double dt)
{
    auto& reg = em.registry();
    GameState& gs = reg.ctx().get<GameState>();

    // F1 toggles the dev tunables panel (drawn in the ImGui pass).
    if (pressedThisFrame(em, SDL_SCANCODE_F1))
        tune_panel::toggle();

    // M mutes / unmutes the soundtrack (persists across track changes).
    if (pressedThisFrame(em, SDL_SCANCODE_M))
        AudioSystem::toggleMusicMute();

    // A stat change re-runs the engine over the stat keys (growth can land a
    // thought or re-open a prior miss). Then announce any newly-reachable
    // observations/actions (the pull-back to a spot).
    pumpStatChangeThoughts(gs);

    // Cheap + idempotent (diffs against announced_unlocks), so once/frame.
    pumpUnlockNotifications(gs);

    // Only the action MENU is modal (captures F/W/S/Space + freezes movement); a
    // reading never freezes you. So the pause page must not see those keys while a
    // menu is up.
    const bool menuUp = thought_box::menuActive();

    if (stepPausePage(em, gs, menuUp) == pause_page::Action::Quit)
        engine.requestQuit();

    // Freeze the world while the page is open: skip movement, observing, camera.
    // Animation is halted in gamePreRender (it runs at wall-clock rate there).
    if (gs.pause.open)
        return;

    // World time advances only while unfrozen (drives notebook datelines; later the
    // day/night cycle). Placed after the pause guard so a paused world holds time.
    worldclock::tick(gs.clock, dt);

    // Snapshot positions for render interpolation before integrating.
    for (auto [e, t, pt] : reg.view<Transform, PreviousTransform>().each())
    {
        pt.x = t.x;
        pt.y = t.y;
    }

    const PlayerConfig& pc = gs.player_config;

    // Only the action menu freezes movement (its W/S drive selection). Readings
    // leave you free to walk -- and walking away dismisses them (below).
    static const Uint8 kNoKeys[SDL_NUM_SCANCODES] = {};
    const Uint8* keys = menuUp ? kNoKeys : SDL_GetKeyboardState(nullptr);

    // Hold Shift to fast-walk: faster movement + brisker leg cadence.
    const bool fast = keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0;
    const float speed = fast ? pc.speed * pc.run_speed_mult : pc.speed;

    // Interaction stance = running vs walking (raw Shift, so it tracks the true stance even
    // with a menu up, where movement is frozen). Plays the enter-Act / enter-Observe SFX on a
    // transition; the badge (rendered in the UI pass) shows the current stance.
    const Uint8* rawKeys = SDL_GetKeyboardState(nullptr);
    const bool running = rawKeys[SDL_SCANCODE_LSHIFT] != 0 || rawKeys[SDL_SCANCODE_RSHIFT] != 0;
    interaction_mode::update(gs.int_mode_state, gs.int_mode_config, running);
    player_movement::update(em, gs.player, keys, speed, static_cast<float>(dt));

    // Facing + state from the resulting velocity. Moving -> face movement
    // direction (diagonals snap to the dominant cardinal) and play walk or
    // fast-walk; still -> hold the standing pose for the last direction.
    auto& anim = reg.get<Animation>(gs.player);
    const auto& vel = reg.get<Velocity>(gs.player);
    updatePlayerAnim(anim, vel, pc, fast);

    // Footstep SFX: a footfall matching the surface under the player, on a speed-scaled
    // cadence while moving. The surface is the tile at the player's position (empty if
    // untagged -> the default pool; water has no pool -> silent).
    const bool moving = vel.dx != 0.0f || vel.dy != 0.0f;
    const auto& ptf = reg.get<Transform>(gs.player);
    const std::string surface = surfaceUnder(em, gs, ptf.x, ptf.y);
    footsteps::update(gs.footstep_state, gs.footstep_config, surface, moving, fast,
                      static_cast<float>(dt));

    // Player position -- observation glow + interaction are proximity-based (no facing).
    const auto& pt = reg.get<Transform>(gs.player);

    // Ambient triggers: Enter observables (areas, moods) fire on their own when the player
    // is within range -- no observe verb. Deliberate object observing stays in
    // handleObserveInput. Fires once each; earns Spirit EXP like a deliberate reading.
    gs.growth.spirit_exp +=
        observations::triggerProximity(gs.observations, gs.growth, pt.x, pt.y, observeNudge).earned;

    // Read the left-click ONCE (mouseClicked consumes it) so the same click can't both
    // advance a reading AND fire an interactable / leak to the pause page.
    const bool clicked = clickedThisFrame(em, SDL_BUTTON_LEFT);

    // World interaction: a reading/menu, if up, consumes input (modal -- click = Space,
    // advancing it). Otherwise resolve the active interactable (nearest in reach or under
    // the cursor), and Space OR a click ON it fires -- observe now, pick-up/craft/open
    // later. The InteractionSystem also sets the `active` flag that drives the glow.
    if (!handleReadingInput(em, gs, clicked))
    {
        interaction::Intent intent;
        intent.px = pt.x;
        intent.py = pt.y;
        intent.mouse_valid = mouseWorld(engine, em, intent.mouse_x, intent.mouse_y);
        intent.pressed = pressedThisFrame(em, SDL_SCANCODE_SPACE);
        intent.clicked = clicked;
        // Running (Shift held) makes interacting an ACT (skip to the deed menu); walking
        // observes. Slow down to notice; move with intent to do.
        const Uint8* keyState = SDL_GetKeyboardState(nullptr);
        intent.act = keyState[SDL_SCANCODE_LSHIFT] != 0 || keyState[SDL_SCANCODE_RSHIFT] != 0;
        const interaction::Context ctx{gs.observations,
                                       gs.growth,
                                       observeNudge,
                                       gs.satchel,
                                       gs.items,
                                       gs.loot_tables,
                                       gs.observations.interact_reach};
        const interaction::Outcome fired = interaction::update(em, intent, ctx);
        gs.growth.spirit_exp += fired.earned;
        if (fired.fired)
            onInteractionFired(gs, fired);
    }

    // World glimmer: an observable glows only while it's the active target; its brightness is
    // the Perception formula (same for every spot). Fades to 0 otherwise -- nothing lingers.
    // Runs after the InteractionSystem set `active`.
    glimmer::update(em, gs.growth, gs.formulas, gs.glimmer_config, static_cast<float>(dt));

    // World items: the active one gets a lit rim outline (its own edge lights -- the pickup
    // cue, distinct from the observation glimmer). Also runs after `active` is set.
    world_items::updateOutlines(em, gs.world_items_config);

    // Over-head thought bubble: shown exactly while a thought reading is on screen,
    // then fades. It tracks the player's head each frame.
    Color thoughtHue{};
    const bool thoughtUp = thought_box::activeThought(gs.growth, thoughtHue);
    head_marker::set(em, thoughtUp);
    head_marker::update(em, gs.head_marker_config, pt.x, pt.y, static_cast<float>(dt));

    // Snap the active camera to its entity (the player).
    CameraSystem::update(em);
}

void gamePreRender(Engine& engine, EntityManager& em)
{
    auto& gs = em.registry().ctx().get<GameState>();

    // The page freezes the world: hold the sprite pose and stop draining
    // monologue lines (both advance at wall-clock rate, so they must be gated
    // here rather than in the fixed-step update).
    if (gs.pause.open)
        return;

    // Sprite-sheet animation advances at wall-clock frame rate, not the fixed
    // tick (see engines/engine/docs/ENGINE.md "Animation system").
    AnimationSystem::update(em, static_cast<float>(engine.frameDt()));

    // The notebook records a reading as it surfaces IFF the pilgrim carries the
    // notebook (key-item gate); the entry is dated only if he also carries a watch
    // (else undated). GameLoop owns the item/clock knowledge; the box just records.
    thought_box::RecordSink sink;
    sink.record = &gs.notebook;
    sink.enabled = inventory::has(gs.satchel, "notebook");
    sink.day = inventory::has(gs.satchel, "watch") ? worldclock::day(gs.clock) : 0;
    thought_box::update(gs.observations, gs.growth, static_cast<float>(engine.frameDt()),
                        engine.windowWidth(), engine.windowHeight(), sink);
}

void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float alpha)
{
    (void)alpha;

    engine::gl::pixelTargetBegin(kAmbientR, kAmbientG, kAmbientB);
    // Render at the internal resolution (the pixel target's viewport), zoom 1.
    // camX/camY are the engine-interpolated active-camera position. Draw order:
    // GROUND -> DECORATION (flowers, under player) -> character sprites -> OVERHANG
    // (canopy tops, over player = walk-behind). The sparse passes no-op if absent.
    TileMapRenderer::render(camX, camY, kInternalWidth, kInternalHeight, 1.0f);
    TileMapRenderer::renderDecoration(camX, camY, kInternalWidth, kInternalHeight, 1.0f);
    RenderSystem::render(em, engine.textureManager(), camX, camY, 1.0f);
    TileMapRenderer::renderOverhang(camX, camY, kInternalWidth, kInternalHeight, 1.0f);
    engine::gl::pixelTargetEnd(engine.windowWidth(), engine.windowHeight());
}

void gameRenderUI(Engine& engine, EntityManager& em)
{
    auto& gs = em.registry().ctx().get<GameState>();
    const int ww = engine.windowWidth();
    const int wh = engine.windowHeight();

    // HUD visibility mode (see hud::Visibility): Off suppresses all HUD region
    // drawing; On adds always-on region frames under the content; Auto (default)
    // draws only regions that hold content. The pause page is separate (F-gated),
    // so Off still lets the player open it.
    const bool hudOff = gs.hud.visibility == hud::Visibility::Off;
    if (gs.hud.visibility == hud::Visibility::On)
        hud::drawIdleFrames(gs.hud, ww, wh);

    if (!hudOff)
    {
        // Inner-monologue textbox, drawn in native window space (the engine's UI
        // pass runs after the world blit, at window resolution). Tinted by faculty
        // + rarity via the growth state.
        thought_box::render(gs.growth, ww, wh);

        // Ambient notification toasts (EXP, "new observation/action available") --
        // in the notification band, non-blocking, self-fading.
        notify::render(static_cast<float>(engine.frameDt()), ww, wh);

        // Interaction-stance badge (Observe / Act) so the player always knows which verb an
        // interact will do.
        interaction_mode::render(gs.int_mode_state, gs.int_mode_config, ww, wh);
    }

    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    const bool lClick = engine::ui::mouseClicked(em, SDL_BUTTON_LEFT);

    // Mouse on the action menu (interchangeable with W/S + Space): hover an option
    // to highlight, click to confirm. Runs after render() stashes menu geometry, so
    // it's skipped when the HUD is Off (no menu drawn, geometry stale).
    // The pause page can't be open while the menu is up, so the click is theirs to
    // share without conflict.
    if (!hudOff)
        enactConfirm(em, gs,
                     thought_box::menuMouse(gs.observations, gs.growth, observeNudge,
                                            static_cast<float>(mx), static_cast<float>(my),
                                            lClick));

    // Pause page over everything (no-op when closed). Mouse is interchangeable
    // with the keyboard controls: hover a tab to highlight, click to switch,
    // click Quit on the System tab to exit. Mouse handling lives here because it
    // hit-tests the geometry render() draws.
    const pause_page::Mouse mouse{static_cast<float>(mx), static_cast<float>(my), lClick};
    const pause_page::Content content{gs.observations, gs.satchel, gs.items,
                                      gs.notebook,     gs.recipes, gs.crafting_state};
    if (pause_page::render(gs.pause, gs.growth, content, mouse, engine.windowWidth(),
                           engine.windowHeight()) == pause_page::Action::Quit)
        engine.requestQuit();

    // Clear one-shot input buffers after all consumers have seen them (the
    // engine fills them but leaves clearing to the game). Guard on a tick having
    // run so events arriving on a 0-tick frame aren't discarded before the
    // fixed-step observe/input code reads them.
    if (em.ticks_this_frame > 0)
    {
        em.key_down_events.clear();
        em.mouse_down_events.clear();
        em.mouse_wheel_y = 0;
        em.text_input_buffer.clear();
    }
}

void gameRenderImGui(Engine& /*engine*/, EntityManager& em)
{
    // Dev tunables panel (F1). No-op when hidden. Edits player_config + stats
    // live; the Cognition tab shows the observation state's live tree.
    auto& gs = em.registry().ctx().get<GameState>();
    tune_panel::render(gs.player_config, gs.growth, gs.observations);
}
