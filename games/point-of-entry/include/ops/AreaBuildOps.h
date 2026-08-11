#pragma once

// The game's area-object vocabulary: builders for everything an authored
// place can contain that is not travel's own (doors, starts). Registered by
// both the game and the editor tool, so a map means the same thing in both.
namespace area_build
{

void registerAll();

} // namespace area_build
