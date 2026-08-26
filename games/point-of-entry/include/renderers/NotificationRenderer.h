#pragma once

#include <string>

class Engine;

// The acquisition feed: what just entered the satchel, stacked bottom-left above the tool.
//
// Same-key pushes AGGREGATE while the line is alive -- three flakes in a second read as one
// line bumping to +3 with a flash, not three lines racing each other. That bump is what makes
// a multi-pickup feel like a haul instead of spam; the aggregation window is simply the line's
// remaining life, so a steady trickle keeps its line warm and a pause lets it fade.
//
// Interface events only. Damage numbers stay world-anchored (FloaterRenderer): a hit belongs
// where it landed, a gain belongs to the ledger's corner.
namespace notify
{

// Add `count` of `label` under an aggregation key (usually the item path).
void item(const std::string& key, const std::string& label, int count);

// A plain announcement through the same feed -- a line that is an event, not a tally, so it
// carries no count. Same keying, same life, same stack.
void line(const std::string& key, const std::string& label);

// Tick and draw. Called from the HUD's pass with its frame time.
void render(Engine& engine, float dt);

// Drop everything -- a new job should not inherit the last one's feed.
void clear();

} // namespace notify
