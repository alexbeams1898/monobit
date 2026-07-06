#include "ui/CharacterPreview.h"

#include "AppStateGlobal.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalAssets.h"
#include "anim/SkeletalMesh.h"
#include "anim/SkeletalRenderer.h"
#include "anim/SkeletonJointMap.h"
#include "combat/ActorVolumes.h"
#include "gameplay/Actor.h"
#include "gameplay/Appearance.h"
#include "gameplay/AppearanceDeformation.h"
#include "render/HairRenderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <glad/glad.h>

namespace selva::ui
{

namespace
{

// Preview viewport dimensions. Souls-style portrait orientation
// (taller than wide) -- the character is a vertical figure, the
// image should be too. Sized to fill the character-creator's
// preview pane comfortably (~700x1050 on a 1440p display, scaled
// down by the pane sizer at lower resolutions). The F1 tuning
// panel uses the same texture and crops to its smaller area.
constexpr int kPreviewWidth = 700;
constexpr int kPreviewHeight = 1050;

// Canonical humanoid bind-pose height in meters. Used to scale
// the preview camera framing so the whole figure (feet to top of
// head) fits in the viewport regardless of body_scale -- at 0.6x
// the figure is ~1.1m and the camera tucks in, at 4.0x the figure
// is ~7.2m and the camera pulls way back.
constexpr float kCanonicalBodyHeightMeters = 1.8f;
// FOV used to derive camera distance from body height. Wider FOV =
// closer camera. 35deg gives a flattering Souls-style portrait
// (mild telephoto compression).
constexpr float kPreviewFovDegrees = 35.0f;
// Fraction of vertical viewport the figure should occupy. 0.80 =
// figure fills 80% of frame, 20% of headroom split top/bottom.
constexpr float kFigureFillFraction = 0.80f;
// Look-at target: fraction of the body height (from feet) to aim
// the camera at. 0.55 = roughly chest height. Half-body = 0.50.
// Slightly above half so the head doesn't sit at top edge.
constexpr float kLookAtFraction = 0.55f;

// Background color. Souls's creator uses a very dark warm gray.
// Linear-space; originally sRGB-authored (0.08, 0.08, 0.10) dark warm
// gray. Linearized for the sRGB-framebuffer round-trip.
constexpr float kBgColor[4] = {0.0072f, 0.0072f, 0.0100f, 1.0f};

// MSAA sample count for clean silhouette edges. 4x is the safe
// universally-supported floor; 8x looks better but isn't guaranteed.
// Souls character creators are visibly AA'd; without this the edges
// of the figure read as jagged stairsteps at 400x600.
constexpr int kMsaaSamples = 4;

// Multisample FBO + attachments: where we actually render.
GLuint sMsFbo = 0;
GLuint sMsColorRbo = 0;
GLuint sMsDepthRbo = 0;
// Resolve FBO + color texture: what ImGui samples. After rendering
// to the multisample FBO we blit-resolve into this.
GLuint sResolveFbo = 0;
GLuint sResolveColorTex = 0;
bool sInitialized = false;

// Orbit-camera state. yaw=0 = looking at the figure's front (from
// camera's local -Z which corresponds to world-forward of the
// figure). pitch=0 = level. zoom=1.0 = default framing.
float sYawDeg = 0.0f;
float sPitchDeg = 0.0f;
float sZoom = 1.0f;
// Per-frame look-at override. NaN means "use kLookAtFraction default
// (chest height)". The character creator sets this to e.g. 0.92 to
// frame on the face when the Eyes category is selected. Manual
// orbit (drag/scroll) doesn't touch this -- callers control it
// explicitly via snapCharacterPreviewFraming.
float sLookAtFractionOverride = std::nanf("");

// Clamps to keep the camera sensible.
constexpr float kPitchMin = -85.0f; // never quite top-down
constexpr float kPitchMax = 85.0f;
constexpr float kZoomMin = 0.25f; // close-up: face fills frame
constexpr float kZoomMax = 4.0f;  // far out: figure small in frame

// Optional skeleton override -- when non-empty, the preview draws
// the bundle keyed by this id instead of the pushed appearance's
// body_type. Used by the F1 panel for in-engine validation of
// newly-baked humanoid rigs before swapping live gameplay.
std::string sSkeletonOverride;
std::string sActiveSamplerKey; // which override the sampler is currently built for
selva::anim::PoseSampler sOverrideSampler;
double sOverrideClipTime = 0.0;

// -------------------------------------------------------------------
// PreviewState: the preview module's OWNED render data. Populated by
// setCharacterPreviewAppearance() (which the creator + F1 panel call
// each frame with their own Appearance). renderCharacterPreview()
// reads from here. Zero references to sPlayer -- the preview is a
// self-contained subsystem.
//
// Sampler + palette + morph vector live here so the preview owns its
// own animation state without touching the gameplay player's tracks.
// Rebound only when the pushed body_type crosses skeleton bundles;
// stays warm across frames on the common path.
// -------------------------------------------------------------------
struct PreviewState
{
    // Copied in from setCharacterPreviewAppearance(). Default at
    // process start is the Appearance struct's own defaults (bald,
    // body_type 1, natural proportions) so first render works even
    // before any client pushes.
    selva::gameplay::Appearance appearance;

    // Which skeleton bundle the sampler + mesh currently target.
    // Rebound only when a push changes this (i.e. body_type flips).
    std::string skeleton_id;

    // The preview's own sampler. Advanced by the standard_idle clip
    // each render; NEVER shared with gameplay. A fresh preview
    // instance == a fresh sampler == no leaked pose state.
    selva::anim::PoseSampler sampler;

    // Reusable scratch buffers for the per-frame deformation +
    // morph-weight resolution. Kept as members so the underlying
    // allocations survive across frames -- clearing a std::vector
    // (rather than reallocating) is a no-op with warm capacity.
    std::vector<glm::mat4> deformed_palette;
    std::vector<float> morph_weights;

    // Idle clip time cursor for the preview's sampler.
    double clip_time_seconds = 0.0;

    // True once at least one push has landed. Fresh install skips
    // the "empty push" first-frame case where callers haven't wired
    // yet; then setCharacterPreviewAppearance is the only mutator.
    bool has_pushed_appearance = false;
};

PreviewState sPreview;

// Rebuild the sampler for a new skeleton bundle. Called from
// setCharacterPreviewAppearance when body_type changes -- expensive
// (per-joint palette allocation, morph binding), so guarded by an
// equality check on skeleton_id.
void bindPreviewSamplerToSkeleton(const std::string& skeleton_id)
{
    sPreview.skeleton_id = skeleton_id;
    sPreview.sampler = selva::anim::createPoseSampler(selva::anim::skeletonByKey(skeleton_id),
                                                      selva::anim::meshByKey(skeleton_id),
                                                      selva::anim::jointMapByKey(skeleton_id));
    sPreview.clip_time_seconds = 0.0;
}

} // namespace

void setCharacterPreviewAppearance(const selva::gameplay::Appearance& appearance)
{
    // Cheap fast-path when only slider values changed (no skeleton
    // rebind). Sampler rebind fires only on body_type flip -- checked
    // by comparing the resolved skeleton_id, so any BodyType->id
    // mapping change ripples through automatically.
    const std::string new_sk = selva::gameplay::bodyTypeSkeletonId(appearance.body_type);
    sPreview.appearance = appearance;
    sPreview.has_pushed_appearance = true;
    if (new_sk != sPreview.skeleton_id)
        bindPreviewSamplerToSkeleton(new_sk);
}

bool initCharacterPreview()
{
    if (sInitialized)
        return true;

    // Multisample FBO: render here (color + depth as multisample
    // renderbuffers, because multisample textures need a different
    // sampler in the shader and we don't want to fork the skeletal
    // shader for the preview).
    glGenFramebuffers(1, &sMsFbo);
    glGenRenderbuffers(1, &sMsColorRbo);
    glGenRenderbuffers(1, &sMsDepthRbo);

    glBindRenderbuffer(GL_RENDERBUFFER, sMsColorRbo);
    // sRGB color buffer: skeletal shader writes linear-light; GPU
    // encodes to sRGB on resolve into the matching-format
    // sResolveColorTex (also sRGB). ImGui then samples it as a
    // regular texture; with GL_FRAMEBUFFER_SRGB on at draw time
    // the read-back-as-sRGB-encoded-bytes path stays correct.
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, kMsaaSamples, GL_SRGB8_ALPHA8, kPreviewWidth,
                                     kPreviewHeight);
    glBindRenderbuffer(GL_RENDERBUFFER, sMsDepthRbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, kMsaaSamples, GL_DEPTH_COMPONENT24,
                                     kPreviewWidth, kPreviewHeight);

    glBindFramebuffer(GL_FRAMEBUFFER, sMsFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sMsColorRbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sMsDepthRbo);

    GLenum ms_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (ms_status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::fprintf(stderr, "[character-preview] multisample FBO incomplete: 0x%x\n", ms_status);
        std::fflush(stderr);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    // Resolve FBO: single-sample texture ImGui samples. After
    // rendering to sMsFbo we blit-resolve into here.
    glGenFramebuffers(1, &sResolveFbo);
    glGenTextures(1, &sResolveColorTex);
    glBindTexture(GL_TEXTURE_2D, sResolveColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, kPreviewWidth, kPreviewHeight, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, sResolveFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sResolveColorTex,
                           0);
    GLenum r_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (r_status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::fprintf(stderr, "[character-preview] resolve FBO incomplete: 0x%x\n", r_status);
        std::fflush(stderr);
        return false;
    }

    sInitialized = true;
    return true;
}

void shutdownCharacterPreview()
{
    if (sMsDepthRbo != 0)
    {
        glDeleteRenderbuffers(1, &sMsDepthRbo);
        sMsDepthRbo = 0;
    }
    if (sMsColorRbo != 0)
    {
        glDeleteRenderbuffers(1, &sMsColorRbo);
        sMsColorRbo = 0;
    }
    if (sMsFbo != 0)
    {
        glDeleteFramebuffers(1, &sMsFbo);
        sMsFbo = 0;
    }
    if (sResolveColorTex != 0)
    {
        glDeleteTextures(1, &sResolveColorTex);
        sResolveColorTex = 0;
    }
    if (sResolveFbo != 0)
    {
        glDeleteFramebuffers(1, &sResolveFbo);
        sResolveFbo = 0;
    }
    sInitialized = false;
}

void setCharacterPreviewSkeletonOverride(const char* skeleton_id)
{
    sSkeletonOverride = (skeleton_id != nullptr) ? skeleton_id : "";
}

const char* characterPreviewSkeletonOverride()
{
    return sSkeletonOverride.c_str();
}

namespace
{

// Lazy-init / re-init the override sampler when the override key
// changes. Returns false if the override bundle isn't loaded yet
// (caller falls back to player sampler).
bool ensureOverrideSampler()
{
    if (sSkeletonOverride.empty())
        return false;
    if (!selva::anim::hasSkeleton(sSkeletonOverride))
        return false;
    if (sActiveSamplerKey != sSkeletonOverride || sOverrideSampler.bone_palette.empty())
    {
        sOverrideSampler =
            selva::anim::createPoseSampler(selva::anim::skeletonByKey(sSkeletonOverride),
                                           selva::anim::meshByKey(sSkeletonOverride),
                                           selva::anim::jointMapByKey(sSkeletonOverride));
        sActiveSamplerKey = sSkeletonOverride;
        sOverrideClipTime = 0.0;
    }
    return !sOverrideSampler.bone_palette.empty();
}

} // namespace

void renderCharacterPreview()
{
    if (!sInitialized)
        return;

    // Lazy bind on first render if no client has pushed yet: use the
    // Appearance-default's implied skeleton. The FBO renders SOMETHING
    // (default humanoid, warm brown default hair tint) even in the
    // zero-configuration case so a caller sees the preview surface is
    // alive.
    if (!sPreview.has_pushed_appearance && sPreview.skeleton_id.empty())
        bindPreviewSamplerToSkeleton(
            selva::gameplay::bodyTypeSkeletonId(sPreview.appearance.body_type));

    const bool use_override = ensureOverrideSampler();
    if (!use_override && sPreview.sampler.bone_palette.empty())
        return;

    // Save the current viewport so we can restore it after the FBO pass.
    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

    glBindFramebuffer(GL_FRAMEBUFFER, sMsFbo);
    glViewport(0, 0, kPreviewWidth, kPreviewHeight);
    glClearColor(kBgColor[0], kBgColor[1], kBgColor[2], kBgColor[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    // Camera: framed to fit the whole figure regardless of body_scale.
    // The rig's mesh authored-forward is +Z; the +pi flip in
    // buildActorModelMatrix rotates that to world-forward = -Z (the
    // codebase's actor-forward convention). The FRONT of the figure
    // is therefore the -Z side -- camera at -Z looks at the face.
    // See PoseSampler.cpp jointWorldMatrixWithActor for the
    // canonical comment on this flip.
    //
    // Distance derivation: given a desired fraction F of the viewport
    // the figure should occupy, vertical FOV theta, and figure height
    // H -- the half-figure subtends F/2 of the half-FOV, so
    //   tan(theta/2) * dist = H/2 / F   ->   dist = H / (2 * F * tan(theta/2))
    // Resolve which skeleton bundle to size against: override wins,
    // else the pushed appearance's body_type. All the render-side
    // sizing math flows from this one decision so the camera fits
    // whatever mesh actually renders.
    const std::string sk_id_for_size = use_override ? sSkeletonOverride : sPreview.skeleton_id;
    const float foot_y = selva::anim::meshByKey(sk_id_for_size).foot_offset_y;
    const float body_scale = sPreview.appearance.body_scale;
    // Camera distance is derived from the CANONICAL body height (not
    // the current body_scale). This is the difference between "the
    // preview always fills the frame" (auto-reframe -> body_scale
    // edits invisible) and "body_scale edits visibly grow/shrink the
    // figure relative to a fixed framing." Souls character creators
    // do the latter: the canonical model fills the frame, larger
    // sliders push the head out the top until you zoom out.
    const float figure_height = kCanonicalBodyHeightMeters * body_scale;
    const float canonical_height = kCanonicalBodyHeightMeters;
    const float fov_rad = glm::radians(kPreviewFovDegrees);
    const float base_distance =
        canonical_height / (2.0f * kFigureFillFraction * std::tan(fov_rad * 0.5f));
    const float cam_distance = base_distance * sZoom;

    // The model matrix places foot at world Y = -foot_y * body_scale
    // (per buildActorModelMatrix below); the head sits at roughly
    // world Y = (figure_height - foot_y*body_scale). Look at the
    // body's mid-height fraction -- or the override fraction if the
    // creator has snapped to a per-category framing (Face=0.92 etc).
    const float foot_world_y = -foot_y * body_scale;
    const float look_at_fraction =
        std::isnan(sLookAtFractionOverride) ? kLookAtFraction : sLookAtFractionOverride;
    const float look_y = foot_world_y + figure_height * look_at_fraction;
    const glm::vec3 cam_target(0.0f, look_y, 0.0f);

    // Orbit math: yaw=0/pitch=0 places camera at look_at + (0, 0,
    // -cam_distance) -- in front of the figure (front-of-mesh is -Z
    // after the +pi flip per buildActorModelMatrix). Positive yaw
    // rotates the camera CCW around the figure as viewed from above
    // (so the camera moves to the figure's left); positive pitch
    // tilts the camera up (so the viewer looks slightly down at the
    // figure).
    const float yaw_rad = glm::radians(sYawDeg);
    const float pitch_rad = glm::radians(sPitchDeg);
    const float cy = std::cos(yaw_rad), sy = std::sin(yaw_rad);
    const float cp = std::cos(pitch_rad), sp = std::sin(pitch_rad);
    // Local-space camera offset (yaw=0 -> -Z): start at (0, 0, -d),
    // pitch lifts toward +Y, yaw rotates around the Y axis.
    const glm::vec3 cam_offset(sy * cp * cam_distance, sp * cam_distance, -cy * cp * cam_distance);
    const glm::vec3 cam_pos = cam_target + cam_offset;

    const glm::mat4 view = glm::lookAt(cam_pos, cam_target, glm::vec3(0.0f, 1.0f, 0.0f));
    const float aspect = static_cast<float>(kPreviewWidth) / static_cast<float>(kPreviewHeight);
    const glm::mat4 proj =
        glm::perspective(fov_rad, aspect, 0.05f, std::max(50.0f, cam_distance * 4.0f));
    const glm::mat4 view_proj = proj * view;

    // Model matrix: place the player at world origin (independent of
    // the gameplay player's actual world position). Yaw=0 so the
    // character faces the camera.
    const glm::mat4 model_mat = selva::combat::buildActorModelMatrix(
        glm::vec3(0.0f, 0.0f, 0.0f), /*yaw=*/0.0f, foot_y, body_scale);

    // Preview lighting. The skeletal shader uses half-Lambert with
    // the sun as a directional light. Sun direction is the TO-LIGHT
    // vector (pointing FROM the surface TOWARD the source). Camera
    // is at -Z (in front of the figure), so a -Z + +Y + slight -X
    // "to-light" direction puts the key on the figure's face / front
    // of chest / front of legs. Polish-deferred: forking a
    // portrait-specific shader with proper 3-point + ambient for the
    // preview pass.
    const glm::vec3 sun_dir = glm::normalize(glm::vec3(-0.3f, 0.5f, -1.0f));
    selva::anim::setSkeletalSun(sun_dir);
    // Shadow VP that pushes samples way off-map; combined with the
    // shadow texture's clamp-to-border (color = 1.0 = fully lit) this
    // is "shadow off" without modifying the shader.
    const glm::mat4 dummy_shadow_vp = glm::mat4(1.0f);
    selva::anim::setSkeletalShadow(dummy_shadow_vp, sun_dir,
                                   /*shadow_cam_pos=*/glm::vec3(0.0f, 0.0f, 0.0f),
                                   /*shadow_texture_unit=*/0);

    // Resolve which sampler + mesh to draw against. Override wins
    // (F1 in-engine rig-validation path); otherwise the preview's
    // own sampler bound to the pushed appearance's skeleton bundle.
    //
    // The preview NEVER reads player().sampler -- it owns
    // sPreview.sampler and ticks it here. The idle-clip tick keeps
    // strand-of-life movement (subtle breathing, blink) so the
    // portrait doesn't feel dead-frozen.
    const std::string sk_id = use_override ? sSkeletonOverride : sPreview.skeleton_id;
    selva::anim::PoseSampler& sampler = use_override ? sOverrideSampler : sPreview.sampler;
    const selva::anim::SkeletalMesh& mesh = selva::anim::meshByKey(sk_id);

    // Advance the sampler's idle clip a fixed 60Hz-equivalent step.
    // Real dt isn't available here (we're an ImGui-embedded render),
    // and a fixed step keeps the preview's motion smooth regardless
    // of the parent frame's variable timing.
    {
        const auto* idle = selva::anim::clipsByKey(sk_id).get("standard_idle");
        if (idle != nullptr && idle->isLoaded())
        {
            constexpr float kPreviewDtSeconds = 1.0f / 60.0f;
            sampler.update(*idle, kPreviewDtSeconds, 0.0f);
            if (use_override)
                sOverrideClipTime += kPreviewDtSeconds;
            else
                sPreview.clip_time_seconds += kPreviewDtSeconds;
        }
    }

    // Deform + morph resolution into PreviewState scratch buffers.
    // Reusing the persistent vectors instead of static function-locals
    // means allocator churn only happens on the first frame + on rare
    // capacity growth, not per-frame per-mesh.
    selva::gameplay::applyAppearanceDeformation(sampler, selva::anim::jointMapByKey(sk_id),
                                                sPreview.appearance, sPreview.deformed_palette);

    sPreview.morph_weights.clear();
    sPreview.morph_weights.reserve(mesh.morph_names.size());
    const auto& app_const = static_cast<const selva::gameplay::Appearance&>(sPreview.appearance);
    for (const auto& name : mesh.morph_names)
        sPreview.morph_weights.push_back(selva::gameplay::appearanceMorphWeight(app_const, name));

    // Portrait-fill lighting: brighter ambient than the world default
    // so legs/palms/downward-facing surfaces don't go pure black
    // against the FBO's empty background. The world's ambient values
    // (Tunables.lighting) assume bounce light + atmospheric
    // in-scatter from the surrounding scene; the preview FBO has
    // neither, so a softer floor reads as "studio portrait" rather
    // than "abyss." Sun-tint stays close to neutral white so the
    // skin tone reads accurately for character creation.
    constexpr glm::vec3 kPreviewSkyAmbient(0.35f, 0.35f, 0.38f);
    constexpr glm::vec3 kPreviewGroundAmbient(0.12f, 0.10f, 0.09f);
    constexpr glm::vec3 kPreviewSunTint(0.9f, 0.85f, 0.78f);
    selva::anim::setSkeletalAmbientOverride(kPreviewSkyAmbient, kPreviewGroundAmbient,
                                            kPreviewSunTint);

    selva::anim::beginSkeletalPass();
    // Eye tint: applies to the eye submesh only (material.role == Eyes).
    // Auto-resets inside drawSkeletalMesh so it doesn't leak.
    selva::anim::setSkeletalEyeTint(sPreview.appearance.eye_tint);
    selva::anim::drawSkeletalMesh(mesh, model_mat, view_proj, sPreview.deformed_palette,
                                  sPreview.appearance.color, /*alpha=*/1.0f,
                                  sPreview.morph_weights);
    // Hair: same skeletal pass, drawn against the SAME preview
    // palette. drawHair takes an Actor; synthesize a minimal one on
    // the stack from PreviewState -- pos = origin, yaw = 0 (matches
    // the model_mat above), appearance = the pushed appearance,
    // skeleton_id = the resolved bundle. No connection to any
    // gameplay actor.
    selva::gameplay::Actor preview_actor;
    preview_actor.appearance = sPreview.appearance;
    preview_actor.skeleton_id = sk_id;
    selva::render::drawHair(preview_actor, view_proj, sPreview.deformed_palette, foot_y);
    selva::anim::endSkeletalPass();

    // Return to world defaults so gameplay's next skeletal pass uses
    // the Tunables.lighting values, not the portrait fill.
    selva::anim::clearSkeletalAmbientOverride();

    // Resolve the multisample render into the single-sample texture
    // ImGui samples. Blit color-only (we don't need depth downstream).
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sMsFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sResolveFbo);
    glBlitFramebuffer(0, 0, kPreviewWidth, kPreviewHeight, 0, 0, kPreviewWidth, kPreviewHeight,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Restore prior FBO + viewport so the rest of the frame (which
    // draws the F1 panel ImGui surface on top of the gameplay scene)
    // continues to target the default back buffer.
    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
}

unsigned int characterPreviewTexture()
{
    return static_cast<unsigned int>(sResolveColorTex);
}

int characterPreviewWidth()
{
    return kPreviewWidth;
}

int characterPreviewHeight()
{
    return kPreviewHeight;
}

void addCharacterPreviewYaw(float delta_degrees)
{
    sYawDeg += delta_degrees;
    // Wrap to [-180, 180] so the accumulator doesn't drift to huge
    // values over thousands of frames. Rotation is purely visual --
    // the wrap is invisible to the user.
    while (sYawDeg > 180.0f)
        sYawDeg -= 360.0f;
    while (sYawDeg < -180.0f)
        sYawDeg += 360.0f;
}

void addCharacterPreviewPitch(float delta_degrees)
{
    sPitchDeg = std::clamp(sPitchDeg + delta_degrees, kPitchMin, kPitchMax);
}

void addCharacterPreviewZoom(float delta_factor)
{
    // Multiplicative zoom feels natural with scroll wheel: each
    // notch scales by a small factor rather than adding a fixed
    // amount (so far-out scrolling doesn't feel sluggish vs close-up
    // scrolling).
    sZoom = std::clamp(sZoom * (1.0f + delta_factor), kZoomMin, kZoomMax);
}

void resetCharacterPreviewCamera()
{
    sYawDeg = 0.0f;
    sPitchDeg = 0.0f;
    sZoom = 1.0f;
    sLookAtFractionOverride = std::nanf("");
}

void snapCharacterPreviewFraming(float yaw_degrees, float pitch_degrees, float zoom,
                                 float look_at_fraction)
{
    // NaN sentinel = "don't change this knob." Lets callers set
    // partial framings (e.g. just the look-at + zoom, keep manual
    // yaw/pitch) without an explicit struct.
    if (!std::isnan(yaw_degrees))
    {
        sYawDeg = yaw_degrees;
        while (sYawDeg > 180.0f)
            sYawDeg -= 360.0f;
        while (sYawDeg < -180.0f)
            sYawDeg += 360.0f;
    }
    if (!std::isnan(pitch_degrees))
        sPitchDeg = std::clamp(pitch_degrees, kPitchMin, kPitchMax);
    if (!std::isnan(zoom))
        sZoom = std::clamp(zoom, kZoomMin, kZoomMax);
    if (!std::isnan(look_at_fraction))
        sLookAtFractionOverride = look_at_fraction;
}

} // namespace selva::ui
