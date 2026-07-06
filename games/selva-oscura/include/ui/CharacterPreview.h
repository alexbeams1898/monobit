#pragma once

// Souls-style character preview viewport, used by BOTH the character-
// creator (Playing pre-entry) and the F1 tuning panel (Playing).
// Renders a single humanoid figure -- no world, no enemies, no
// terrain -- into an offscreen framebuffer that ImGui::Image displays.
//
// Data ownership (post-refactor 2026-07-01):
//   * The preview owns its ENTIRE render state: appearance, sampler,
//     bone palette, morph vector, skeleton binding, clip cursor. No
//     read of sPlayer, no read of any global gameplay actor. Callers
//     push what they want rendered via setCharacterPreviewAppearance().
//   * The creator pushes state().app (the in-flight slider edit) so
//     preview updates on every slider drag. The F1 panel pushes
//     player().appearance so the tuning knobs preview the actual live
//     player. Same rendering module, different data sources, zero
//     coupling between the two clients.
//   * The preview's sampler is REBOUND to the appearance's skeleton
//     bundle only when body_type changes -- cheap on the common case.
//
// Doctrine: creator preview state and gameplay player state share a
// STRUCT TYPE (Appearance), not a struct instance. Nothing in the
// preview can leak into gameplay; nothing in gameplay can leak into
// the preview.
//
// Camera / lighting / FBO details unchanged from the earlier
// implementation.

#include "gameplay/Appearance.h"

namespace selva::ui
{

bool initCharacterPreview();
void shutdownCharacterPreview();

// Push the Appearance the next render should draw. Cheap -- copies
// the struct in, rebinds the sampler only if body_type changed
// (which selects a different skeleton bundle). Callers push whatever
// they want previewed each frame:
//   * character creator: setCharacterPreviewAppearance(state().app)
//   * F1 tuning panel: setCharacterPreviewAppearance(player().appearance)
// If no client has pushed yet in a session, the preview renders a
// default Appearance (bald, default body-type-1 skin, natural
// proportions).
void setCharacterPreviewAppearance(const selva::gameplay::Appearance& appearance);

// Render one frame of the preview into the FBO. No-op before init or
// after shutdown, and idempotent within a frame (subsequent calls in
// the same frame short-circuit -- the creator + F1 panel both call
// this from their own render paths, and they must not double-render).
void renderCharacterPreview();

// GL texture id of the preview color attachment, suitable for passing
// to ImGui::Image. 0 if uninitialized.
unsigned int characterPreviewTexture();

// Pixel dimensions of the preview FBO. ImGui::Image must be called
// with these (or a proportional crop) to avoid stretching.
int characterPreviewWidth();
int characterPreviewHeight();

// Orbit-camera state for the preview. yaw rotates around the
// figure's vertical axis (0 = looking at the front), pitch tilts
// up/down (0 = level, +30 = slightly looking down), zoom multiplies
// the auto-derived camera distance (1.0 = default framing fits the
// whole body, 0.5 = closer / cropped, 2.0 = farther out).
//
// Panel code reads + writes these in response to mouse input inside
// the image rect (left-drag rotates, scroll zooms). Persisted across
// frames; reset to defaults on resetCharacterPreviewCamera().
void addCharacterPreviewYaw(float delta_degrees);
void addCharacterPreviewPitch(float delta_degrees);
void addCharacterPreviewZoom(float delta_factor);
void resetCharacterPreviewCamera();

// Absolute camera setters. Used by the character creator's
// category navigator to snap framing when the user picks a section
// (Eyes -> close-up on face; Proportions -> full body; etc.).
//
// snapCharacterPreviewFraming sets the four framing knobs in one
// call: yaw + pitch (camera angle), zoom multiplier on the auto-
// derived distance, and the look-at point as a fraction of the
// body height (0 = feet, 1 = top of head). Pass NaN for any value
// to leave it unchanged. After this call the manual orbit (drag /
// scroll) layers on top of the new framing -- the user can still
// adjust from there.
void snapCharacterPreviewFraming(float yaw_degrees, float pitch_degrees, float zoom,
                                 float look_at_fraction);

// Override the skeleton/mesh/clip bundle the preview draws. Empty
// string clears the override and falls back to sPlayer's bundle
// (humanoid_male by default). When set to a key like "humanoid_male",
// the preview lazily builds a per-key PoseSampler that ticks against
// the bundle's "standard_idle" clip -- a no-risk in-engine validation
// path for newly-baked humanoid rigs before swapping live gameplay.
void setCharacterPreviewSkeletonOverride(const char* skeleton_id);
const char* characterPreviewSkeletonOverride();

} // namespace selva::ui
