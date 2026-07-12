#include "TunePanel.h"

#include "gl/PixelRenderTarget.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <fstream>

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

bool visible()
{
    return sVisible;
}

void render(PlayerConfig& pc)
{
    if (!sVisible)
        return;

    // Default size ~3x ImGui's stock width; FirstUseEver so the user can still
    // resize and the choice sticks.
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 500.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tunables (F1)", &sVisible);

    if (ImGui::CollapsingHeader("Movement", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Walk speed", &pc.speed, 20.0f, 400.0f, "%.0f px/s");
        ImGui::SliderFloat("Run multiplier", &pc.run_speed_mult, 1.0f, 3.0f, "%.2fx");
        if (ImGui::Button("Save movement"))
            savePlayer(pc);
    }

    if (ImGui::CollapsingHeader("Animation cadence", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Per-frame seconds for each locomotion cycle -- lower = quicker legs.
        // Decoupled from speed, so this is the direct cadence-feel knob.
        ImGui::SliderFloat("Walk frame time", &pc.walk.duration, 0.02f, 0.30f, "%.3f s");
        ImGui::SliderFloat("Run frame time", &pc.fast_walk.duration, 0.02f, 0.30f, "%.3f s");
        if (ImGui::Button("Save cadence"))
            savePlayer(pc);
    }

    if (ImGui::CollapsingHeader("Color grade", ImGuiTreeNodeFlags_DefaultOpen))
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
        if (ImGui::Button("Save grade"))
            saveGrade(g);
    }

    ImGui::End();
}

} // namespace tune_panel
