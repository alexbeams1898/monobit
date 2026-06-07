#include "gameplay/Sangue.h"

#include <algorithm>

namespace selva::sangue
{

namespace
{

// Saturating add at SANGUE_LIFETIME_CAP. Returns the amount actually
// added (limited by remaining room).
std::uint32_t saturatingAdd(std::uint32_t& field, std::uint32_t amount)
{
    if (field >= SANGUE_LIFETIME_CAP)
        return 0u;
    const std::uint32_t room = SANGUE_LIFETIME_CAP - field;
    const std::uint32_t add = std::min(amount, room);
    field += add;
    return add;
}

} // namespace

std::uint32_t grantOnKill(PlayerProfile& p, std::uint32_t amount)
{
    if (amount == 0u)
        return 0u;
    // Lifetime caps independently from vessel -- a player whose vessel
    // is full can still tick the lifetime counter, and vice versa. The
    // vessel grant is what the HUD reads; the lifetime is the ledger.
    saturatingAdd(p.sangue_lifetime, amount);
    return saturatingAdd(p.sangue_vessel, amount);
}

void reclaimVessel(PlayerProfile& p)
{
    p.sangue_vessel = 0u;
}

} // namespace selva::sangue
