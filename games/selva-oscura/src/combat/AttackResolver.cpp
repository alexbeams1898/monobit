#include "combat/AttackResolver.h"

#include "combat/Weapon.h"

namespace selva::combat
{

namespace
{

bool buttonMatches(const std::string& expected, const char* press)
{
    if (expected.empty() || expected == "any")
        return true;
    return expected == press;
}

} // namespace

ResolvedAttack resolveAttackChainEntry(const PlayerEquipment& eq, HandSide hand,
                                       AttackKind kind, int chain_index, int technique_index,
                                       const selva::anim::ClipRegistry& clips)
{
    ResolvedAttack out;
    const Weapon* w = (hand == HandSide::Right) ? eq.right : eq.left;
    if (w == nullptr || w->cls == nullptr)
        return out;
    const auto& aset = (eq.grip == Grip::TwoHanded) ? w->cls->two_handed : w->cls->one_handed;

    const auto* techniques = &aset.light;
    if (kind == AttackKind::Heavy)
        techniques = &aset.heavy;
    else if (kind == AttackKind::Running && !aset.running.empty())
        techniques = &aset.running;

    if (techniques->empty())
        return out;
    const int t_idx =
        (technique_index >= 0 && technique_index < static_cast<int>(techniques->size()))
            ? technique_index
            : 0;
    const auto& chain = (*techniques)[t_idx].attacks;
    if (chain.empty())
        return out;

    out.chain_size = static_cast<int>(chain.size());
    const int idx = (chain_index >= 0 && chain_index < out.chain_size) ? chain_index : 0;
    out.attack = &chain[idx];
    if (out.attack->clip.empty())
        return out;
    out.clip = clips.get(out.attack->clip);
    return out;
}

TechniqueDispatch dispatchTechniqueForPress(const PlayerEquipment& eq, HandSide hand,
                                            AttackKind kind, int chain_index,
                                            int current_locked, const char* button)
{
    TechniqueDispatch out;
    const Weapon* w = (hand == HandSide::Right) ? eq.right : eq.left;
    if (w == nullptr || w->cls == nullptr)
        return out;
    const auto& aset = (eq.grip == Grip::TwoHanded) ? w->cls->two_handed : w->cls->one_handed;
    const auto* techniques = &aset.light;
    if (kind == AttackKind::Heavy)
        techniques = &aset.heavy;
    else if (kind == AttackKind::Running && !aset.running.empty())
        techniques = &aset.running;
    if (techniques->empty())
        return out;

    if (current_locked >= 0 && current_locked < static_cast<int>(techniques->size()))
    {
        const auto& chain = (*techniques)[current_locked].attacks;
        if (chain_index < 0 || chain_index >= static_cast<int>(chain.size()))
            return out;
        if (!buttonMatches(chain[chain_index].expected_button, button))
            return out;
        out.locked = current_locked;
        out.valid = true;
        return out;
    }

    int last_match = -1;
    int match_count = 0;
    for (std::size_t i = 0; i < techniques->size(); ++i)
    {
        const auto& chain = (*techniques)[i].attacks;
        if (chain_index < 0 || chain_index >= static_cast<int>(chain.size()))
            continue;
        if (buttonMatches(chain[chain_index].expected_button, button))
        {
            last_match = static_cast<int>(i);
            ++match_count;
        }
    }
    if (match_count == 0)
        return out;
    out.valid = true;
    out.locked = (match_count == 1) ? last_match : -1;
    return out;
}

} // namespace selva::combat
