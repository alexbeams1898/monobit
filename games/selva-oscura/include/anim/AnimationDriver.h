#pragma once

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// AnimationDriver — abstraction for character animation.
//
// Doctrine: see engines/engine/docs/3D-EXTENSION.md §5b. Procedural drivers
// implement this today; a future skeletal driver implements the same surface
// against ozz-animation. Combat / movement / dodge code targets this
// abstraction and never reaches into either implementation directly.
//
// First home is selva-oscura/. When a second 3D game on this engine wants
// the same surface, promote to engines/engine/include/anim/.
//
// Lifecycle of a state: when gameplay enters a state (e.g. DodgeRoll),
// game code feeds (state, phase=0..1, params) into the driver each frame
// and applies the returned offsets on top of the entity's base transform.
// State transitions, durations, hitbox windows, and cancellation rules
// are owned by gameplay code — the driver only computes per-frame offsets.
// ---------------------------------------------------------------------------

namespace selva::anim
{

// Closed enum of animation states the game can request. Adding a state
// requires extending the procedural driver's switch — the compiler enforces
// exhaustive coverage via -Wswitch.
enum class AnimState
{
    // Default neutral pose; driver returns identity offsets.
    None,

    // Standing locomotion (walking / sprinting). Today the driver outputs
    // identity (movement is integrated by gameplay code directly, not
    // animation-driven). Listed here so future bobbing / arm-swing /
    // sprint-lean animations have a home without an API change.
    Locomotion,

    // Dodge — directional roll with hop arc, ease-out forward translation,
    // and visual tumble. Souls-style commit: gameplay code locks out
    // input for the duration; this driver supplies the visual+positional
    // shape. params.dodge_dir is the unit ground-plane direction; the
    // driver translates the entity along that direction over the phase.
    DodgeRoll,

    // Dodge backstep — short, faster, no tumble, no hop. Direction is
    // params.dodge_dir (typically -facing). Same translation shape as
    // DodgeRoll but no Y arc and no rotation.
    DodgeBackstep,

    // Recovery window after a dodge — animation sits at neutral but the
    // state is named so future "panting / stagger-out" animations have a
    // home. Driver currently returns identity.
    DodgeRecover,
};

// Per-state parameters. State that's specific to a particular AnimState
// goes here. Today only the dodge needs anything; combat states will add
// fields (attack tier, weapon class, hit-react impulse, etc.) as they land.
struct AnimDriverParams
{
    // Ground-plane unit vector. Used by DodgeRoll / DodgeBackstep to
    // compute the X/Z translation contribution. Ignored for other states.
    glm::vec3 dodge_dir = glm::vec3(0.0f, 0.0f, -1.0f);
};

// Inputs to the driver each frame.
struct AnimDriverInput
{
    AnimState state = AnimState::None;

    // Normalized phase progress in [0, 1] — 0 = state just entered, 1 =
    // state about to exit. The driver does not own timing; the gameplay
    // state machine computes phase from its own timer / duration.
    float phase = 0.0f;

    AnimDriverParams params;
};

// Per-frame outputs. Composed onto the entity's base transform by the
// caller — driver does not write to entity state directly. This keeps
// the driver pure and testable.
struct AnimDriverOutput
{
    // Translation offset in world space, added to the entity position.
    glm::vec3 translation = glm::vec3(0.0f);

    // Rotation offset around three axes (radians). Applied as additional
    // rotations after the entity's base yaw — used for tumble during
    // dodge, lean during sprint, etc.
    float pitch_offset = 0.0f; // around local +X
    float yaw_offset = 0.0f;   // around world +Y
    float roll_offset = 0.0f;  // around local +Z

    // Future: bone palette pointer for the skeletal driver. Procedural
    // drivers leave this null. Not modeled today to avoid premature API.
};

// Evaluate the driver for the given input. Free function — no state, no
// allocation, easy to test in isolation. The procedural implementation
// lives in src/anim/ProceduralDriver.cpp.
AnimDriverOutput evaluate(const AnimDriverInput& input);

} // namespace selva::anim
