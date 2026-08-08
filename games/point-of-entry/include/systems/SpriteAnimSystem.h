#pragma once

#include "SpriteDefLoader.h"

#include <string>

namespace entt
{
enum class entity : unsigned int;
}
class EntityManager;

// Playing a named animation on an entity.
//
// The engine's AnimationSystem addresses a sprite sheet as a GRID -- a row per direction, a
// column per frame. This game's sheets are a single horizontal strip with named tag ranges,
// because every character is one drawing mirrored (see Player.cpp) and so has no direction
// rows to spend. The two meet through the engine's frame mask: row 0 always, and the mask
// holds the tag's frame columns. Nothing here re-implements playback -- timing, looping and
// one-shot hold stay with the engine.
namespace sprite_anim
{

// Give the entity the animation machinery described by `def`. Call once, after the sprite's
// sheet is set. Does nothing if the art carries no tags -- a still sprite needs no animator.
void attach(EntityManager& em, entt::entity entity, const sprite_def::Def& def);

// Play the tag of this name, e.g. "walk". Restarts only if it is not already the one playing,
// so calling it every tick is the intended usage and will not stall the cycle on frame 0.
// An unknown name leaves the current animation alone and logs once -- a typo in a tag should
// be findable, but must not blank the character mid-play.
void play(EntityManager& em, entt::entity entity, const std::string& name);

// Play `name`, or `fallback` frozen on its first frame if the art carries no such tag. For
// states a character may not have been drawn for yet: a half-finished sprite should stand
// there looking calm rather than warn every tick about the tag nobody has drawn.
void playIfPresent(EntityManager& em, entt::entity entity, const std::string& name,
                   const std::string& fallback);

// The tag currently playing, empty if none.
const std::string& current(const EntityManager& em, entt::entity entity);

} // namespace sprite_anim
