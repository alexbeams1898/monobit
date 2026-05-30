#include "ui/ComboHud.h"

#include "Tunables.h"
#include "WallClock.h"
#include "combat/AttackChain.h"
#include "combat/ChainObserver.h"
#include "combat/CombatData.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace selva::ui
{

namespace
{

const char* labelFor(const std::string& s)
{
    if (s == "LMB")
        return "L";
    if (s == "RMB")
        return "R";
    return "*";
}

// Slot-N button for the matched technique, where N = observer.step.
// Returns nullptr if no technique matched or step is past the end.
const char* matchedTechniqueNextButton(const std::vector<selva::combat::WeaponTechnique>& techs)
{
    const auto& obs = selva::combat::chainState();
    if (obs.technique_id == nullptr || obs.step <= 0)
        return nullptr;
    for (const auto& tech : techs)
    {
        if (tech.id != obs.technique_id)
            continue;
        if (obs.step < static_cast<int>(tech.attacks.size()))
            return labelFor(tech.attacks[obs.step].expected_button);
        return "-";
    }
    return nullptr;
}

// Union of all techniques' slot-0 expected buttons. Used when no
// chain is in progress.
const char* unionOfSlotZeroButtons(const std::vector<selva::combat::WeaponTechnique>& techs)
{
    bool has_lmb = false;
    bool has_rmb = false;
    bool has_any = false;
    for (const auto& tech : techs)
    {
        if (tech.attacks.empty())
            continue;
        const auto& exp = tech.attacks[0].expected_button;
        if (exp == "LMB")
            has_lmb = true;
        else if (exp == "RMB")
            has_rmb = true;
        else
            has_any = true;
    }
    if (has_any || (has_lmb && has_rmb))
        return "L/R";
    if (has_lmb)
        return "L";
    if (has_rmb)
        return "R";
    return "-";
}

// Next button to advance the chain. If a technique is matched, return
// the slot-N expected button (where N = observer.step, the next press).
// Otherwise return the union of all techniques' slot-0 buttons.
const char* nextExpectedButtonLabel()
{
    using selva::combat::Grip;
    const selva::combat::Weapon* w = selva::combat::equipment().right;
    if (w == nullptr || w->cls == nullptr)
        return "-";
    const auto& aset = (selva::combat::equipment().grip == Grip::TwoHanded) ? w->cls->two_handed
                                                                            : w->cls->one_handed;
    if (aset.light.empty())
        return "-";
    if (const char* matched = matchedTechniqueNextButton(aset.light))
        return matched;
    return unionOfSlotZeroButtons(aset.light);
}

} // namespace

void renderComboHud()
{
    if (!selva::tuning::current().debug_show_combo_hud)
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = 360.0f;
    const float h = 80.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y - h - 30.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##ComboHUD", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoMove);

    const auto& obs = selva::combat::chainState();
    const char* state = obs.last_press_perfect             ? "PERFECT"
                        : (obs.last_press_accuracy > 0.0f) ? "HIT"
                                                           : "READY";
    const char* tech_id = (obs.technique_id != nullptr) ? obs.technique_id : "-";
    ImGui::Text("Combo: %s @ %d  -  %s  acc=%.2f", tech_id, obs.step, state,
                obs.last_press_accuracy);

    const char* next_btn = nextExpectedButtonLabel();
    const float btn_box_w = 28.0f;
    const float bar_h = 16.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 btn_bg = IM_COL32(60, 60, 100, 220);
    const ImU32 btn_fg = IM_COL32(230, 230, 255, 255);
    draw->AddRectFilled(pos, ImVec2(pos.x + btn_box_w, pos.y + bar_h), btn_bg);
    const ImVec2 ts = ImGui::CalcTextSize(next_btn);
    draw->AddText(ImVec2(pos.x + (btn_box_w - ts.x) * 0.5f, pos.y + (bar_h - ts.y) * 0.5f), btn_fg,
                  next_btn);

    const auto& cw = selva::combat::cancelWindow(selva::combat::HandSide::Right);
    const float open = cw.open_at;
    const float close = cw.close_at;
    const float now = selva::wallClock();
    const float view_secs = 1.0f;
    const float bar_x = pos.x + btn_box_w + 6.0f;
    const float bar_w = w - 16.0f - (btn_box_w + 6.0f);
    const ImU32 bg = IM_COL32(40, 40, 40, 200);
    const ImU32 win = IM_COL32(60, 200, 80, 220);
    const ImU32 perfect = IM_COL32(255, 220, 80, 240);
    const ImU32 marker = IM_COL32(255, 240, 80, 255);
    draw->AddRectFilled(ImVec2(bar_x, pos.y), ImVec2(bar_x + bar_w, pos.y + bar_h), bg);
    if (close > open && open > 0.0f)
    {
        const float view_start = now - view_secs * 0.5f;
        const float view_end = now + view_secs * 0.5f;
        auto t_to_x = [&](float t) -> float
        {
            const float u = (t - view_start) / (view_end - view_start);
            return bar_x + std::clamp(u, 0.0f, 1.0f) * bar_w;
        };
        const float x_open = t_to_x(open);
        const float x_close = t_to_x(close);
        if (x_close > x_open)
            draw->AddRectFilled(ImVec2(x_open, pos.y), ImVec2(x_close, pos.y + bar_h), win);

        const float threshold = selva::tuning::current().perfect_accuracy_threshold;
        const float perfect_frac = std::clamp(1.0f - threshold, 0.0f, 1.0f);
        const float center = 0.5f * (open + close);
        const float half = 0.5f * (close - open) * perfect_frac;
        const float x_perfect_open = t_to_x(center - half);
        const float x_perfect_close = t_to_x(center + half);
        if (x_perfect_close > x_perfect_open)
            draw->AddRectFilled(ImVec2(x_perfect_open, pos.y),
                                ImVec2(x_perfect_close, pos.y + bar_h), perfect);
    }
    const float x_now = bar_x + bar_w * 0.5f;
    draw->AddLine(ImVec2(x_now, pos.y - 2), ImVec2(x_now, pos.y + bar_h + 2), marker, 2.0f);
    ImGui::Dummy(ImVec2(w - 16.0f, bar_h));

    ImGui::End();
}

} // namespace selva::ui
