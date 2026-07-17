#include "TunePanel.h"

#include "ReadingColor.h"
#include "gl/PixelRenderTarget.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

namespace tune_panel
{
namespace
{
bool sVisible = false;

// Patch a subset of fields into an existing JSON file without disturbing the
// rest (player.json also holds animation layout; atmosphere.json holds a
// comment). Reads, applies `patch`, writes back. Silently no-ops if the file
// can't be read or written -- this is a dev convenience, not a save system.
template <typename PatchFn> void patchJson(const char* path, PatchFn patch)
{
    nlohmann::json j;
    if (std::ifstream in{path})
    {
        j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded())
            j = nlohmann::json::object();
    }
    patch(j);
    if (std::ofstream out{path})
        out << j.dump(4) << '\n';
}

void savePlayer(const PlayerConfig& pc)
{
    patchJson("config/player.json",
              [&](nlohmann::json& j)
              {
                  j["speed"] = pc.speed;
                  j["run_speed_mult"] = pc.run_speed_mult;
                  // Patch only the frame durations inside the states block; row /
                  // frames / other animation fields are left untouched.
                  auto& states = j["animation"]["states"];
                  states["walk"]["duration"] = pc.walk.duration;
                  states["fast_walk"]["duration"] = pc.fast_walk.duration;
              });
}

void saveGrade(const engine::gl::Grade& g)
{
    patchJson("config/atmosphere.json",
              [&](nlohmann::json& j)
              {
                  auto& grade = j["grade"];
                  grade["saturation"] = g.saturation;
                  grade["brightness"] = g.brightness;
                  grade["tint_r"] = g.tint_r;
                  grade["tint_g"] = g.tint_g;
                  grade["tint_b"] = g.tint_b;
              });
}

} // namespace

void toggle()
{
    sVisible = !sVisible;
}

// Live int sliders for a list of stat names, editing growth.stat_levels in
// place. Values are progression state (not authored config), so there is no
// save -- edits are session-only, to see stats change on the Self tab.
void statSliders(growth::GrowthState& growth, const std::vector<std::string>& names)
{
    for (const auto& name : names)
    {
        int v = growth::statLevel(growth, name);
        if (ImGui::SliderInt(name.c_str(), &v, 0, 20))
            growth.stat_levels[name] = v;
    }
}

void movementTab(PlayerConfig& pc)
{
    ImGui::SliderFloat("Walk speed", &pc.speed, 20.0f, 400.0f, "%.0f px/s");
    ImGui::SliderFloat("Run multiplier", &pc.run_speed_mult, 1.0f, 3.0f, "%.2fx");
    ImGui::Spacing();
    // Per-frame seconds for each locomotion cycle -- lower = quicker legs.
    // Decoupled from speed, so this is the direct cadence-feel knob.
    ImGui::SliderFloat("Walk frame time", &pc.walk.duration, 0.02f, 0.30f, "%.3f s");
    ImGui::SliderFloat("Run frame time", &pc.fast_walk.duration, 0.02f, 0.30f, "%.3f s");
    ImGui::Spacing();
    if (ImGui::Button("Save to player.json"))
        savePlayer(pc);
}

void gradeTab()
{
    // The grade lives in the pixel target; read, edit, write back live.
    engine::gl::Grade g = engine::gl::pixelTargetGetGrade();
    bool changed = false;
    changed |= ImGui::SliderFloat("Saturation", &g.saturation, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("Brightness", &g.brightness, 0.5f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Tint R", &g.tint_r, 0.5f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Tint G", &g.tint_g, 0.5f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Tint B", &g.tint_b, 0.5f, 1.5f, "%.2f");
    if (changed)
        engine::gl::pixelTargetSetGrade(g);
    ImGui::Spacing();
    if (ImGui::Button("Save to atmosphere.json"))
        saveGrade(g);
}

void statsTab(growth::GrowthState& growth)
{
    // Progression values, not config -- edits are session-only and show up on
    // the pause page's Self tab. The self (faculties) then the doing layer.
    ImGui::TextDisabled("The self");
    statSliders(growth, growth.faculties);
    ImGui::Spacing();
    ImGui::TextDisabled("Doing layer");
    statSliders(growth, growth.secondary);
}

// --- Cognition tab: the live tree read from the observation state ----------

ImVec4 statusColor(observations::ThoughtStatus st)
{
    switch (st)
    {
    case observations::ThoughtStatus::Fired:
        return {0.55f, 0.80f, 0.50f, 1.0f}; // green
    case observations::ThoughtStatus::Available:
        return {0.98f, 0.86f, 0.45f, 1.0f}; // gold -- eligible, will roll
    case observations::ThoughtStatus::OutOfReach:
        return {0.55f, 0.57f, 0.62f, 1.0f}; // grey
    }
    return {1, 1, 1, 1};
}

const char* statusWord(observations::ThoughtStatus st)
{
    switch (st)
    {
    case observations::ThoughtStatus::Fired:
        return "FIRED";
    case observations::ThoughtStatus::Available:
        return "available";
    case observations::ThoughtStatus::OutOfReach:
        return "out of reach";
    }
    return "";
}

// Render one clause's requirements compactly (e.g. "stone + water | perception>=2").
std::string clauseText(const unlock::Clause& c)
{
    std::string s;
    for (std::size_t i = 0; i < c.observed.size(); ++i)
        s += (i ? " + " : "") + c.observed[i];
    for (const auto& [name, lvl] : c.stat)
        s += (s.empty() ? "" : " | ") + name + ">=" + std::to_string(lvl);
    if (!c.flag.empty())
        s += (s.empty() ? "" : " | ") + std::string("flag:") + c.flag;
    return s.empty() ? "(always)" : s;
}

void drawEncounterNode(const observations::State& obs, const growth::GrowthState& g,
                       const observations::Encounter& o)
{
    const int reached = obs.observed_tier.count(o.id) ? obs.observed_tier.at(o.id) : 0;
    const observations::Signal sig = observations::signalFor(obs, g, o.id);
    const char* sigName = sig == observations::Signal::Unobserved ? "unseen" : "observed";
    if (!ImGui::TreeNode(o.id.c_str(), "%s   [%s]   value %d   tier %d/%zu", o.id.c_str(), sigName,
                         o.value, reached, o.tiers.size()))
        return;
    if (!o.visible_when.any.empty())
        ImGui::TextColored({0.6f, 0.55f, 0.7f, 1}, "  hidden until: %s",
                           clauseText(o.visible_when.any.front()).c_str());
    for (std::size_t i = 0; i < o.tiers.size(); ++i)
    {
        const bool got = static_cast<int>(i) + 1 <= reached;
        const ImVec4 col = got ? ImVec4{0.55f, 0.80f, 0.50f, 1} : ImVec4{0.6f, 0.6f, 0.6f, 1};
        ImGui::TextColored(col, "  tier %zu %s %s", i + 1, got ? "[reached]" : "[  ]",
                           o.tiers[i].text.c_str());
        // How to unlock this tier (each clause of its unlock_when; base = always).
        const auto& cond = o.tiers[i].unlock_when;
        if (cond.any.empty())
            ImGui::TextColored({0.5f, 0.52f, 0.55f, 1}, "        needs: (always)");
        else
            for (const auto& clause : cond.any)
                ImGui::TextColored({0.5f, 0.52f, 0.55f, 1}, "        needs%s: %s",
                                   cond.any.size() > 1 ? " (any)" : "", clauseText(clause).c_str());
    }
    ImGui::TreePop();
}

void drawThoughtNode(const observations::State& obs, const growth::GrowthState& g,
                     const observations::Thought& r)
{
    const auto st = observations::statusOf(obs, g, r);
    ImGui::TextColored(statusColor(st), "%s", statusWord(st));
    ImGui::SameLine();
    // Header: kind + faculty + the derived rarity word (from difficulty).
    if (!ImGui::TreeNode(r.id.c_str(), "%s   [%s %s | %s]", r.id.c_str(),
                         observations::isSynthesis(r) ? "CONCLUSION" : "thought", r.faculty.c_str(),
                         reading_color::rarityWord(r.difficulty)))
        return;

    // The derived VALUE model, tinted by the rarity color (dev X-ray).
    //   importance = centrality (derived) + emotional_weight (authored)
    //   value      = importance + opening (what it uniquely reveals)
    //   difficulty = structural (separate axis); reward = value * exp
    const Color rc = reading_color::rarityColor(r.difficulty);
    ImGui::TextColored({rc.r, rc.g, rc.b, 1.0f},
                       "  value %d = importance %d (centrality %d + emotion %d) + opening %d",
                       r.value, r.importance, r.centrality, r.emotional_weight, r.opening);
    ImGui::TextColored({rc.r, rc.g, rc.b, 1.0f}, "  difficulty %d (%s)   reward %d EXP",
                       r.difficulty, reading_color::rarityWord(r.difficulty), r.spirit_exp);

    // Expandable: how that centrality number was derived (upstream + downstream).
    if (ImGui::TreeNode((r.id + "_centrality").c_str(), "  how centrality is calculated"))
    {
        for (const auto& line : observations::explainCentrality(obs, r))
            ImGui::TextColored({0.58f, 0.62f, 0.7f, 1.0f}, "%s", line.c_str());
        ImGui::TreePop();
    }

    for (const auto& clause : r.unlock_when.any)
        ImGui::Text("  needs%s: %s", r.unlock_when.any.size() > 1 ? " (any)" : "",
                    clauseText(clause).c_str());
    for (const auto& [name, per] : r.feeders)
        ImGui::Text("  feeder: %s / %d", name.c_str(), per);
    if (!r.set_flag.empty())
        ImGui::Text("  -> sets flag: %s", r.set_flag.c_str());
    // Why it's at this status + what flips it.
    ImGui::Spacing();
    for (const auto& line : observations::explainStatus(obs, g, r))
        ImGui::TextColored({0.62f, 0.64f, 0.68f, 1.0f}, "%s", line.c_str());
    ImGui::TreePop();
}

void cognitionTab(const observations::State& obs, const growth::GrowthState& g)
{
    ImGui::TextDisabled("Live cognition tree (dev X-ray -- reads the loaded state).");
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Encounters", ImGuiTreeNodeFlags_DefaultOpen))
        for (const auto& o : obs.encounters)
            drawEncounterNode(obs, g, o);
    if (ImGui::CollapsingHeader("Thoughts", ImGuiTreeNodeFlags_DefaultOpen))
        for (const auto& r : obs.thoughts)
            drawThoughtNode(obs, g, r);
}

void render(PlayerConfig& pc, growth::GrowthState& growth, const observations::State& obs)
{
    if (!sVisible)
        return;

    // Default size ~3x ImGui's stock width and tall enough for the stats list;
    // FirstUseEver so the user can still resize and the choice sticks.
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 760.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tunables (F1)", &sVisible);

    if (ImGui::BeginTabBar("tunables"))
    {
        if (ImGui::BeginTabItem("Movement"))
        {
            movementTab(pc);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Grade"))
        {
            gradeTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Stats"))
        {
            statsTab(growth);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Cognition"))
        {
            cognitionTab(obs, growth);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace tune_panel
