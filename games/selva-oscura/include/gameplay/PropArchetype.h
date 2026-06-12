#pragma once

// PropArchetype -- JSON-authored spawn template for non-actor things
// in the world (trees, lights, ambient props, future cairn-stones,
// vestigia, markers, etc).
//
// Sibling to EnemyArchetype. Both are spawn templates that produce
// entt::entity instances with components attached; neither inherits
// from the other. Per the locked design (see this conversation's
// schema sketch + future docs/design/props.md):
//
//   - Runtime: there is no Prop class. A "prop" is just an
//     entt::entity with prop-shaped components attached. The
//     archetype is JUST the authoring schema.
//   - Authoring: PropArchetype JSON in config/props/*.json, declared
//     in region.json via the props[] array (single-instance) or
//     prop_scatter[] (procgen). Both produce PropDecl records that
//     flow through the spawnPropFromDecl funnel.
//   - The diamond funnel (applyPropArchetypeToEntity) is the single
//     site that wires archetype-driven state onto an entity. Adding
//     a new archetype-derived field means updating ONE site -- the
//     same doctrine as applyArchetypeToActor for enemies.
//
// What this schema deliberately does NOT carry:
//   - AI fields. Props don't think; ticking props get dedicated
//     components, not AI archetype fields.
//   - Health / hurtbox / combat fields. No destructible props in v1.
//   - Animation / skeleton fields. Props are static-mesh only.
//   - Faction / form. Props aren't actors; category enum below is
//     the prop equivalent.
//   - Inheritance from EnemyArchetype. Explicitly siblings.

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::gameplay
{

// Coarse categorization mirroring EnemyArchetype's `form` enum.
// Each category is a CLOSED enum value; adding more requires
// doctrine (this is the prop-equivalent of "expanding the five
// forms"). v1 enumerates only what trees + lights need; later
// passes add categories as the lore demands them.
enum class PropCategory : std::uint8_t
{
    Unknown = 0,
    Tree = 1,
    Light = 2,
};

// Collision shape enum. v1 supports the union of what trees +
// authored static meshes use today.
enum class PropCollisionKind : std::uint8_t
{
    None = 0,
    Cylinder = 1,
    Aabb = 2,
    // Trimesh deferred -- relies on the same static-mesh loader path
    // JsonRegion already uses for authored architectural meshes;
    // wired when a prop archetype needs trimesh collision.
};

// Optional light-source block. Populated only for archetypes whose
// category=Light (or future arrangements). Mirrors the data
// registerLimboLights hardcodes today, lifted into per-archetype
// config so each Limbo light becomes a one-line authored entry.
struct PropLightSource
{
    std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range_meters = 0.0f;
    // Flicker parameters -- mirror the engine LightSource fields.
    // amp = 0 = no flicker (steady). freq = Hz.
    float flicker_amp = 0.0f;
    float flicker_freq = 0.0f;
    std::string kind; // "campfire" / "brazier" / "fungus" / etc -- renderer/audio system hook
};

// Optional harvestable resource block. Forward-allocated per the
// schema sketch: limbo.md anticipates light vignettes becoming
// harvestable nodes when crafting ships, and pre-allocating the
// shape now is one optional block + cheap. v1 trees + lights don't
// set this; archetypes that ship harvestable behavior in future
// fill it in.
struct PropHarvestable
{
    std::string resource_id;
    int yield_amount = 0;
    float cooldown_seconds = 0.0f;
    bool depletes = false;
};

struct PropArchetype
{
    // --- Identity ---
    std::string id;
    // Inheritance is a load-time directive consumed by the registry
    // (merge_patch chain), NOT a struct field at runtime. Strip
    // before deserialize. Mirrors EnemyArchetype's pattern.
    PropCategory category = PropCategory::Unknown;

    // --- Visual representation ---
    // Path to the source mesh asset. Optional: archetypes with no
    // mesh are invisible (place-based examines, ambient zones).
    std::string mesh_path;
    // Which gltf nodes to pull from the source mesh. Empty = take
    // all primitives. Mirrors how TreeAssets filters node names
    // today.
    std::vector<std::string> mesh_node_filter;
    // Per-archetype texture overrides keyed by material/node name.
    // The loader swaps the asset's authored texture for the override
    // path. Used for dead-foliage variants without authoring new
    // mesh files. Optional.
    std::unordered_map<std::string, std::string> texture_overrides;
    // Random scale jitter range applied per-instance at spawn. Two
    // values [min, max] sampled uniformly. Empty / single-value =
    // no jitter. Mirrors the existing tree scale spread.
    std::array<float, 2> scale_jitter_range = {1.0f, 1.0f};
    // Alpha-cutout threshold for foliage cards; 0 means no cutout.
    // Matches the existing tree branches material's authored intent.
    float alpha_cutoff = 0.0f;
    // Foliage tint multiplier applied in the tree shader. Identity
    // = (1,1,1); dead-wood = warm grey-brown < 1.0. Lifts the
    // hardcoded WorldRenderer constant into per-archetype config so
    // each archetype carries its own tint.
    std::array<float, 3> foliage_tint = {1.0f, 1.0f, 1.0f};

    // --- Collision ---
    PropCollisionKind collision_kind = PropCollisionKind::None;
    float cylinder_radius = 0.0f;
    float cylinder_half_height = 0.0f;
    std::array<float, 3> collision_aabb_half_extents = {0.0f, 0.0f, 0.0f};

    // --- Interactability ---
    // The Kind of interactable prompt. Empty = no prompt. Mirrors
    // the existing static-mesh + actor-archetype Examine/Talk/Open
    // pattern; uses the same selva::interact registry and the same
    // lang-map keys.
    std::string interactable_kind; // "Examine" / "Pickup" / "Use" / ""
    float interact_range_meters = 0.0f;
    std::string examine_text;
    std::string examine_text_key;
    // Multi-tier examine text keys. Same pattern as the static-mesh
    // path: when non-empty, indexed by the player's current examine
    // count (capped at last index). Wins over single examine_text_key.
    std::vector<std::string> examine_text_keys;
    std::string examine_label_key;
    std::string display_name;
    std::string display_name_key;
    // Story-gated visibility: if non-empty, the prompt only appears
    // when this flag is set on the active profile. Sibling of
    // EnemyArchetype::talk_requires_flag.
    std::string interactable_requires_flag;

    // --- Insight ---
    // Subject string passed to selva::insight::notifyExamined when
    // an examine fires. Defaults to `id` if empty -- mirrors the
    // existing actor-archetype convention.
    std::string insight_trigger_subject;

    // --- Optional sub-systems ---
    // Populated only when the archetype declares the corresponding
    // JSON block. Empty/default values = unused.
    PropLightSource light_source;
    bool has_light_source = false;
    PropHarvestable harvestable;
    bool has_harvestable = false;
};

// Free-function adapter so nlohmann/json can deserialize directly:
// PropArchetype a = j.get<PropArchetype>();
void from_json(const nlohmann::json& j, PropArchetype& a);

// Process-wide archetype registry. Loaded once at boot via
// loadDirectory("config/props"); read by region/spawn code via
// propArchetypes(). Pattern mirrors EnemyArchetypeRegistry.
class PropArchetypeRegistry
{
  public:
    void loadDirectory(const std::filesystem::path& dir);

    // nullptr if no archetype with that id was loaded.
    const PropArchetype* get(const std::string& id) const;

    const std::unordered_map<std::string, PropArchetype>& all() const
    {
        return by_id;
    }

  private:
    std::unordered_map<std::string, PropArchetype> by_id;
};

// Single global registry. Pattern matches archetypes() for enemies.
PropArchetypeRegistry& propArchetypes();

// Carve-out region: any prop sample landing inside this XZ AABB is
// rejected. Used to keep scatter out of the walkway, the plateau,
// or future authored zones.
struct PropExcludeAabbXZ
{
    float x_min = 0.0f;
    float x_max = 0.0f;
    float z_min = 0.0f;
    float z_max = 0.0f;
};

// Carve-out circle. Sibling of PropExcludeAabbXZ; used for the
// breathing-room circles around the wake zone, future safe spots,
// or any other circular carve.
struct PropExcludeCircleXZ
{
    float center_x = 0.0f;
    float center_z = 0.0f;
    float radius = 0.0f;
};

// Scatter rule -- a procgen producer that emits PropDecl records at
// region commit. Two modes today (aisle / disc); both share the
// same archetype selection + exclude_aabb_xz carve-out pattern.
// The schema is intentionally narrow for v1; new modes get added as
// new scatter shapes become load-bearing.
//
// `archetypes` is the candidate list; the producer hash-picks one
// per sample using the sample's XZ position, matching the
// hash-driven variant rendering that already runs at draw time.
struct PropScatterRule
{
    // Common
    std::string mode; // "aisle" / "disc"
    std::vector<std::string> archetypes;
    std::uint32_t seed = 0u;
    std::array<float, 2> radius_range = {0.30f, 0.30f};
    std::vector<PropExcludeAabbXZ> exclude_aabb_xz;
    std::vector<PropExcludeCircleXZ> exclude_circle_xz;

    // Aisle mode
    float z_start = 0.0f;
    float z_end = 0.0f;
    int count_per_side = 0;
    float x_band_inner = 0.0f;
    float x_band_outer = 0.0f;
    float x_jitter = 0.0f;
    float z_jitter = 0.0f;

    // Disc mode
    std::array<float, 2> origin_xz = {0.0f, 0.0f};
    float boundary_radius = 0.0f;
    int count = 0;
    int max_attempts = 0;
    // If non-empty, only emit on this terrain region (e.g.
    // "selva_inner" -- trees scatter only on the inner-Wood
    // terrain, not Limbo).
    std::string terrain_region;
};

// Per-instance prop declaration, mirroring EnemySpawnDecl. Authored
// in region.json's props[] array; the spawn funnel reads this +
// the referenced archetype and produces an entity with components.
// Optional id supports referenced-by-name use cases (debug,
// triggers, future scripted events that need to find a specific
// prop instance).
struct PropDecl
{
    std::string id; // optional; non-empty when authored
    std::string archetype;
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float pos_z = 0.0f;
    // When true, pos_y was authored as the string "auto_terrain" and
    // the spawn funnel must sample the terrain at (pos_x, pos_z) at
    // spawn time. Mirrors EnemySpawnDecl::pos_y_auto_terrain.
    bool pos_y_auto_terrain = false;
    float yaw = 0.0f;
    // Per-instance scale override. 0 = use the archetype's
    // scale_jitter_range default (or a per-decl jitter sample).
    float scale_override = 0.0f;
    // Per-instance light overrides (only consumed for Light-category
    // props). 0 = inherit the archetype's PropLightSource default.
    // Lets a single "warm_light" archetype serve both main + companion
    // instances with per-decl intensity/range variation rather than
    // proliferating archetype files.
    float light_intensity_override = 0.0f;
    float light_range_override = 0.0f;
};

// Category / collision-kind parse helpers (used by from_json + by
// any caller that needs to convert string<->enum). Defined in the
// .cpp; declared here so loaders elsewhere can reuse them.
PropCategory parsePropCategory(const std::string& s);
PropCollisionKind parsePropCollisionKind(const std::string& s);

} // namespace selva::gameplay
