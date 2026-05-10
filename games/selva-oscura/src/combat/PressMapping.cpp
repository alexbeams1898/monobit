#include "combat/PressMapping.h"

#include "combat/ChainObserver.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"

#include <cstring>

namespace selva::combat
{

namespace
{

// Find a technique whose slot 0 expected_button matches `want`.
// Returns nullptr if none match. `want` is "LMB" or "RMB".
const WeaponAttack* slotZeroAttackForButton(const WeaponGripAnimSet& aset, const char* want)
{
    for (const auto& tech : aset.light)
    {
        if (tech.attacks.empty())
            continue;
        const auto& exp = tech.attacks[0].expected_button;
        if (exp == want || exp.empty() || exp == "any")
            return &tech.attacks[0];
    }
    if (!aset.light.empty() && !aset.light[0].attacks.empty())
        return &aset.light[0].attacks[0];
    return nullptr;
}

const WeaponGripAnimSet& gripSet(const Weapon& w, Grip grip)
{
    return (grip == Grip::TwoHanded) ? w.cls->two_handed : w.cls->one_handed;
}

// Sprint+LMB → running attack. Returns nullptr otherwise.
const char* sprintRunningClip(const WeaponGripAnimSet& aset, const char* button,
                              const PressModifiers& mods)
{
    if (!mods.sprinting || std::strcmp(button, "LMB") != 0)
        return nullptr;
    if (aset.running.empty() || aset.running[0].attacks.empty())
        return nullptr;
    return aset.running[0].attacks[0].clip.c_str();
}

// If the observer has a technique matched at step N AND this press
// lands inside the cancel window AND the technique's slot N wants
// this button, return that slot's clip. Sets out_is_chain_advance.
const char* chainAdvanceClip(const WeaponGripAnimSet& aset, const char* button,
                             float wall_clock_seconds, float cancel_window_open_at,
                             float cancel_window_close_at, bool* out_is_chain_advance)
{
    const bool inside_window = cancel_window_close_at > cancel_window_open_at &&
                               wall_clock_seconds >= cancel_window_open_at &&
                               wall_clock_seconds <= cancel_window_close_at;
    if (!inside_window)
        return nullptr;
    const ChainState& cs = chainState();
    if (cs.technique_id == nullptr || cs.step <= 0)
        return nullptr;
    for (const auto& tech : aset.light)
    {
        if (std::strcmp(tech.id.c_str(), cs.technique_id) != 0)
            continue;
        const int next_slot = cs.step;
        if (next_slot < 0 || next_slot >= static_cast<int>(tech.attacks.size()))
            return nullptr;
        const auto& exp = tech.attacks[next_slot].expected_button;
        if (exp.empty() || exp == "any" || exp == button)
        {
            if (out_is_chain_advance != nullptr)
                *out_is_chain_advance = true;
            return tech.attacks[next_slot].clip.c_str();
        }
        return nullptr;
    }
    return nullptr;
}

// Unarmed RMB → first technique attack with expected_button=="RMB"
// (typically hook). Hook isn't slot-0 of any technique, but we want
// RMB cold to fire it.
const char* unarmedRmbHookClip(const WeaponClass& cls, const WeaponGripAnimSet& aset,
                               const char* button)
{
    if (std::strcmp(button, "RMB") != 0 || cls.id != "unarmed")
        return nullptr;
    for (const auto& tech : aset.light)
    {
        for (const auto& atk : tech.attacks)
        {
            if (atk.expected_button == "RMB")
                return atk.clip.c_str();
        }
    }
    return nullptr;
}

} // namespace

const char* clipForButton(const PlayerEquipment& eq, HandSide hand, const char* button,
                          const PressModifiers& mods, float wall_clock_seconds,
                          float cancel_window_open_at, float cancel_window_close_at,
                          bool* out_is_chain_advance)
{
    if (out_is_chain_advance != nullptr)
        *out_is_chain_advance = false;
    const Weapon* w = (hand == HandSide::Right) ? eq.right : eq.left;
    if (w == nullptr || w->cls == nullptr)
        return nullptr;
    const auto& aset = gripSet(*w, eq.grip);

    if (const char* c = sprintRunningClip(aset, button, mods))
        return c;
    if (const char* c = chainAdvanceClip(aset, button, wall_clock_seconds, cancel_window_open_at,
                                         cancel_window_close_at, out_is_chain_advance))
        return c;
    if (mods.shift && !aset.heavy.empty() && !aset.heavy[0].attacks.empty())
        return aset.heavy[0].attacks[0].clip.c_str();
    if (const char* c = unarmedRmbHookClip(*w->cls, aset, button))
        return c;
    const WeaponAttack* a = slotZeroAttackForButton(aset, button);
    return (a != nullptr) ? a->clip.c_str() : nullptr;
}

} // namespace selva::combat
