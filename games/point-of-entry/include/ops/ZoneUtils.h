#pragma once

class EntityManager;

// Where the trade is practised. One predicate, read by the shell (may the
// trigger fire?) and the HUD (does the kit draw live or stowed?), so the two
// can never disagree.
namespace zone
{

// Anywhere vermin can reach him: a dug floor (generated space), or an
// authored room whose dig site is open and leaking. The weapon fires here
// and nowhere else.
bool combat(const EntityManager& em);

} // namespace zone
