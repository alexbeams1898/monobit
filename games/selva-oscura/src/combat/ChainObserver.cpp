#include "combat/ChainObserver.h"

#include "Tunables.h"
#include "combat/CombatLog.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace selva::combat
{

namespace
{

constexpr std::size_t kMaxHistory = 8;

struct Press
{
    const char* button = nullptr; // pointer to static "LMB"/"RMB"
    float t = 0.0f;
};

struct ObserverState
{
    std::array<Press, kMaxHistory> history{};
    std::size_t count = 0;
    ChainState current{};
    float last_press_t = -1.0f;
    float last_window_center = 0.0f;
    float last_window_half = 0.0f;
    CancelWindow cancel_right;
    CancelWindow cancel_left;
};

ObserverState sObs;

// True if `tech` matches the most-recent N presses where N = tech.attacks.size().
bool techniqueMatchesTail(const WeaponTechnique& tech, const Press* hist, std::size_t hist_count,
                          int up_to_step)
{
    if (up_to_step <= 0 || up_to_step > static_cast<int>(tech.attacks.size()))
        return false;
    if (hist_count < static_cast<std::size_t>(up_to_step))
        return false;
    for (int i = 0; i < up_to_step; ++i)
    {
        const Press& p = hist[hist_count - up_to_step + i];
        const auto& exp = tech.attacks[i].expected_button;
        if (exp.empty() || exp == "any")
            continue;
        if (exp != p.button)
            return false;
    }
    return true;
}

const WeaponGripAnimSet& gripSet(const Weapon& w, Grip grip)
{
    return (grip == Grip::TwoHanded) ? w.cls->two_handed : w.cls->one_handed;
}

void rescanChain(const PlayerEquipment& eq)
{
    const char* prev_id = sObs.current.technique_id;
    const int prev_step = sObs.current.step;
    sObs.current.technique_id = nullptr;
    sObs.current.step = 0;
    if (sObs.count == 0 || eq.right == nullptr || eq.right->cls == nullptr)
    {
        combatLog("[combat:obs] rescan EMPTY (count={} eq.right={}) prev={}@{} -> -@0", sObs.count,
                  fmt::ptr(eq.right), prev_id ? prev_id : "-", prev_step);
        return;
    }
    const auto& aset = gripSet(*eq.right, eq.grip);
    // Find longest tail-match across all techniques.
    int best_step = 0;
    const WeaponTechnique* best_tech = nullptr;
    for (const auto& tech : aset.light)
    {
        const int max_check =
            std::min(static_cast<int>(tech.attacks.size()), static_cast<int>(sObs.count));
        for (int n = max_check; n > best_step; --n)
        {
            if (techniqueMatchesTail(tech, sObs.history.data(), sObs.count, n))
            {
                best_step = n;
                best_tech = &tech;
                break;
            }
        }
    }
    if (best_tech != nullptr)
    {
        sObs.current.technique_id = best_tech->id.c_str();
        sObs.current.step = best_step;
        // Technique completed (matched all its steps) — keep the HUD
        // displaying the finisher state but clear history so the next
        // press starts a fresh chain. Without history clear, the final
        // press would seed the same technique again at step 1.
        if (best_step >= static_cast<int>(best_tech->attacks.size()))
        {
            combatLog("[combat:obs] rescan COMPLETE {} @ step {} -> reset history", best_tech->id,
                      best_step);
            sObs.count = 0;
        }
    }
    combatLog("[combat:obs] rescan tech_count={} hist_count={} prev={}@{} -> {}@{}",
              aset.light.size(), sObs.count, prev_id ? prev_id : "-", prev_step,
              sObs.current.technique_id ? sObs.current.technique_id : "-", sObs.current.step);
}

} // namespace

void recordPress(const PlayerEquipment& eq, const char* button, float wall_clock_seconds,
                 float cancel_window_center, float cancel_window_half_width)
{
    // Score accuracy of THIS press against the PREVIOUS fire's window.
    if (sObs.last_press_t > 0.0f && sObs.last_window_half > 0.0f)
    {
        const float d = std::fabs(wall_clock_seconds - sObs.last_window_center);
        sObs.current.last_press_accuracy = std::max(0.0f, 1.0f - (d / sObs.last_window_half));
        const float threshold = selva::tuning::current().perfect_accuracy_threshold;
        sObs.current.last_press_perfect = sObs.current.last_press_accuracy >= threshold;
    }
    else
    {
        sObs.current.last_press_accuracy = 0.0f;
        sObs.current.last_press_perfect = false;
    }

    // Append press; drop oldest if over capacity.
    if (sObs.count >= kMaxHistory)
    {
        for (std::size_t i = 1; i < kMaxHistory; ++i)
            sObs.history[i - 1] = sObs.history[i];
        sObs.count = kMaxHistory - 1;
    }
    sObs.history[sObs.count++] = {button, wall_clock_seconds};
    sObs.last_press_t = wall_clock_seconds;
    sObs.last_window_center = cancel_window_center;
    sObs.last_window_half = cancel_window_half_width;

    combatLog("[combat:obs] recordPress button={} t={:.4f} hist_count={} acc={:.2f} perfect={}",
              button, wall_clock_seconds, sObs.count, sObs.current.last_press_accuracy,
              sObs.current.last_press_perfect ? 1 : 0);

    rescanChain(eq);
}

const ChainState& chainState()
{
    return sObs.current;
}

void setCancelWindow(HandSide hand, float open_at, float close_at)
{
    CancelWindow& w = (hand == HandSide::Right) ? sObs.cancel_right : sObs.cancel_left;
    w.open_at = open_at;
    w.close_at = close_at;
}

const CancelWindow& cancelWindow(HandSide hand)
{
    return (hand == HandSide::Right) ? sObs.cancel_right : sObs.cancel_left;
}

void resetChain()
{
    combatLog("[combat:obs] resetChain (was tech={}@{} hist_count={})",
              sObs.current.technique_id ? sObs.current.technique_id : "-", sObs.current.step,
              sObs.count);
    sObs.count = 0;
    sObs.current = {};
    sObs.last_press_t = -1.0f;
    sObs.last_window_center = 0.0f;
    sObs.last_window_half = 0.0f;
}

void tickChainObserver(float wall_clock_seconds, bool one_shot_active)
{
    // While a clip is playing the player can't press the next button
    // (presses are buffered, not recorded). Holding the grace clock
    // forward during the clip would drain the chain mid-flight.
    if (one_shot_active)
    {
        sObs.last_press_t = wall_clock_seconds;
        return;
    }
    const float grace = selva::tuning::current().combo_reset_grace_seconds;
    if (sObs.last_press_t > 0.0f && wall_clock_seconds - sObs.last_press_t > grace)
    {
        combatLog("[combat:obs] tick GRACE EXPIRED last_press_t={:.4f} now={:.4f} grace={:.2f}",
                  sObs.last_press_t, wall_clock_seconds, grace);
        resetChain();
    }
}

} // namespace selva::combat
