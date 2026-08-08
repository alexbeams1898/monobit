#pragma once

namespace entt
{
enum class entity : unsigned int;
}
class EntityManager;

// What killing pays.
//
// A dead thing drops its worth on the ground rather than paying it straight into the sheet:
// the drop is the moment the kill becomes VISIBLE as income, and walking into it is a small
// completed transaction -- the loop's heartbeat. Pickups magnet to him inside a radius that
// Inspection widens, which is that stat's first real job: the thorough man collects what the
// hasty one walks past.
//
// The currency is credited THE MOMENT the kill lands -- it is never an object in the world.
// What lies on the ground is a different channel entirely: item drops, collected by hand when
// items exist. The pocketed sum is spent only at a rest spot, at a price that climbs with the
// level already bought; the curve reads the sheet itself -- level is points spent, so no second
// ledger exists to drift from it.
namespace reward
{

// Pay for a kill, straight into the pocket. Called where things finish dying.
void credit(EntityManager& em, int worth);

// Rest-spot upkeep (the slow mend while standing there).
void update(EntityManager& em, float dt);

int banked(const EntityManager& em);
// What the NEXT point costs, from the sheet's level.
int costOfNext(const EntityManager& em);
// Is he standing somewhere points are sold?
bool atRest(const EntityManager& em);

// Buy one point for a stat (0=chemical..4=inspection): only at a rest spot, only if the pocket
// covers the price. Re-derives the body.
bool spend(EntityManager& em, int statIndex);

} // namespace reward
