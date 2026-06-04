#include "spawn/FlowSpawner.h"

#include "WallClock.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/SkeletalAssets.h"
#include "combat/CombatLog.h"
#include "gameplay/Actor.h"
#include "gameplay/Enemies.h"
#include "gameplay/EnemyArchetype.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace selva::spawn
{

namespace
{

// Parsed shape of one flow's config. Mirrors the JSON schema.
struct FlowConfig
{
    std::string id;
    std::string archetype_id;
    std::string spawn_region_id;
    glm::vec3 spawn_position = glm::vec3(0.0f);
    float spawn_yaw = 0.0f;
    std::optional<glm::vec3> scripted_target_pos;
    float scripted_stop_range = 0.5f;
    // Intermediate waypoints traversed BEFORE scripted_target_pos /
    // the slot target. Used when the straight-line path from spawn
    // to target would cut through geometry -- e.g. larvae need to
    // reach the corridor exit before angling to their scattered
    // slots. Authored as `waypoints: [[x,y,z], ...]` in flow JSON.
    std::vector<glm::vec3> waypoints;
    std::vector<std::string> active_count_includes; // archetype ids
    int active_cap = 1;
    float spawn_interval_seconds = 30.0f;
    std::string on_arrival_action; // e.g. "convert_to:larva_aged" / "halt" / "despawn"
    float on_arrival_delay_seconds = 0.0f;
    // Optional frozen-pose idle for trickle actors AFTER they arrive
    // at the scripted target but BEFORE on_arrival_action fires.
    // Plays the named clip as a one-shot with freeze_last + the given
    // freeze_at_seconds so the actor holds a specific pose frame
    // (e.g. zombie_crawl frozen at 0.1s = prone crawl pose, used to
    // visually distinguish fresh-at-shore from standing aged). Empty
    // = the trickle archetype's default idle plays (existing behavior).
    std::string frozen_idle_clip;
    float frozen_idle_freeze_at_seconds = 0.0f;
    // Target-selection mode for trickle spawns.
    //   "fixed_target" (default): every trickle walks to scripted_target_pos.
    //   "first_vacant_slot": every trickle walks to the first vacant
    //     slot from initial_positions. Slots are vacated when their
    //     occupying actor dies. This makes the queue feel lived-in:
    //     the slot a feral died in is exactly where the next fresh
    //     crawls to, and the converted aged repopulates that same
    //     slot. Requires initial_positions to be authored.
    std::string target_mode = "fixed_target";
    // Optional initial-population block. When present, the flow's
    // FIRST tick spawns enough actors of `initial_archetype_id` at
    // `initial_position` to reach active_cap immediately (or to
    // fill the gap if the world already has some actors). Represents
    // the population that already EXISTS in the world at game start
    // -- centuries of accumulation per the larva cosmology. Without
    // this, the flow takes (cap * interval) seconds to ramp up to
    // full capacity, which feels wrong on a fresh entry to the area.
    std::string initial_archetype_id; // empty = no initial fill
    // Authored list of initial positions. The fill spawns ONE actor
    // per entry (so authors scatter the population by hand). Length
    // determines the initial count; trickle then maintains the cap
    // up from there. Empty = no initial fill.
    std::vector<glm::vec3> initial_positions;
    std::vector<float> initial_yaws; // optional; parallel to positions
    // Optional per-slot archetype overrides. Parallel to positions;
    // entries are empty string by default (use initial_archetype_id).
    // Non-empty entries spawn that slot with the named archetype
    // instead, allowing visual variants within one queue (e.g.
    // larva_aged + larva_aged_feeder mixed in the larva flow).
    std::vector<std::string> initial_archetypes_per_slot;
    // Optional per-slot on_arrival_action overrides. Parallel to
    // positions; entries are empty string by default (use the flow-
    // level on_arrival_action). Non-empty entries fire that action
    // when a trickle actor reaches THIS slot, allowing mixed-mode
    // queues -- e.g. feeder slots convert fresh -> larva_aged_feeder
    // while standing slots convert fresh -> larva_aged.
    std::vector<std::string> on_arrival_actions_per_slot;
};

// Per-flow runtime state.
struct FlowState
{
    FlowConfig cfg;
    float seconds_since_last_check = 0.0f;
    int spawn_counter = 0;
    bool initial_fill_done = false;
    // Slot occupancy tracking when target_mode == "first_vacant_slot".
    // Each entry is the spawn_decl_id of the actor currently in that
    // slot (or empty == vacant). Length matches initial_positions when
    // slot mode is active; empty for fixed-target flows.
    std::vector<std::string> slot_occupant;
};

std::vector<FlowState>& flows()
{
    static std::vector<FlowState> v;
    return v;
}

// Parse an action string into (verb, optional arg). E.g.
// "convert_to:larva_aged" -> ("convert_to", "larva_aged").
// "halt" -> ("halt", "").
// "" -> ("", "").
std::pair<std::string, std::string> parseAction(const std::string& s)
{
    if (s.empty())
        return {std::string{}, std::string{}};
    const auto colon = s.find(':');
    if (colon == std::string::npos)
        return {s, std::string{}};
    return {s.substr(0, colon), s.substr(colon + 1)};
}

// Count active actors (non-dead) whose archetype id is in the
// flow's active_count_includes set.
int countActiveForFlow(const FlowConfig& cfg)
{
    std::unordered_set<std::string> includes(cfg.active_count_includes.begin(),
                                             cfg.active_count_includes.end());
    if (includes.empty())
        includes.insert(cfg.archetype_id); // sensible default
    int n = 0;
    for (const auto& a : selva::gameplay::actors())
    {
        if (a.archetype == nullptr || a.is_dead)
            continue;
        if (includes.find(a.archetype->id) != includes.end())
            ++n;
    }
    return n;
}

// Build a spawn decl for one trickle actor from the flow. Each spawn
// gets a unique decl id so the actor pool can identify it. If
// target_override is provided (slot mode), use that as scripted_target_pos
// instead of the flow's default. Caller is responsible for resolving
// which target to pass.
selva::gameplay::EnemySpawnDecl buildSpawnDecl(FlowState& fs, const glm::vec3* target_override)
{
    selva::gameplay::EnemySpawnDecl d;
    d.id = fs.cfg.id + "_dynamic_" + std::to_string(fs.spawn_counter++);
    d.archetype = fs.cfg.archetype_id;
    d.pos = fs.cfg.spawn_position;
    d.yaw = fs.cfg.spawn_yaw;
    d.permanent_on_death = false;
    if (target_override != nullptr)
    {
        d.scripted_target_pos = *target_override;
        d.scripted_stop_range = fs.cfg.scripted_stop_range;
        // In slot mode the override IS the final target; corridor-exit
        // waypoints (authored in flow.waypoints) still apply IN FRONT
        // of the slot target so each fresh finishes the descent before
        // angling to its slot.
        d.scripted_path_waypoints = fs.cfg.waypoints;
    }
    else if (fs.cfg.scripted_target_pos.has_value())
    {
        d.scripted_target_pos = *fs.cfg.scripted_target_pos;
        d.scripted_stop_range = fs.cfg.scripted_stop_range;
        d.scripted_path_waypoints = fs.cfg.waypoints;
    }
    return d;
}

// Build a spawn decl for one initial-population actor at a specific
// authored position + yaw. These actors represent the population that
// already EXISTS in the world (e.g. the larva queue accumulated over
// centuries before the player arrived). No scripted target -- they
// spawn already-at-destination.
selva::gameplay::EnemySpawnDecl buildInitialDecl(FlowState& fs, const glm::vec3& pos, float yaw,
                                                 const std::string& archetype_override)
{
    selva::gameplay::EnemySpawnDecl d;
    d.id = fs.cfg.id + "_initial_" + std::to_string(fs.spawn_counter++);
    // Per-slot archetype override wins; empty means use the flow default.
    d.archetype = !archetype_override.empty() ? archetype_override : fs.cfg.initial_archetype_id;
    d.pos = pos;
    d.yaw = yaw;
    d.permanent_on_death = false;
    return d;
}

// Initial fill: run once per flow, on the first tick after init or
// reset. Spawns one actor per entry in initial_positions, each at
// its own authored XYZ + yaw. Authors scatter the population by hand;
// the trickle loop maintains the cap from there. Marks fill_done
// after firing so subsequent ticks skip this pass.
void tickInitialFill(FlowState& fs)
{
    if (fs.initial_fill_done)
        return;
    fs.initial_fill_done = true;
    if (fs.cfg.initial_archetype_id.empty() || fs.cfg.initial_positions.empty())
        return;
    const bool slot_mode = (fs.cfg.target_mode == "first_vacant_slot");
    if (slot_mode)
        fs.slot_occupant.assign(fs.cfg.initial_positions.size(), std::string{});
    int spawned = 0;
    for (std::size_t i = 0; i < fs.cfg.initial_positions.size(); ++i)
    {
        const glm::vec3& p = fs.cfg.initial_positions[i];
        const float y =
            (i < fs.cfg.initial_yaws.size()) ? fs.cfg.initial_yaws[i] : fs.cfg.spawn_yaw;
        const std::string& arch_override = (i < fs.cfg.initial_archetypes_per_slot.size())
                                               ? fs.cfg.initial_archetypes_per_slot[i]
                                               : std::string{};
        selva::gameplay::EnemySpawnDecl d = buildInitialDecl(fs, p, y, arch_override);
        const std::string spawn_id = d.id;
        selva::gameplay::spawnEnemyFromDecl(fs.cfg.spawn_region_id, d);
        if (auto* a = selva::gameplay::actorByDeclId(spawn_id))
            a->spawning_flow_id = fs.cfg.id;
        if (slot_mode)
            fs.slot_occupant[i] = spawn_id;
        ++spawned;
    }
    selva::combat::combatLog(
        "[spawn-flow] '{}' initial-fill: spawned {} '{}' actors at authored positions", fs.cfg.id,
        spawned, fs.cfg.initial_archetype_id);
}

// Slot bookkeeping: sweep occupants, clear any whose actor is dead or
// no longer in the pool. Runs each tick when slot mode is active so
// vacancies are detected on the same frame an actor dies. Used by
// tickSpawnDecision below to find the next slot for a fresh trickle.
void sweepSlotOccupancy(FlowState& fs)
{
    if (fs.slot_occupant.empty())
        return;
    const auto& pool = selva::gameplay::actors();
    for (auto& occ : fs.slot_occupant)
    {
        if (occ.empty())
            continue;
        bool found_alive = false;
        for (const auto& a : pool)
        {
            if (a.spawn_decl_id == occ && !a.is_dead)
            {
                found_alive = true;
                break;
            }
        }
        if (!found_alive)
            occ.clear();
    }
}

// Pick the first vacant slot index. Returns -1 if none vacant or
// slot mode is inactive. Caller is responsible for reserving the
// slot with the new actor's id BEFORE the spawn completes.
int firstVacantSlot(const FlowState& fs)
{
    for (std::size_t i = 0; i < fs.slot_occupant.size(); ++i)
        if (fs.slot_occupant[i].empty())
            return static_cast<int>(i);
    return -1;
}

// Per-actor sweep: detect arrival at scripted target, stamp arrival
// time, fire delayed on_arrival_action once delay has elapsed.
// Find the per-slot on_arrival_action override for this actor, falling
// back to the flow-level action when no slot has a non-empty override.
std::string resolveEffectiveArrivalAction(const FlowState& fs, const selva::gameplay::Actor& a)
{
    for (std::size_t i = 0; i < fs.slot_occupant.size(); ++i)
    {
        if (fs.slot_occupant[i] != a.spawn_decl_id)
            continue;
        if (i < fs.cfg.on_arrival_actions_per_slot.size() &&
            !fs.cfg.on_arrival_actions_per_slot[i].empty())
            return fs.cfg.on_arrival_actions_per_slot[i];
        return fs.cfg.on_arrival_action;
    }
    return fs.cfg.on_arrival_action;
}

// Optional frozen-pose idle played once on first arrival so an actor
// visually distinguishes from its converted form (e.g. fresh larva in
// the crawl pose vs the standing zombie_idle the aged form uses).
void playFrozenIdleIfDeclared(const FlowState& fs, selva::gameplay::Actor& a)
{
    if (fs.cfg.frozen_idle_clip.empty())
        return;
    const auto& reg = selva::anim::clipsByKey(a.skeleton_id);
    const selva::anim::AnimationClip* clip = reg.get(fs.cfg.frozen_idle_clip.c_str());
    if (clip == nullptr || !clip->isLoaded())
        return;
    selva::anim::PoseSampler::OneShotOptions opts;
    opts.clip_key = fs.cfg.frozen_idle_clip.c_str();
    opts.freeze_last = true;
    opts.freeze_at_seconds = fs.cfg.frozen_idle_freeze_at_seconds;
    a.sampler.playOneShot(*clip, /*blend_in=*/0.20f, /*blend_out=*/0.20f,
                          selva::anim::PoseSampler::BodyMask::Full,
                          /*start_time_seconds=*/0.0f, /*playback_rate=*/1.0f, opts);
}

// Fire the on_arrival_action verb for a single arrived actor. Returns
// whether the verb consumed the actor (so the loop marks arrival_action_fired).
void fireArrivalAction(const FlowState& fs, selva::gameplay::Actor& a, const std::string& verb,
                       const std::string& arg)
{
    if (verb.empty() || verb == "halt")
    {
        // No-op: actor stops at target and stays there.
        a.arrival_action_fired = true;
        return;
    }
    if (verb == "convert_to")
    {
        const selva::gameplay::EnemyArchetype* target = selva::gameplay::archetypes().get(arg);
        if (target == nullptr)
        {
            std::fprintf(stderr,
                         "[spawn-flow] '%s' convert_to '%s': target archetype not found\n",
                         fs.cfg.id.c_str(), arg.c_str());
            std::fflush(stderr);
            a.arrival_action_fired = true;
            return;
        }
        selva::gameplay::applyArchetypeSwap(a, *target);
        a.arrival_action_fired = true;
        return;
    }
    if (verb == "despawn")
    {
        // Soft despawn: tickEnemies's dead-actor path cleans up. A
        // true pool-remove would need more work.
        a.is_dead = true;
        a.arrival_action_fired = true;
        return;
    }
    std::fprintf(stderr, "[spawn-flow] '%s' unknown on_arrival_action verb '%s'\n",
                 fs.cfg.id.c_str(), verb.c_str());
    std::fflush(stderr);
    a.arrival_action_fired = true;
}

void tickArrivals(FlowState& fs)
{
    const float now = selva::wallClock();
    for (auto& a : selva::gameplay::actors())
    {
        if (a.archetype == nullptr || a.is_dead)
            continue;
        // Match by flow id, not archetype id. Two flows sharing an
        // archetype each see only their own actors; hand-spawned
        // actors never get arrival processing they didn't opt into.
        if (a.spawning_flow_id != fs.cfg.id)
            continue;
        if (!a.had_scripted_target || !std::isnan(a.scripted_target_pos.x))
            continue;
        // First-arrival side effects: stamp wallclock, play any frozen
        // idle pose. Delayed-action timer runs from "moment of arrival"
        // not now (survives pause / post-load re-detect).
        const bool first_arrival = (a.arrival_wallclock < 0.0f);
        if (first_arrival)
        {
            a.arrival_wallclock = now;
            playFrozenIdleIfDeclared(fs, a);
        }
        if (a.arrival_action_fired)
            continue;
        if (now - a.arrival_wallclock < fs.cfg.on_arrival_delay_seconds)
            continue;
        const std::string effective_action = resolveEffectiveArrivalAction(fs, a);
        const auto [verb, arg] = parseAction(effective_action);
        fireArrivalAction(fs, a, verb, arg);
    }
}

void tickSpawnDecision(FlowState& fs, float dt)
{
    fs.seconds_since_last_check += dt;
    if (fs.seconds_since_last_check < fs.cfg.spawn_interval_seconds)
        return;
    fs.seconds_since_last_check = 0.0f;
    const int active = countActiveForFlow(fs.cfg);
    if (active >= fs.cfg.active_cap)
        return;
    const bool slot_mode = (fs.cfg.target_mode == "first_vacant_slot");
    const glm::vec3* target_override = nullptr;
    int chosen_slot = -1;
    if (slot_mode)
    {
        chosen_slot = firstVacantSlot(fs);
        if (chosen_slot < 0)
            return; // no vacancy; trickle waits
        target_override = &fs.cfg.initial_positions[chosen_slot];
    }
    selva::gameplay::EnemySpawnDecl d = buildSpawnDecl(fs, target_override);
    const std::string spawn_id = d.id;
    selva::gameplay::spawnEnemyFromDecl(fs.cfg.spawn_region_id, d);
    if (slot_mode)
        fs.slot_occupant[chosen_slot] = spawn_id;
    // Stamp arrival_action_delay_seconds + spawning_flow_id on the
    // actor: the delay so render-side systems can visualize
    // burn-progress (e.g. tint fresh -> aged as it ages), the flow id
    // so tickArrivals gates matching by which flow spawned it rather
    // than by archetype id (two flows sharing an archetype would
    // otherwise both process each other's actors).
    if (auto* a = selva::gameplay::actorByDeclId(spawn_id))
    {
        a->arrival_action_delay_seconds = fs.cfg.on_arrival_delay_seconds;
        a->spawning_flow_id = fs.cfg.id;
    }
    selva::combat::combatLog("[spawn-flow] '{}' check: active={}/{}  spawned '{}' slot={}",
                             fs.cfg.id, active, fs.cfg.active_cap, spawn_id, chosen_slot);
}

glm::vec3 parseVec3(const nlohmann::json& arr)
{
    return glm::vec3(arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>());
}

void parseInitialPopulationPositions(const nlohmann::json& ip, FlowConfig& out)
{
    if (!ip.contains("positions") || !ip["positions"].is_array())
        return;
    for (const auto& p : ip["positions"])
    {
        if (!p.is_array() || p.size() < 3)
            continue;
        out.initial_positions.push_back(parseVec3(p));
        // Optional 4th element = yaw for this position.
        out.initial_yaws.push_back(p.size() >= 4 ? p[3].get<float>() : 0.0f);
        // Per-slot archetype override starts empty; filled in by the
        // parallel "archetypes" array below if present.
        out.initial_archetypes_per_slot.emplace_back();
    }
}

void parseInitialPopulationParallelArray(const nlohmann::json& ip, const char* key,
                                          std::vector<std::string>& out)
{
    if (!ip.contains(key) || !ip[key].is_array())
        return;
    const auto& arr = ip[key];
    for (std::size_t i = 0; i < arr.size() && i < out.size(); ++i)
        if (arr[i].is_string())
            out[i] = arr[i].get<std::string>();
}

void parseInitialPopulation(const nlohmann::json& ip, FlowConfig& out)
{
    out.initial_archetype_id = ip.value("archetype", std::string{});
    parseInitialPopulationPositions(ip, out);
    // Optional parallel "archetypes" / "on_arrival_actions" arrays
    // (each same length as positions). Empty/omitted entries fall
    // back to the flow-level default. Lets one flow mix variants
    // (larva_aged + larva_aged_feeder) and per-slot conversions
    // (feeder slots -> larva_aged_feeder; standing slots -> larva_aged)
    // without needing two separate flows.
    parseInitialPopulationParallelArray(ip, "archetypes", out.initial_archetypes_per_slot);
    out.on_arrival_actions_per_slot.assign(out.initial_positions.size(), std::string{});
    parseInitialPopulationParallelArray(ip, "on_arrival_actions",
                                         out.on_arrival_actions_per_slot);
}

// Hand-rolled JSON parse, not NLOHMANN_DEFINE_TYPE. The reasons it
// stays this way:
//   * glm::vec3 has no default nlohmann adapter, and the schema uses
//     [x, y, z] (and the 4-element [x, y, z, yaw] variant in
//     initial_population.positions) -- both need custom parsing.
//   * initial_population.archetypes / on_arrival_actions are
//     PARALLEL arrays whose entries map index-by-index onto positions.
//     NLOHMANN_DEFINE would deserialize each as a flat vector and
//     leave the cross-array alignment to a post-processing pass --
//     which is exactly what this function does explicitly.
//   * Several fields are conditionally optional based on the value of
//     "target_mode" (e.g. waypoints only apply when target_mode ==
//     "first_vacant_slot"). NLOHMANN_DEFINE has no conditional schema.
// Future shape may simplify if we drop the parallel-array layout in
// favor of a `slots: [{position, archetype, on_arrival_action}]`
// schema; at that point this could collapse to NLOHMANN_DEFINE.
bool parseFlowJson(const nlohmann::json& j, FlowConfig& out)
{
    try
    {
        out.id = j.at("id").get<std::string>();
        out.archetype_id = j.at("archetype").get<std::string>();
        out.spawn_region_id = j.at("spawn_region_id").get<std::string>();
        out.spawn_position = parseVec3(j.at("spawn_position"));
        out.spawn_yaw = j.value("spawn_yaw", 0.0f);
        if (j.contains("scripted_target_pos") && j["scripted_target_pos"].is_array())
            out.scripted_target_pos = parseVec3(j["scripted_target_pos"]);
        out.scripted_stop_range = j.value("scripted_stop_range", 0.5f);
        if (j.contains("waypoints") && j["waypoints"].is_array())
            for (const auto& w : j["waypoints"])
                if (w.is_array() && w.size() >= 3)
                    out.waypoints.push_back(parseVec3(w));
        if (j.contains("active_count_includes") && j["active_count_includes"].is_array())
            for (const auto& s : j["active_count_includes"])
                out.active_count_includes.push_back(s.get<std::string>());
        out.active_cap = j.value("active_cap", 1);
        out.spawn_interval_seconds = j.value("spawn_interval_seconds", 30.0f);
        out.on_arrival_action = j.value("on_arrival_action", std::string{});
        out.on_arrival_delay_seconds = j.value("on_arrival_delay_seconds", 0.0f);
        out.frozen_idle_clip = j.value("frozen_idle_clip", std::string{});
        out.frozen_idle_freeze_at_seconds = j.value("frozen_idle_freeze_at_seconds", 0.0f);
        out.target_mode = j.value("target_mode", std::string("fixed_target"));
        if (j.contains("initial_population") && j["initial_population"].is_object())
            parseInitialPopulation(j["initial_population"], out);
        return true;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[spawn-flow] parse error: %s\n", e.what());
        std::fflush(stderr);
        return false;
    }
}

} // namespace

void initFlowSpawner()
{
    flows().clear();
    namespace fs = std::filesystem;
    const fs::path dir = "config/spawn_flows";
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
    {
        std::fprintf(stderr, "[spawn-flow] no config/spawn_flows directory; no flows loaded\n");
        std::fflush(stderr);
        return;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".json")
            continue;
        std::ifstream in(entry.path());
        if (!in)
        {
            std::fprintf(stderr, "[spawn-flow] cannot open %s\n", entry.path().string().c_str());
            std::fflush(stderr);
            continue;
        }
        try
        {
            nlohmann::json j;
            in >> j;
            FlowConfig cfg;
            if (!parseFlowJson(j, cfg))
                continue;
            // Validate archetype id resolvable. Spawn-side
            // spawnEnemyFromDecl also handles missing archetypes, but
            // an early validation here logs which flow is broken
            // instead of one spawn-time error every interval.
            if (selva::gameplay::archetypes().get(cfg.archetype_id) == nullptr)
            {
                std::fprintf(stderr, "[spawn-flow] '%s' archetype '%s' not registered; skipping\n",
                             cfg.id.c_str(), cfg.archetype_id.c_str());
                std::fflush(stderr);
                continue;
            }
            std::fprintf(stderr,
                         "[spawn-flow] loaded '%s' archetype=%s cap=%d interval=%.1fs "
                         "on_arrival='%s' delay=%.1fs\n",
                         cfg.id.c_str(), cfg.archetype_id.c_str(), cfg.active_cap,
                         cfg.spawn_interval_seconds, cfg.on_arrival_action.c_str(),
                         cfg.on_arrival_delay_seconds);
            std::fflush(stderr);
            flows().push_back({std::move(cfg), 0.0f, 0});
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "[spawn-flow] parse error in %s: %s\n",
                         entry.path().string().c_str(), e.what());
            std::fflush(stderr);
        }
    }
}

void tickFlowSpawner(float dt)
{
    for (auto& fs : flows())
    {
        tickInitialFill(fs);
        // Sweep slot occupancy BEFORE the spawn decision so any
        // vacated slots (from this frame's deaths) are available
        // for the trickle to claim immediately. No-op for non-slot
        // flows.
        sweepSlotOccupancy(fs);
        tickArrivals(fs);
        tickSpawnDecision(fs, dt);
    }
}

void resetFlowSpawner()
{
    for (auto& fs : flows())
    {
        fs.seconds_since_last_check = 0.0f;
        fs.initial_fill_done = false;
        fs.slot_occupant.clear();
        // spawn_counter preserved across reset so dynamic ids never
        // collide across cycles.
    }
}

} // namespace selva::spawn
