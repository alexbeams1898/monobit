#include "gameplay/PropSpawn.h"

#include "combat/CombatLog.h"
#include "insight/Insight.h"
#include "interact/Interaction.h"
#include "lang/Language.h"
#include "text/Examine.h"
#include "text/TextPresentation.h"
#include "world/Collision.h"
#include "world/Lights.h"
#include "world/Terrain.h"
#include "world/TreeAssets.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace selva::gameplay
{

namespace
{

// Build a CylinderCollider from a PropDecl + Tree-category archetype.
// Tree props become render-driving cylinders the existing
// WorldRenderer::renderTrees path iterates -- no separate render
// pipeline needed for v1.
//
// Resolves the variant index by archetype id (e.g. "pine_a" ->
// treeVariantIndexByName -> integer index). If the variant isn't
// loaded, the collider is still pushed (variant_idx=-1) so collision
// works; render simply finds no variant and skips draw.
// Build an engine LightSource from a PropDecl + Light-category
// archetype. The archetype's PropLightSource block supplies the
// defaults (color, intensity, range, flicker); the decl can override
// intensity + range per-instance so a single archetype serves both
// "main" and "companion" lights with size variance. region_name is
// passed through to the engine LightSource so the renderer's
// per-region cull works (lights of region X don't light region Y).
void spawnLightProp(const PropDecl& decl, const PropArchetype& arch, const char* region_name)
{
    if (!arch.has_light_source)
        return;
    engine::world::LightSource L;
    L.position = glm::vec3(decl.pos_x, decl.pos_y, decl.pos_z);
    L.color = glm::vec3(arch.light_source.color[0], arch.light_source.color[1],
                        arch.light_source.color[2]);
    L.intensity = (decl.light_intensity_override > 0.0f) ? decl.light_intensity_override
                                                         : arch.light_source.intensity;
    L.radius = (decl.light_range_override > 0.0f) ? decl.light_range_override
                                                  : arch.light_source.range_meters;
    L.flicker_amp = arch.light_source.flicker_amp;
    L.flicker_freq = arch.light_source.flicker_freq;
    L.region_name = region_name;
    // Use the decl id as the debug label if authored; else archetype
    // id. Both live in long-lived containers (saveData / archetype
    // registry) -- their c_str() is stable for the program's lifetime.
    L.debug_name = decl.id.empty() ? arch.id.c_str() : decl.id.c_str();
    engine::world::registerLight(L);
}

// Register an Examine interactable for a prop whose archetype carries
// examine_text or examine_text_key. Position is captured by value
// because props are placed once at boot and never move; the on_interact
// closure resolves text + insight subject by looking up the archetype
// in the global registry (stable for the program's lifetime).
//
// Mirrors the actor-side applyArchetypeInteractable -- same label
// cascade, same lang-key precedence, same notifyExamined hook -- so
// the doctrine "every examine fires an insight node" works
// identically for props and actors.
void registerPropExamineInteractable(const PropDecl& decl, const PropArchetype& arch)
{
    if (arch.examine_text.empty() && arch.examine_text_key.empty())
        return;
    // Label preference order matches the actor pattern:
    //   1. examine_label_key (lang-gated, preferred)
    //   2. display_name_key (lang-gated fallback)
    //   3. display_name (literal fallback)
    //   4. empty (renders as bare "[E] Examine" -- noun-stripped per pillar)
    std::string label;
    if (!arch.examine_label_key.empty())
        label = selva::lang::resolve(arch.examine_label_key);
    else if (!arch.display_name_key.empty())
        label = selva::lang::resolve(arch.display_name_key);
    else if (!arch.display_name.empty())
        label = arch.display_name;
    // Insight subject defaults to the archetype id when no
    // insight_trigger_subject is authored. Same convention as
    // actor-side examines (which use archetype id directly).
    const std::string subject =
        arch.insight_trigger_subject.empty() ? arch.id : arch.insight_trigger_subject;
    const glm::vec3 pos(decl.pos_x, decl.pos_y, decl.pos_z);
    const std::string archetype_id = arch.id;
    selva::interact::Decl idecl;
    idecl.kind = selva::interact::Kind::Examine;
    idecl.position = [pos]() { return pos; };
    constexpr float kDefaultExamineRangeMeters = 2.0f;
    idecl.range_meters = (arch.interact_range_meters > 0.0f) ? arch.interact_range_meters
                                                             : kDefaultExamineRangeMeters;
    idecl.label = label;
    idecl.on_interact = [archetype_id, subject]()
    {
        // Resolve archetype + text at interact time so a hot-reload
        // path (future) picks up edits without re-registering.
        const PropArchetype* a = propArchetypes().get(archetype_id);
        if (a == nullptr)
            return;
        std::string text;
        if (!a->examine_text_keys.empty())
        {
            // Tiered: pick the key by current examine count (capped
            // at last index). Mirrors the static-mesh JsonRegion
            // path; same notifyExamined-after-resolve ordering.
            const std::uint32_t cur = selva::insight::examineCountOf(subject);
            const std::size_t idx = std::min<std::size_t>(static_cast<std::size_t>(cur),
                                                          a->examine_text_keys.size() - 1);
            text = selva::lang::resolve(a->examine_text_keys[idx]);
        }
        else
        {
            text = a->examine_text_key.empty() ? a->examine_text
                                               : selva::lang::resolve(a->examine_text_key);
        }
        selva::text::beginExamine(text);
        selva::insight::notifyExamined(subject);
    };
    idecl.available = []() { return !selva::text::active(); };
    selva::interact::registerInteractable(std::move(idecl));
}

void spawnTreeProp(selva::world::CollisionRegion& region, const PropDecl& decl,
                   const PropArchetype& arch)
{
    selva::world::CylinderCollider c;
    c.center = glm::vec3(decl.pos_x, decl.pos_y, decl.pos_z);
    // Collider radius/half_height scale with the per-decl scale so a
    // 0.6-scale prop pushes proportionally tighter than a 0.9-scale
    // prop using the same archetype -- mirrors the existing C++
    // chapel-facade authoring where the smaller back row carried
    // tighter radius/height values.
    const float base_radius = (arch.cylinder_radius > 0.0f) ? arch.cylinder_radius : 0.30f;
    const float base_half_height =
        (arch.cylinder_half_height > 0.0f) ? arch.cylinder_half_height : 2.0f;
    const float scale = (decl.scale_override > 0.0f) ? decl.scale_override : 1.0f;
    c.radius = base_radius * scale;
    c.half_height = base_half_height * scale;
    c.forced_variant_idx = selva::world::treeVariantIndexByName(arch.id);
    c.forced_scale = decl.scale_override;
    region.cylinders.push_back(c);
    registerPropExamineInteractable(decl, arch);
}

} // namespace

void spawnPropsFromDecls(selva::world::CollisionRegion& region, const char* region_name,
                         const std::vector<PropDecl>& decls)
{
    int spawned = 0;
    int skipped = 0;
    for (const PropDecl& decl : decls)
    {
        const PropArchetype* arch = propArchetypes().get(decl.archetype);
        if (arch == nullptr)
        {
            selva::combat::combatLog(
                "[prop-spawn] decl id='{}' references unknown archetype '{}'; skipping",
                decl.id.c_str(), decl.archetype.c_str());
            continue;
        }
        switch (arch->category)
        {
        case PropCategory::Tree:
            spawnTreeProp(region, decl, *arch);
            ++spawned;
            break;
        case PropCategory::Light:
            spawnLightProp(decl, *arch, region_name);
            ++spawned;
            break;
        case PropCategory::Unknown:
            selva::combat::combatLog(
                "[prop-spawn] decl id='{}' archetype '{}' has Unknown category; skipping",
                decl.id.c_str(), decl.archetype.c_str());
            ++skipped;
            break;
        }
    }
    if (spawned > 0 || skipped > 0)
        std::fprintf(stderr, "[prop-spawn] spawned %d, skipped %d (of %zu decls)\n", spawned,
                     skipped, decls.size());
}

namespace
{

// True if (x, z) lies inside ANY of the rule's carve-out AABBs or
// circles. Used to exclude scatter samples from authored carve-outs
// (the walkway, the plateau, the wake-zone breathing circle, etc.).
bool inAnyExclude(const PropScatterRule& rule, float x, float z)
{
    for (const auto& e : rule.exclude_aabb_xz)
    {
        if (x >= e.x_min && x <= e.x_max && z >= e.z_min && z <= e.z_max)
            return true;
    }
    for (const auto& c : rule.exclude_circle_xz)
    {
        const float dx = x - c.center_x;
        const float dz = z - c.center_z;
        if (dx * dx + dz * dz < c.radius * c.radius)
            return true;
    }
    return false;
}

// True if any existing cylinder is within `min_dist` of (x, z). The
// legacy scatter used this rule to prevent overlap; preserved here
// so the dense-region collision feel stays identical.
bool tooCloseToExisting(const selva::world::CollisionRegion& region, float x, float z,
                        float min_dist)
{
    for (const auto& c : region.cylinders)
    {
        const float dx = c.center.x - x;
        const float dz = c.center.z - z;
        if (dx * dx + dz * dz < min_dist * min_dist)
            return true;
    }
    return false;
}

// Hash-pick an archetype id from the rule's archetype list using
// the sample's XZ position. Matches the existing render-time hash
// pick so the variant mix doesn't drift from authoring intent.
// Stable: same (x, z) yields same archetype across runs.
const std::string& pickArchetype(const std::vector<std::string>& archetypes, float x, float z)
{
    if (archetypes.size() == 1)
        return archetypes.front();
    // Lightweight position-hash. Combine the bits of x + z * prime
    // through a basic xorshift mixer; modulo into the list.
    std::uint32_t bits_x = 0u;
    std::uint32_t bits_z = 0u;
    std::memcpy(&bits_x, &x, sizeof(bits_x));
    std::memcpy(&bits_z, &z, sizeof(bits_z));
    std::uint32_t h = bits_x ^ (bits_z * 2654435761u);
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    return archetypes[h % archetypes.size()];
}

// Emit one cylinder from a (x, z, archetype) sample. Mirrors the
// authored-prop path through the same archetype lookup so the
// per-archetype collider size + variant lookup stay consistent.
// Also registers an Examine interactable when the archetype declares
// one -- a scattered tree is examinable on the same terms as an
// authored framing tree.
void emitScatterSample(selva::world::CollisionRegion& region, const std::string& archetype_id,
                       float x, float z, float trunk_r)
{
    selva::world::CylinderCollider c;
    c.center = glm::vec3(x, 0.0f, z);
    c.radius = trunk_r;
    // half_height was 2.0 for both passes in legacy populateAisle
    // / populateHubTrees; preserved here. Archetype's
    // cylinder_half_height is the AT-SCALE-1.0 default for authored
    // props but the scatter producer ignores it (the per-sample
    // trunk_r already captures size variance; height is uniform).
    c.half_height = 2.0f;
    c.forced_variant_idx = selva::world::treeVariantIndexByName(archetype_id);
    c.forced_scale = 0.0f; // hash-driven scale per render-time existing path
    region.cylinders.push_back(c);
    const PropArchetype* arch = propArchetypes().get(archetype_id);
    if (arch != nullptr)
    {
        PropDecl synthetic;
        synthetic.archetype = archetype_id;
        synthetic.pos_x = x;
        synthetic.pos_y = 0.0f;
        synthetic.pos_z = z;
        registerPropExamineInteractable(synthetic, *arch);
    }
}

// Aisle producer: parametric Z, mirrored sides, X-band uniform,
// per-sample jitter. Mirrors legacy populateAisle.
void runAisleScatter(selva::world::CollisionRegion& region, const PropScatterRule& rule)
{
    if (rule.archetypes.empty() || rule.count_per_side <= 0)
        return;
    std::mt19937 rng(rule.seed);
    std::uniform_real_distribution<float> radius_dist(rule.radius_range[0], rule.radius_range[1]);
    std::uniform_real_distribution<float> x_jitter(-rule.x_jitter, rule.x_jitter);
    std::uniform_real_distribution<float> z_jitter(-rule.z_jitter, rule.z_jitter);
    std::uniform_real_distribution<float> band(rule.x_band_inner, rule.x_band_outer);
    const float step_z = (rule.z_start - rule.z_end) / static_cast<float>(rule.count_per_side);
    for (int i = 0; i < rule.count_per_side; ++i)
    {
        const float z_base = rule.z_start - static_cast<float>(i) * step_z;
        for (int side = 0; side < 2; ++side)
        {
            const float sign = (side == 0) ? -1.0f : 1.0f;
            const float x = sign * band(rng) + x_jitter(rng);
            const float z = z_base + z_jitter(rng);
            if (inAnyExclude(rule, x, z))
                continue;
            const float trunk_r = radius_dist(rng);
            if (tooCloseToExisting(region, x, z, trunk_r))
                continue;
            emitScatterSample(region, pickArchetype(rule.archetypes, x, z), x, z, trunk_r);
        }
    }
}

// Disc producer: uniform disc sampling around origin_xz with
// boundary_radius cap. Mirrors the background scatter pass of
// legacy populateHubTrees. Honors min_distance_from_origin (the
// spawn-circle breathing room) and the optional terrain_region
// filter (selva_inner only).
void runDiscScatter(selva::world::CollisionRegion& region, const PropScatterRule& rule)
{
    if (rule.archetypes.empty() || rule.count <= 0)
        return;
    std::mt19937 rng(rule.seed);
    std::uniform_real_distribution<float> radius_dist(rule.radius_range[0], rule.radius_range[1]);
    std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * 3.14159265f);
    std::uniform_real_distribution<float> radial_dist(0.0f, 1.0f);
    int placed = 0;
    int attempts = 0;
    while (placed < rule.count && attempts < rule.max_attempts)
    {
        ++attempts;
        const float u = radial_dist(rng);
        const float r = std::sqrt(u) * rule.boundary_radius;
        const float a = angle_dist(rng);
        const float x = std::cos(a) * r + rule.origin_xz[0];
        const float z = std::sin(a) * r + rule.origin_xz[1];
        if (inAnyExclude(rule, x, z))
            continue;
        if (!rule.terrain_region.empty())
        {
            const auto* tr = selva::world::terrainRegionAt(x, z);
            if (tr == nullptr || tr->name != rule.terrain_region)
                continue;
        }
        const float trunk_r = radius_dist(rng);
        if (tooCloseToExisting(region, x, z, trunk_r))
            continue;
        emitScatterSample(region, pickArchetype(rule.archetypes, x, z), x, z, trunk_r);
        ++placed;
    }
}

} // namespace

void runPropScatterRules(selva::world::CollisionRegion& region,
                         const std::vector<PropScatterRule>& rules)
{
    int rules_run = 0;
    const std::size_t before = region.cylinders.size();
    for (const auto& rule : rules)
    {
        if (rule.mode == "aisle")
        {
            runAisleScatter(region, rule);
            ++rules_run;
        }
        else if (rule.mode == "disc")
        {
            runDiscScatter(region, rule);
            ++rules_run;
        }
        else
        {
            std::fprintf(stderr, "[prop-spawn] scatter rule with unknown mode '%s'; skipping\n",
                         rule.mode.c_str());
        }
    }
    if (rules_run > 0)
    {
        const std::size_t after = region.cylinders.size();
        std::fprintf(stderr, "[prop-spawn] scatter ran %d rules, emitted %zu cylinders\n",
                     rules_run, after - before);
    }
}

} // namespace selva::gameplay
