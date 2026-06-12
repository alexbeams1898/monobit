#pragma once

#include <glm/vec2.hpp>

namespace selva::ui
{

// Screen-space anchor of the vessel sangue counter inside the HUD.
// Sangue pulses target this point; the HUD readout draws at it.
// Single source of truth so the pulse system and the HUD don't drift.
// Returns (0, 0) if the main viewport hasn't been computed yet
// (boot frame).
glm::vec2 sangueHudAnchor();

// Player HP + stamina HUD. Two stacked horizontal bars in the
// top-left of the viewport (soulslike convention). HP red,
// stamina green. Dark background, no decoration, drawn each
// frame from selvaRenderImGui.
//
// Reads selva::gameplay::player().hp / .stamina directly. No
// state of its own; just visualization.
void renderActorHud();

// Compass strip at top-center. Horizontal Skyrim/Minecraft-style band
// showing cardinal + intercardinal directions; the letter under the
// center mark is the direction the camera is currently facing. Reads
// at a glance, no screen real estate cost beyond a thin strip.
void renderCompass();

// Debug toggle: when true, the world-overlay pass draws projected
// outlines of every hitbox + hurtbox in the pool. Toggled from the
// F1 panel; off by default.
bool showHitVolumes();
void setShowHitVolumes(bool enabled);

// World-collider debug overlay: wireframe outlines for every cylinder
// + box collider in the active scene. Gated by
// selva::debug::flags().show_colliders (F1 panel toggle).
void renderColliderDebug();

// Jolt physics body overlay: wireframe AABBs for every body in the
// Jolt world (static trimeshes, static boxes, character capsules)
// colored by surface tag. Gated by selva::debug::flags().show_physics_bodies.
// Source of truth for "is this mesh actually registered in physics?"
void renderPhysicsBodyDebug();

// Region system overlays:
//   * Always-on: scene-name chip in the corner + fade-to-black overlay
//     during transitions
//   * Debug-gated: trigger volume wireframes (color + label) inside
//     the active scene
//   * Debug-gated: scene state panel (current + transition state +
//     body counts + force-transition buttons)
void renderSceneOverlays();

} // namespace selva::ui
