#pragma once

class EntityManager;

// The walk, done in code rather than drawn.
//
// A character in this game is ONE drawing. It does not have a walk cycle; it has a body that
// hops and rocks while it travels, and that motion is two sine waves rather than a row of frames.
// Which is the whole reason to do it this way: every pest ever added gets the walk for free,
// a field guide of dozens costs one drawing each, and the height and speed of it are numbers rather
// than art.
//
// THE MOTION IS A FIGURE-EIGHT, not a bob. Hopping straight up and down reads as a sprite being
// nudged; the goofy cartoon walk swings the body sideways as it rises, one full left-right sway
// per two hops, so the body traces a loop. That halved frequency is the entire trick.
//
// Driven by DISTANCE TRAVELLED rather than by time: the rock speeds up when the character moves
// faster and stops dead the instant it stops, with no state to keep in step. A time-driven bob
// keeps dancing while standing still.
namespace walk_bob
{

// Offset every walking sprite. Runs after movement, before anything reads a sprite for drawing.
void update(EntityManager& em, float dt);

} // namespace walk_bob
