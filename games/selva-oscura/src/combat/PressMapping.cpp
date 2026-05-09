#include "combat/PressMapping.h"

#include <cstring>

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
                          const PressModifiers& mods)
{
    const Weapon* w = (hand == HandSide::Right) ? eq.right : eq.left;
    if (w == nullptr || w->cls == nullptr)
        return nullptr;
    const auto& aset = gripSet(*w, eq.grip);

    // Sprint: running attack overrides everything.
    if (mods.sprinting && !aset.running.empty() && !aset.running[0].attacks.empty())
        return aset.running[0].attacks[0].clip.c_str();

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
