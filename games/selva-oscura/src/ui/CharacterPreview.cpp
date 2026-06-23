#include "ui/CharacterPreview.h"

#include "AppStateGlobal.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalAssets.h"
#include "anim/SkeletalMesh.h"
#include "anim/SkeletalRenderer.h"
#include "anim/SkeletonJointMap.h"
#include "combat/ActorVolumes.h"
#include "gameplay/Actor.h"
#include "gameplay/Appearance.h"
#include "gameplay/AppearanceDeformation.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <glad/glad.h>

namespace selva::ui
{

namespace
{

// Preview viewport dimensions. Souls-style portrait orientation
// (taller than wide) -- the character is a vertical figure, the
// image should be too. 400x600 fits comfortably in a 800-wide F1
// panel (sliders on the left).
constexpr int kPreviewWidth = 400;
constexpr int kPreviewHeight = 600;

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
constexpr float kBgColor[4] = {0.08f, 0.08f, 0.10f, 1.0f};

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

// Clamps to keep the camera sensible.
constexpr float kPitchMin = -85.0f; // never quite top-down
constexpr float kPitchMax = 85.0f;
constexpr float kZoomMin = 0.25f; // close-up: face fills frame
constexpr float kZoomMax = 4.0f;  // far out: figure small in frame

} // namespace

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
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, kMsaaSamples, GL_RGBA8, kPreviewWidth,
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kPreviewWidth, kPreviewHeight, 0, GL_RGBA,
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

void renderCharacterPreview()
{
    if (!sInitialized)
        return;

    selva::gameplay::Actor& player = selva::gameplay::player();
    if (player.sampler.bone_palette.empty())
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
    // Mesh authored-forward is +Z (Mixamo convention); the +pi flip
    // in buildActorModelMatrix rotates that to world-forward = -Z
    // (the codebase's actor-forward convention). The FRONT of the
    // figure is therefore the -Z side -- camera at -Z looks at the
    // face. See PoseSampler.cpp jointWorldMatrixWithActor for the
    // canonical comment on this flip.
    //
    // Distance derivation: given a desired fraction F of the viewport
    // the figure should occupy, vertical FOV theta, and figure height
    // H -- the half-figure subtends F/2 of the half-FOV, so
    //   tan(theta/2) * dist = H/2 / F   ->   dist = H / (2 * F * tan(theta/2))
    const float foot_y = selva::gameplay::actorFootOffsetY(player);
    const float body_scale = player.appearance.body_scale;
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
    // body's mid-height fraction.
    const float foot_world_y = -foot_y * body_scale;
    const float look_y = foot_world_y + figure_height * kLookAtFraction;
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

    // Apply the appearance deformation into a preview-local scratch
    // buffer (not the player's deformed_bone_palette, which the
    // gameplay draw reuses each frame -- the preview's render order
    // means we can't share). Reads sPlayer.appearance.
    const std::string sk_id =
        player.skeleton_id.empty() ? std::string("player") : player.skeleton_id;
    static std::vector<glm::mat4> sPreviewPalette;
    selva::gameplay::applyAppearanceDeformation(player.sampler, selva::anim::jointMapByKey(sk_id),
                                                player.appearance, sPreviewPalette);

    selva::anim::beginSkeletalPass();
    selva::anim::drawSkeletalMesh(selva::anim::playerMesh(), model_mat, view_proj, sPreviewPalette,
                                  player.appearance.color);
    selva::anim::endSkeletalPass();

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
}

} // namespace selva::ui
