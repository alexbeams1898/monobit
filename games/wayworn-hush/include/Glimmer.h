#pragma once

#include "Formulas.h"
#include "Growth.h"
#include "Psyche.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

#include <entt/entt.hpp>

class EntityManager;

// The highlight that marks an OBSERVABLE spot: a lit rim around the thing itself,
// beating gently so it reads as selectable. One rule -- a spot is lit while it is
// the InteractionSystem's resolved target (in reach AND faced, or hovered), and
// its brightness = formulas::glowBrightness(Perception): the stat renders the
// world through the character's eyes, so low Perception marks it faintly and a
// grown one marks it plainly. Nothing lingers: turn away and it fades.
//
// The rim is drawn on the ART the encounter covers -- an Encounter box carries no
// sprite of its own, so at spawn it adopts the prop whose footprint it sits on
// (see spawn). A box over bare ground has no silhouette to light and simply shows
// nothing. Items use the same rim through world_items (their own steady one).
// See docs/design/PSYCHE.md.

// An encounter highlight's smoothed display level. `level` lerps toward the
// target each frame (lit when active+present, else 0); the drawn rim alpha is
// level + breathing, so the pulse rides a clean fade and never flashes on.
// `art` is the entity actually wearing the rim (the adopted prop, or the
// encounter entity itself when it has art of its own).
struct Glimmer
{
    float level = 0.0f;
    entt::entity art = entt::null;
    // True when `art` is a BODY (a person's encounter): the box follows them, so
    // walking up to someone who moved still finds them. A prop never moves, so its
    // box stays where the map put it.
    bool follows_body = false;
};

namespace glimmer
{
// Highlight feel (config over constants). All tunable; edit config/glimmer.json +
// relaunch (dev reads assets from source).
struct Config
{
    float fade_speed = 6.0f; // alpha lerp rate toward the target (per second)
    // WHO sets the brightness. true = formulas::glowBrightness(Perception) (config/
    // formulas.json), the world seen through the character's eyes -- a faint mark
    // early, a plain one once grown. false = every spot lights at `brightness`, a
    // flat rim while the feel is being tuned (the stat can be handed the dial back
    // by flipping this, with no other change).
    bool perception_scales = false;
    float brightness = 1.0f; // the flat level used when the stat isn't driving it
    float pulse_amp = 0.35f; // beat depth (fraction of the lit level)
    float pulse_hz = 1.1f;   // beat rate
    float rim_width = 1.5f;  // rim thickness, in source texels
    float warm_r = 1.0f;     // "notice me" warm rim color
    float warm_g = 0.94f;
    float warm_b = 0.78f;
};

// Load the highlight feel from config/glimmer.json (silent no-op -> defaults if missing).
void load(Config& cfg, const std::string& path);

// Spawn one highlight entity per Observe-mode encounter, each adopting the art
// the rim will outline. Enter-mode encounters fire ambiently and carry no
// highlight.
//
// `bodies` maps npc id -> that character's entity for the people already standing
// in this level. A person's encounter lights THEM, by name -- a body is not scenery
// to be found by looking under a box (they move, and they stand in front of
// things). Everything else adopts the prop art under its box.
//
// `gone` is the set of placement ids this pilgrim has removed for good
// (savegame::World::gone): a spot consumed on a previous visit is skipped, so it doesn't
// come back when the world is rebuilt from the authored map.
void spawn(EntityManager& em, const psyche::State& obs, const Config& cfg,
           const std::unordered_map<std::string, entt::entity>& bodies = {},
           const std::unordered_set<std::string>& gone = {});

// Set each interactable's `present` from live encounter visibility -- call BEFORE the
// interaction resolve, so a hidden encounter is no target (neither verb) and a revealed one
// becomes interactable the same frame. The one gate that keeps observe + act in agreement.
void refreshPresence(EntityManager& em, const psyche::State& obs,
                     const growth::GrowthState& growth);

// Per-frame: the active target's art wears a beating rim at the Perception
// brightness; everything else fades out and loses its rim. dt = frame seconds.
void update(EntityManager& em, const growth::GrowthState& growth, const formulas::Config& formulas,
            const Config& cfg, float dt);
} // namespace glimmer
