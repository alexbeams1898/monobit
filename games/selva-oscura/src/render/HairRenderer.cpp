#include "render/HairRenderer.h"

#include "anim/SkeletalAssets.h"
#include "anim/SkeletalMesh.h"
#include "anim/SkeletalRenderer.h"
#include "combat/ActorVolumes.h" // buildActorModelMatrix
#include "gameplay/Actor.h"
#include "gameplay/Appearance.h"
#include "hair/HairRegistry.h"

#include <glm/glm.hpp>

#include <cstdio>
#include <string>
#include <unordered_set>

namespace selva::render
{

namespace
{

// Which viewProj + model matrix hair uses matches the actor's body
// draw exactly. Instead of threading them through drawHair we grab
// the state the skeletal-mesh renderer already knows: caller sets a
// per-frame viewProj via setSkeletal* uniforms, and the model matrix
// is composed the same way buildActorModelMatrix does for the body.
//
// We do NOT keep a separate hair mesh cache -- meshByArchetypePath
// already provides a process-wide cache keyed by mesh path with
// inverse-bind matrices resolved against the passed skeleton, which
// is exactly what we want for hair too. Same cache = one code path,
// one cleanup, one preload story.

// Memo of "we tried this hair path and it failed to load, don't spam
// the log again this session." Not strictly needed because
// meshByArchetypePath falls back to the default skeleton bundle on
// failure, but keeping the check here means we don't emit a garbage
// draw call for an unknown style_id whose registry entry points at a
// deleted file.
std::unordered_set<std::string>& loadFailureMemo()
{
    static std::unordered_set<std::string> memo;
    return memo;
}

} // namespace

void drawHair(const selva::gameplay::Actor& actor, const glm::mat4& view_proj,
              const std::vector<glm::mat4>& bone_palette, float body_foot_offset_y)
{
    if (actor.appearance.hair_style_id.empty())
        return;

    const selva::hair::HairStyle* style =
        selva::hair::findHairStyle(actor.appearance.hair_style_id);
    if (style == nullptr || style->mesh_path_male.empty())
        return;
    if (loadFailureMemo().count(style->mesh_path_male) != 0)
        return;

    // Hair rigs against the mixamo 52-bone convention -- same as every
    // humanoid skeleton bundle -- so the actor's own skeleton_id is
    // the correct target for inverse-bind resolution.
    const std::string sk_id = actor.skeleton_id.empty()
                                  ? std::string(selva::anim::kPlayerSkeletonKey)
                                  : actor.skeleton_id;
    auto& hair_mesh = selva::anim::meshByArchetypePath(style->mesh_path_male, sk_id);
    if (!hair_mesh.isLoaded())
    {
        std::fprintf(stderr, "[hair-render] mesh '%s' failed to load; skipping\n",
                     style->mesh_path_male.c_str());
        std::fflush(stderr);
        loadFailureMemo().insert(style->mesh_path_male);
        return;
    }

    const selva::gameplay::Appearance live_app =
        selva::gameplay::resolveLiveAppearance(actor.appearance);
    // Use the body's foot_offset_y, not the hair mesh's -- hair verts
    // are baked at scalp height (~1.7m) in T-pose; the skinning math
    // re-projects them onto the animated skull, so the model matrix
    // needs to place the hair in the SAME world frame as the body.
    const glm::mat4 hair_model = selva::combat::buildActorModelMatrix(
        actor.pos, actor.yaw, body_foot_offset_y, live_app.body_scale);

    // Colorize mode: desaturate the sampled diffuse to luminance,
    // then multiply by hair_tint -- so the tint IS the hair colour
    // (not a subtle re-shade of the baked brown). Auto-resets to
    // Multiply after the draw so subsequent draws in the pass keep
    // the default behaviour.
    selva::anim::setSkeletalTintMode(selva::anim::TintMode::Colorize);
    selva::anim::drawSkeletalMesh(hair_mesh, hair_model, view_proj, bone_palette,
                                  live_app.hair_tint, /*alpha=*/1.0f);
}

void preloadAllHairMeshes()
{
    // Intentional no-op. Hair meshes lazy-load on first drawHair()
    // call via meshByArchetypePath's cache. Rationale: the player
    // touches at most a handful of styles in the character creator,
    // and every style-swap happens in a menu frame -- a ~50ms cold
    // mesh load is invisible under the ImGui pause. Pre-warming the
    // whole 56-style library stalled boot by ~8s to eagerly pay
    // upload cost the game never uses.
    //
    // When enemy archetypes start declaring hair_style_id (Guide,
    // future humanoid NPCs), add a targeted pre-warm here that walks
    // archetypes().all() and force-loads their referenced styles --
    // an actor spawning must not stall the gameplay frame. Player-
    // picked styles remain lazy.
}

} // namespace selva::render
