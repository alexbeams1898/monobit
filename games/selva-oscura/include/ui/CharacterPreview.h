#pragma once

// Souls-style character preview viewport for the F1 character designer.
// Renders the player's skinned mesh (and only the mesh -- no world,
// no enemies, no terrain) into an offscreen framebuffer, which the F1
// panel displays via ImGui::Image.
//
// Lifecycle:
//   * initCharacterPreview(): one-time GL setup (FBO + color tex +
//     depth tex). Idempotent.
//   * renderCharacterPreview(): per-frame render into the FBO. Caller
//     invokes only on frames the character designer tab is open.
//   * characterPreviewTexture(): returns the GL color texture id the
//     panel passes to ImGui::Image.
//   * shutdownCharacterPreview(): frees the FBO + textures.
//
// Camera: a fixed framing (~2m in front of player, chest height) for
// M1. M2 will add orbit + zoom via mouse drag inside the panel.
//
// Lighting: a neutral 3-light feel achieved through the existing
// skeletal shader's lambert. Shadow sampling is bypassed by setting
// the shadow map's projection to push samples off the map (border
// color = fully lit, matching the existing shadow pass setup).
//
// Why an FBO instead of just drawing the player in-world? Souls'
// creator viewport shows the character against a flat background and
// neutral lighting -- the world isn't behind them. Offscreen
// rendering also lets the panel show the character at a calibrated
// angle independent of the gameplay camera state.

namespace selva::ui
{

bool initCharacterPreview();
void shutdownCharacterPreview();

// Render one frame of the preview into the FBO. No-op before init or
// after shutdown. Reads sPlayer's appearance + sampler.bone_palette;
// applies the appearance deformation pass internally so live slider
// edits show immediately. Safe to call every frame the panel is open.
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

} // namespace selva::ui
