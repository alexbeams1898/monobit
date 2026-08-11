#pragma once

class EntityManager;

// Where the trade is practised. One predicate, read by the shell (may the
// trigger fire?) and the HUD (does the kit draw live or stowed?), so the two
// can never disagree.
namespace zone
{

// A dug floor -- generated space, where waves run and the wave readout shows.
bool dug();

// Anywhere vermin can reach him: a dug floor, or an authored room whose dig
// site leaks. The weapon fires here and nowhere else.
bool combat(const EntityManager& em);

} // namespace zone
