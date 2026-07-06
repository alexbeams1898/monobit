#pragma once

#include "dialog/Encounter.h"
#include "ecs/Items.h"
#include "gather/GatherState.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// Application state and persistence structures for Selva Oscura.
//
// Modeled on games/prison-escape-game/include/ecs/AppState.h. Many of the
// patterns (Phase enum + per-phase handler, UIState overlay, schema-versioned
// SaveData) are reused verbatim from prison-escape; Selva-specific values
// (Phase variants, schema fields) are scoped to Selva's cosmology.
//
// v1 schema is intentionally minimal: a character is just a name. The
// Signing moment (class pick vs unburdened, per setting.md) happens in
// the opening sequence inside Hell, not at the main menu, so the main-
// menu-side character-create only needs to elicit a name. Class, stats,
// sangue totals, evolution stage, keepers felled, etc. are added in
// later schema bumps as those systems ship.
// ---------------------------------------------------------------------------

namespace selva
{

// ---------------------------------------------------------------------------
// UIState - in-game overlay tracking. The pause menu and its tabs (Status,
// Inventory, Equipment, System) live here. GameState owns the top-level
// application mode; UIState owns the in-game overlay layered on top of
// Playing. Layout follows Elden Ring's convention - gameplay-data tabs
// (Status, Inventory, Equipment) and a System tab that holds Save / Settings
// / Quit-to-menu / Quit-to-desktop. Resume is universally ESC + RMB - not
// a button in any tab.
// ---------------------------------------------------------------------------
struct UIState
{
    enum class Screen
    {
        None,
        Menu,
    };

    // Pause-menu top-level tabs. Vessel absorbs the prior Status +
    // Equipment surfaces -- the Vagrant's form is one thing to regard,
    // not two separate views. Sub-page selection lives in
    // vessel_subpage so it persists across pause opens.
    enum class Tab
    {
        Vessel = 0,
        Inventory = 1,
        Craft = 2,
        System = 3,
    };

    // Vessel-tab sub-pages -- regions of the form the player attends
    // to. Overview is the landing page (high-level state); Form is
    // the body's substrate state (HP/Stamina/Poise + stats); Hands is
    // what they bear (equipment slots); Mind is what the Vagrant has
    // come to know (fired insights grouped by category). Imprint /
    // Eyes / etc. arrive as content for them ships.
    enum class VesselSubpage
    {
        Overview = 0,
        Form = 1,
        Borne = 2,
        Mind = 3,
    };

    Screen active_screen = Screen::None;
    Tab menu_tab = Tab::Vessel;
    VesselSubpage vessel_subpage = VesselSubpage::Overview;
    bool show_hud = true;
    bool input_suppressed = false;

    // Mind sub-page selection model (unified). One list spanning both
    // the library and the workbench. Each entry knows which surface
    // the click came from. Buttons derive their enable conditions
    // from this set + per-entry location. Clicking in one location
    // while items from the other are selected clears the other
    // automatically -- selections from library and workbench are
    // mutually exclusive. Session-only; reset on pause-menu close.
    struct MindSelectedItem
    {
        std::string id;
        enum class Location : std::uint8_t
        {
            Library = 0,
            Workbench = 1,
        } location = Location::Workbench;
    };
    std::vector<MindSelectedItem> mind_selection;
    // Reading picker state. When Infer matches an inference,
    // mind_picker_inference is set + the picker modal opens.
    // mind_picker_evidence is the linked evidence at commit time
    // (so the picker can show warranted-vs-not per reading).
    // mind_picker_existing_node is set when Reconsider opens the
    // picker for an already-placed inference (caller updates that
    // node in place instead of creating a new one).
    std::string mind_picker_inference;
    std::vector<std::string> mind_picker_evidence;
    std::string mind_picker_existing_node;
    // Two-stage Infer feedback. On Infer click the page shows a brief
    // feedback panel ('you can infer' / 'you cannot infer'); after a
    // short timer the panel either auto-opens the reading picker
    // (success) or closes (failure). 0 = no feedback active.
    std::uint64_t mind_feedback_open_ticks_ms = 0;
    bool mind_feedback_success = false;
    // Mind sub-page click-to-pick-up state. When non-empty, the
    // cursor is 'carrying' that node id. Clicking on the workbench
    // canvas places/transfers/moves the carried node to that
    // position; clicking the library panel returns to library
    // (lock-respecting); Esc or click outside both panels cancels.
    // mind_carrying_origin tracks where the carry started ('library'
    // or 'workbench') so the place logic knows whether to add a
    // new entry or update an existing one.
    std::string mind_carrying_id;
    enum class MindCarryOrigin : std::uint8_t
    {
        Library = 0,
        Workbench = 1,
    } mind_carrying_origin = MindCarryOrigin::Library;
    // Right-clicked node id, captured when the workbench context menu
    // opens. Empty when the menu was opened on empty canvas.
    std::string mind_context_target_id;

    // SDL_GetTicks64() value at the moment the last autosave completed.
    // Used by the save indicator chip on the HUD. We use SDL ticks here
    // (not selva::wallClock()) because the indicator must keep counting
    // down even when gameplay is paused - selvaPerFrame is gated off
    // during pause, so the gameplay wallclock freezes. 0 = no save this
    // session.
    std::uint64_t last_save_ticks_ms = 0;

    bool isScreenOpen() const
    {
        return active_screen != Screen::None;
    }
};

// ---------------------------------------------------------------------------
// GameState - top-level application-mode state machine. Each value names a
// distinct rendering and update path. The main loop dispatches on
// GameState::phase; screens (MainMenu, LoadGame, Settings) are stateless
// renderers that return an Action which transitions the phase. Character
// creation -- naming + class pick -- happens IN-WORLD via the Beat-3
// Guide-dialog beat + the Beat-4 picker; there is no main-menu CharCreate
// step. Per docs/design/character-creation.md.
//
// Selva phases differ from prison-escape: no Victory/GameOver/HighScores/
// RunSummary - Selva is roguelike, run-end loops back into Playing through
// a Wood-respawn rather than terminating. Those phases will be added when
// run-end + cycle structure ships.
// ---------------------------------------------------------------------------
struct GameState
{
    enum class Phase
    {
        MainMenu,
        LoadGame,
        Settings,
        // Pre-Selva soul-shaping. Player adjusts the appearance
        // schema's identity-defining fields + claims a name BEFORE
        // any world / cosmology renders. Confirmed by the
        // CharacterCreationScreen, which atomically writes
        // appearance + name onto the PlayerProfile and transitions
        // to Playing with the wake-scene flag. Quit-during this
        // phase = no PlayerProfile written (any placeholder added
        // by the New Game button gets discarded).
        CharacterCreation,
        Playing,
    };

    Phase phase = Phase::MainMenu;
    bool world_initialized = false;
    // Defers world creation by one tick after the player selects new/load
    // game, so the loading overlay can render before the world spins up.
    bool pending_world_create = false;
    // Set true ONLY on the New-Game path (not Load-Game). Consumed by
    // selvaPerFrame on the first frame after world creation to fire the
    // wake-up animation Scene. The Vagrant wakes only on the first
    // arrival of a save; subsequent respawns place him standing.
    bool pending_wake_scene = false;
    std::string active_character; // Name of the character for the current run.

    // --- Boss-encounter active state (per docs/design/ideas/boss_backend.md
    // section 6) ---

    // Pool index of the actor that's the active boss this frame, or -1
    // if no boss is engaged. Stored as INDEX (not pointer) because the
    // actor pool may resize between frames; resolve to pointer at use
    // site via selva::gameplay::actors()[active_boss_idx]. Set when an
    // engage trigger fires (or a SpawnEntity trigger spawns a boss);
    // cleared when the boss dies (post-felled-overlay).
    int active_boss_idx = -1;

    // The active boss's spawn-decl id (e.g. "lupa"). Stable across
    // frames even if the pool resizes -- used as the persistent key
    // for save's felled_bosses list when the boss dies. Empty when no
    // boss engaged.
    std::string active_boss_id;
};

// ---------------------------------------------------------------------------
// PlayerClass -- the Vagrant's cosmological identity, locked at Beat 4 via
// the Signing modal. `None` is the pre-Beat-4 state (player hasn't faced
// the Guide yet). Per setting.md *The Signing* + locked
// [[project_crucible_censer_leveling_system]].
//
// Cosmologically: the choice determines which commit-fire the Guide installs
// in the Vagrant. Penitent/Heretic/Ferine receive the chrism-fire (the
// Crucible verb -- feed self / install into substrate). Unburdened receives
// the channel-fire (the Censer verb -- feed Beatrice / route to her reservoir).
// Same fire, opposite mouths. The absorption-capacity is path-independent
// (Vagrant-exception per [[project_imprint_handle_required_for_sangue]]).
//
// Naming history: the third class was originally "Wretched" (retired
// 2026-06-11) -> "Feral" (working name) -> "Ferine" (locked 2026-06-14
// per [[project_class_stats_v2_locked_2026_06_14]]). The enum integer
// value (3) is unchanged so save back-compat for the enum integer holds;
// SaveManager string parsing accepts both "Wretched" and "Ferine".
// ---------------------------------------------------------------------------
enum class PlayerClass : std::uint8_t
{
    None = 0,
    Penitent = 1,
    Heretic = 2,
    Ferine = 3,
    Unburdened = 4,
};

// Returns the stable JSON serialization string for a class. Used for
// save/load round-trip. Symmetric with parsePlayerClass.
const char* playerClassName(PlayerClass c);

// Parse a class name (case-sensitive, matches playerClassName output).
// Returns None on unknown / empty / null input (defensive for old saves).
PlayerClass parsePlayerClass(const std::string& name);

// True when the class carries the chrism-fire (Crucible verb -- feed self).
// False for Unburdened (carries the channel-fire instead -- Censer verb,
// feed Beatrice) and for None (no commit verb yet, pre-Beat-4).
bool isClassPickerPath(PlayerClass c);

// ---------------------------------------------------------------------------
// PlayerProfile - persistent character identity. Schema starts minimal (just
// a name) and grows via schema_version bumps as more systems ship.
//
// Anticipated future fields (not yet in schema; documented for reference):
//   - evolution_stage:      L1/L2/L3 for class-pickers, Unburdened/Svuotato/
//                           Diaphanous for unburdened
//   - stats:                HP / fire_rate / damage (per CLAUDE.md three-stat
//                           constraint)
//   - lifetime_riversato:   cumulative sangue routed to Beatrice via the
//                           Censer commit-verb (unburdened path total)
//   - keepers_felled:       set of "Charon", "Minos", etc. - per setting.md
//                           Per-circle reactivity (drives world-state)
//   - wood_marks:           cairns, etched names, riversamento sites placed
//                           in the Wood (per wood.md persistence)
//   - grimoire_unlocks:     list of Grimoire entries the player has seen
// ---------------------------------------------------------------------------
struct PlayerProfile
{
    std::string name;

    // Last position + facing yaw when the character was saved. Used to
    // restore where the player was on Playing-enter (quit-to-menu and
    // resume returns you to where you were, not to the world spawn).
    // `has_saved_pose` distinguishes "new character, no save yet" from
    // "character saved with literal (0,0,0)". Without the flag, a
    // newly-created character would resume at world origin instead of
    // the configured spawn point.
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float pos_z = 0.0f;
    float yaw = 0.0f;
    bool has_saved_pose = false;

    // Region the player was in when saved. Player pos above is in this
    // region's local coordinate space. Missing / empty = "surface"
    // (back-compat for save files written before the Regions system
    // existed). When the region-aware load runs:
    //   1. activateRegionImmediate(findRegionId(current_region_id))
    //   2. teleport player capsule to (pos_x, pos_y, pos_z), yaw
    std::string current_region_id;

    // Bosses this character has felled. Per
    // games/selva-oscura/docs/design/ideas/boss_backend.md section 5.
    // When a boss actor dies (`actor.is_boss && actor.is_dead`) its
    // spawn-decl id is appended here. On region load, any
    // `enemy_spawns[]` entry whose id appears in this list is SKIPPED
    // entirely -- the boss does not respawn, the slope stays empty
    // for the rest of the save. Per [[selva-wood-lore-locked-2026-05-31]]
    // the empty slope IS the monument.
    //
    // Future: this list may merge with `keepers_felled` (per setting.md
    // *Cycle structure*) since keepers ARE a subset of bosses. Kept
    // separate for v1 until the first keeper ships.
    std::vector<std::string> felled_bosses;

    // Generic quest-state flag set. Persistent string set. Used by:
    //   - Dialogue trees (gate branches on flag presence)
    //   - NPC spawn conditions (only spawn if flag X is set)
    //   - Scripted-event triggers (fire X once if flag Y not set)
    //   - Future: achievement system (derive Steam/PSN achievements
    //     from declarative conditions over this set)
    // Names are STABLE identifiers (renaming = breaking a save); use
    // semantic prefixes like "lupa_felled", "met_guide", "grimoire_5".
    // Access through hasFlag / setFlag / clearFlag in AppStateGlobal.h
    // -- those normalize dedup behavior so direct push_back is never
    // necessary. Empty = no flags set yet (back-compat default for
    // saves written before the flag system shipped).
    std::vector<std::string> flags;

    // Per-character insight nodes the Vagrant has unlocked. Stable
    // node id strings (e.g. "knows_guide", "knows_lupa",
    // "knows_acheron_pile"). The language map's tier promotions
    // (selva::lang::resolve) check membership here via
    // selva::lang::isUnlocked(node_id) -- every tier-2 reveal flows
    // through this single set. Per the doctrine, each Vagrant starts
    // fresh: the set is empty on character create and rebuilt across
    // play by the insight graph's trigger evaluators
    // (selva::insight::tick).
    //
    // Vector (sorted, deduped) for save/load symmetry with flags. Use
    // the helpers in AppStateGlobal: hasInsight / setInsight / clearInsight.
    std::vector<std::string> unlocked_insights;

    // Per-character cumulative kill counts by archetype id. Insight
    // graph accumulator triggers (kill_count) read from this map; the
    // fireEnemyDeath path writes to it. Authored archetype ids match
    // config/enemies/<id>.json. Sparse: only archetypes the player
    // has killed appear.
    std::unordered_map<std::string, std::uint32_t> kill_counts;

    // Conclusions that have been promoted from uncertain to certain --
    // the player both deduced them AND received the confirming
    // observation. Subset of unlocked_insights: every id in here is
    // also in unlocked_insights; a conclusion in unlocked_insights but
    // NOT in here is uncertain. Conclusions without a confirmed_by
    // list never go through uncertain; they're added to BOTH sets at
    // the moment of deduction.
    std::vector<std::string> certain_conclusions;

    // Mind sub-page persistent workbench. The workbench is the player's
    // self-arranged graph of their understanding: observations they have
    // committed to thinking about, conclusions they have deduced from
    // them, all with player-set positions. Per the locked design
    // (2026-06-09 workbench-as-designer): observations enter the
    // workbench via drag from the library; once placed they persist
    // across sessions. Conclusions appear here when deduced and stay
    // permanently. The library only shows observations NOT on the
    // workbench. Positions are in 0..1 normalized canvas space.
    struct WorkbenchNode
    {
        std::string id;
        float x = 0.5f;
        float y = 0.5f;
        // For inferences: the observation ids the player ACTUALLY
        // selected at the moment of the Deduce act. Edges are drawn
        // only from this list -- never from the node's authored
        // requires. Locked once committed (cannot be removed while
        // the inference is on the workbench).
        std::vector<std::string> linked_observations;
        // For inferences (per cognition-system v1): the authored
        // reading id the player picked at Deduce time (or changed via
        // Reconsider). The reading's prose is what displays. The
        // reading's warrant_evidence is what determines whether the
        // inference is warranted (linked_observations exact-set-
        // matches the reading's warrant_evidence).
        std::string reading_id;
        // DEPRECATED 2026-06-10 (cognition-system v1 replaced the
        // confirmation mechanic with readings + warrant). Field kept
        // so old saves don't error on load; never populated by new
        // code; never displayed.
        std::vector<std::string> linked_confirmers;
    };
    std::vector<WorkbenchNode> workbench_observations;
    std::vector<WorkbenchNode> workbench_inferences;

    // Cognitive engagement counters per cognition-system v1. Each is
    // raw count of qualifying events; the stat value is derived via
    // computeCognitiveStat() = 1 + floor(log2(counter + 1)).
    // Diminishing returns naturally fall out of the log curve. The
    // counters are NOT decremented when Reconsider cascades release
    // child inferences -- the simpler v1 model preserves growth so
    // the system stays predictable. Future revision may track gains
    // per inference and decrement on cascade.
    std::uint32_t perception_growth = 0;
    std::uint32_t cognition_growth = 0;
    std::uint32_t intelligence_growth = 0;

    // Per-subject examine count. Sparse: only subjects the player has
    // examined appear. Drives multi-tier examine text (PropArchetype
    // examine_text_keys[] indexes by count) and Nth-time-examined
    // insight flags ("examined:<subject>:<N>"). Keyed by the same
    // subject string passed to selva::insight::notifyExamined.
    std::unordered_map<std::string, std::uint32_t> examine_counts;

    // Mastery-gated crafting state.
    //
    // `craft_counts`: per-recipe success counter, incremented on every
    // successful engine::ops::crafting::craft(). Keyed by recipe
    // config_path. Drives the unlock chain (Poultice -> Salve ->
    // Electuary -> Theriac per [[project_healing_system_locked_2026_06_14]])
    // via the recipe's `unlock_after` threshold.
    //
    // `known_recipes`: set of recipe config_paths the player can
    // currently craft. The Craft UI filters by this list. Starts with
    // recipes the player has been TAUGHT (today: Guide teaches Poultice
    // at Signing) and grows as unlock thresholds fire.
    std::unordered_map<std::string, std::uint32_t> craft_counts;
    std::vector<std::string> known_recipes;

    // Quick-slot rotation -- the Souls/ER consumables wheel. List of
    // item config_paths the player has explicitly assigned to be
    // cyclable in combat via X (or Shift+X to reverse). Q uses the
    // currently-primed entry. Assignment is by config_path (not
    // ItemInstanceId) so the slot survives crafting new instances of
    // the same item type. Capacity bounded at kQuickSlotCapacity below
    // (Souls = 5). Order is insertion order; the player rearranges via
    // the inventory UI's Assign verb.
    std::vector<std::string> quick_slot_assigned;

    // Index into quick_slot_assigned of the currently-primed entry.
    // -1 = none primed (also the state when assigned is empty). Cycle
    // hotkeys (X) move this; use hotkey (Q) consumes one of the primed
    // item from inventory. Wraps modulo size. Persists across save.
    int quick_slot_primed_index = -1;

    // Persistent door state. (door_id, state_name) pairs. Only doors
    // whose state has DEVIATED from their JSON-authored initial_state
    // need entries here. State_name is one of "Locked", "Closed",
    // "Open" (Opening is promoted to Open on save -- mid-animation
    // doesn't persist). Per [[world/Door.h]].
    std::vector<std::pair<std::string, std::string>> door_states;

    // Live gather-node population. One entry per spawned Wood gather
    // node, with the quality ROLLED AT SPAWN TIME (anti-cheese
    // commit-on-spawn -- pickup grants the persisted quality, never
    // re-rolls). Persists across save/load so quit-reload returns to
    // the same world state. Cycle reset (second death) clears this and
    // the GatherSpawner re-rolls initial fill. See
    // [[project_healing_system_locked_2026_06_14]] +
    // [[project_anti_cheese_rolls_locked_2026_06_14]].
    std::vector<selva::gather::NodeState> active_gather_nodes;

    // Monotonic id allocator for active_gather_nodes. Never decrements;
    // never reuses an id once assigned. Stable across save/load so any
    // outstanding Interactable handle keyed on a NodeState.id stays
    // valid after a reload.
    std::uint32_t next_gather_node_id = 1;

    // Per-gather-flow scheduler state. One entry per loaded gather
    // node config in `config/gather_nodes/`. Today there's a single
    // unified `wood_forage` flow so this is effectively one entry, but
    // the vector keeps the door open for additional zones (Hell-side
    // forage when a circle adds one, etc). Mirrors FlowSpawner's
    // interval-based trickle but PERSISTS across save/load (FlowSpawner
    // does NOT -- gather diverges here per anti-cheese doctrine).
    std::vector<selva::gather::FlowState> gather_flows;

    // Per-character carried items, category-bucketed
    // (engine::ecs::Inventory). Bucket keys are the engine category
    // string ids ("weapons", "armor", "consumables", "key_items",
    // "materials", "accessories", "incantations", "invocations") and
    // also serve as the tab ids in config/inventory_categories.json.
    // Each item carries a stable ItemInstanceId; Equipment slots
    // address by id, not by index. Operate via
    // engine::ops::inventory::* (addItem/removeItem/equipItemToSlot/
    // findById/etc). Empty for new characters.
    engine::ecs::Inventory inventory;

    // Currently equipped items per slot. Each slot holds the stable
    // ItemInstanceId of the equipped item, or kInvalidItemInstanceId
    // if nothing equipped. Survives any inventory mutation (ids are
    // stable). Empty for new characters.
    engine::ecs::Equipment equipment;

    // Compendium of item config_paths the Vagrant has ever picked up.
    // Persists across deaths within a run (vestigia remember; per
    // [[project_selva_core_framing]]) and serializes to save. Used to
    // colorize the pickup notification (gold for first-time-ever vs
    // neutral for repeat). Empty for new characters.
    engine::ecs::Compendium compendium;

    // Unread notices the player has discovered but not yet
    // acknowledged in the UI. Cross-domain set keyed
    // "<domain>:<id>" so any future "you have new X" surface
    // (items, insights, NPC topics, recipes, bestiary, regions)
    // shares one storage layer instead of growing parallel fields.
    // Operate via selva::notice::* (mark/isUnread/acknowledge);
    // never read/write directly. Persists across deaths within a
    // run + serializes to save. Empty for new characters.
    std::unordered_set<std::string> unread_notices;

    // Per-NPC encounter history (sparse). Only NPCs the player has
    // talked to have entries. Use selva::npcEncounter(profile, id)
    // to lazily create + read. Stable npc_ids match the topic
    // registry (config/npcs/<id>.json).
    std::unordered_map<std::string, selva::dialog::NpcEncounterState> npc_state;

    // Cumulative sangue ever collected across all cycles. Persistent;
    // never resets. Hard-capped at SANGUE_LIFETIME_CAP (9^9 numerologically
    // = 9 circles completed). Per setting.md *Hell's accounting cap* the
    // renderer caps the displayed roman-numeral form at 3,999,999 (the
    // vinculum cap), but the underlying ledger continues past that until
    // the lifetime hard-cap. Counts EVERY sangue grant -- bookkeeping
    // separate from the per-cycle vessel.
    std::uint32_t sangue_lifetime = 0;

    // Substance currently held in the Vagrant's vessel (the
    // Crucible for class-pickers, the Censer for the unburdened, both
    // TBD at Beat 4). Per [[project_crucible_censer_leveling_system]]
    // the vessel IS the holding zone (no separate wallet); the HUD
    // reads this field. Resets each cycle (uncommitted contents return
    // to Hell on second death; see setting.md *Hell reclaims its
    // substance from the dead*). For Increment 1, vessel is generic
    // (no class distinction yet); class-specific Crucible/Censer
    // mechanics arrive when the class-picker UI lands.
    std::uint32_t sangue_vessel = 0;

    // Cumulative sangue routed to Beatrice's reservoir across all cycles.
    // Per setting.md *The unburdened path* + locked
    // [[project_crucible_censer_leveling_system]]: this is the unburdened's
    // progression axis -- every Censer commit adds to this total and
    // contributes to the unburdened's subtractive evolution
    // (Unburdened -> Svuotato -> Diaphanous). Class-pickers leave this at 0;
    // their progression goes into substrate (stats / abilities) instead.
    // Capped at SANGUE_LIFETIME_CAP, same ceiling as sangue_lifetime.
    std::uint32_t sangue_riversato = 0;

    // Cosmological identity locked at Beat 4 via the Signing modal. Default
    // None for pre-Beat-4 (player hasn't faced the Guide's dialog choice
    // yet). Per setting.md *The Signing and the commit-fire*.
    PlayerClass player_class = PlayerClass::None;

    // Path to the AuthoredCharacter JSON driving this character's
    // visual body (body_scale, morphs, tint) AND the identity slice
    // (stats / player_class / hand items) when the file carries
    // them (has_* flags). Empty = default character. v1 defaults to
    // a shared "default_humanoid" file; the Effigie designer writes
    // per-character variants here.
    std::string character_path;
};

// Hard ceiling on lifetime sangue. 9^9 numerologically -- "all of Hell,
// completed" (9 circles, raised to the power of 9). Per cosmology lock
// 2026-06-04. Saturating add at this value; further grants no-op.
inline constexpr std::uint32_t SANGUE_LIFETIME_CAP = 999'999'999u;

// Maximum entries in PlayerProfile.quick_slot_assigned. Souls / ER's
// quick-slot rotation caps at 5 — small enough that X-cycling lands on
// the right item in 2-3 presses, big enough to hold all current Wood
// consumables plus 1-2 future categories.
inline constexpr int kQuickSlotCapacity = 5;

// ---------------------------------------------------------------------------
// Settings - persistent user preferences. Lives at the save level (not
// per-character) because settings apply to the whole install.
// ---------------------------------------------------------------------------
struct Settings
{
    float bgm_volume = 0.8f;
    float sfx_volume = 1.0f;
    // Camera FOV in degrees. Separate values for third-person (default
    // 60, narrow soulslike framing) and first-person (default 75,
    // wider for spatial awareness when the player can't see their own
    // body). User-tunable via the Settings screen.
    float fov_degrees_third_person = 60.0f;
    float fov_degrees_first_person = 75.0f;
    // World-space focus ring drawn around interactables (E-prompt
    // targets). On by default; the player can hide it from the
    // Settings screen if they prefer a cleaner HUD. The screen-space
    // "[E] Talk" / "[E] Open" prompt still renders regardless --
    // the ring is purely a visual aid for finding the in-world target.
    bool show_interact_ring = true;

    // QoL: when a newly-discovered consumable is granted to the
    // player's inventory AND there's an empty quick_slot_assigned
    // entry on the active profile, auto-append to it. Off by default
    // (Souls convention: assignment is explicit). On = no friction;
    // pick up a Poultice, it's instantly Q-able. Per-install setting
    // because some players prefer the deliberate workflow.
    bool auto_assign_consumables_to_quick_slot = false;
};

// ---------------------------------------------------------------------------
// SaveData - top-level persistent data. Serialized to JSON at
// %APPDATA%/SelvaOscura/save.json (Windows) or platform equivalent via
// SDL_GetPrefPath. Schema versioning enables forward-compatible migrations.
// ---------------------------------------------------------------------------
struct SaveData
{
    static constexpr int CURRENT_VERSION = 10;

    int schema_version = CURRENT_VERSION;
    std::vector<PlayerProfile> characters;
    Settings settings;

    // Souls-style Continue: name of the character most recently
    // saved/played. has_last_played distinguishes "no character has
    // been played yet" (hide Continue) from "the unnamed character was
    // most recent" (resume the empty-name profile, per the unnamed-
    // but-real character pattern). Updated by flushAndSave before
    // SaveManager::save persists the file.
    std::string last_played_character;
    bool has_last_played = false;
};

} // namespace selva
