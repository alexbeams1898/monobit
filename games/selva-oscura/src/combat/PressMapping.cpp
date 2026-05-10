#include "combat/PressMapping.h"

#include <cstring>

#include "combat/ChainObserver.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"

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
        if (exp == want)
            return &tech.attacks[0];
        if (exp.empty() || exp == "any")
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

    // Sprint: running attack only on LMB. Other buttons during sprint
    // fall through to their normal mappings.
    if (mods.sprinting && std::strcmp(button, "LMB") == 0 &&
        !aset.running.empty() && !aset.running[0].attacks.empty())
        return aset.running[0].attacks[0].clip.c_str();

    // Chain advancement: if the observer has a technique matched at
    // step N, AND this press lands inside the cancel window, AND the
    // technique's slot N (the next step after the matched tail) wants
    // this button, fire that slot's clip. This is what makes
    // LMB-RMB-LMB play jab-hook-combo (finisher) instead of
    // jab-hook-jab (cold).
    const ChainState& cs = chainState();
    const bool inside_window = cancel_window_close_at > cancel_window_open_at &&
                               wall_clock_seconds >= cancel_window_open_at &&
                               wall_clock_seconds <= cancel_window_close_at;
    if (inside_window && cs.technique_id != nullptr && cs.step > 0)
    {
        for (const auto& tech : aset.light)
        {
            if (std::strcmp(tech.id.c_str(), cs.technique_id) != 0)
                continue;
            const int next_slot = cs.step;
            if (next_slot >= 0 && next_slot < static_cast<int>(tech.attacks.size()))
            {
                const auto& exp = tech.attacks[next_slot].expected_button;
                if (exp.empty() || exp == "any" || exp == button)
                {
                    if (out_is_chain_advance != nullptr)
                        *out_is_chain_advance = true;
                    return tech.attacks[next_slot].clip.c_str();
                }
            }
            break;
        }
    }

    // Heavy: not yet wired (heavy slot is empty for unarmed, sword runs
    // its own light list as heavy fallback). For now, shift falls
    // through to light.
    if (mods.shift && !aset.heavy.empty() && !aset.heavy[0].attacks.empty())
    {
        // Heavy uses the first technique's slot-0 regardless of
        // button — heavy is its own attack, not a chain step yet.
        return aset.heavy[0].attacks[0].clip.c_str();
    }

    // Light: pick by button. RMB looks for a technique whose slot 0
    // wants RMB (e.g. unarmed's hook isn't slot-0-RMB anywhere, so
    // for unarmed RMB we hit the fallback below).
    const WeaponAttack* a = slotZeroAttackForButton(aset, button);

    // Special-case: unarmed RMB → hook. Hook isn't slot-0 of any
    // technique, but we want RMB cold to fire it. Look for "hook"
    // in any technique's later slots; first match wins.
    if (std::strcmp(button, "RMB") == 0 && w->cls->id == "unarmed")
    {
        for (const auto& tech : aset.light)
        {
            for (const auto& atk : tech.attacks)
            {
                if (atk.expected_button == "RMB")
                    return atk.clip.c_str();
            }
        }
    }

    return (a != nullptr) ? a->clip.c_str() : nullptr;
}

} // namespace selva::combat
