#include "DebugPanel.h"

#include "Engine.h"
#include "Log.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <imgui.h>

namespace debug_panel
{
namespace
{
bool sVisible = false;

// 120 px/s is exactly 2 px/tick at 60 Hz. Whole pixels per tick matter: the camera rounds to
// an internal pixel every frame, so a speed that does not divide evenly makes the world scroll
// in uneven steps, which reads as lag while nothing is actually late.
float sWalkSpeed = 120.0f;

// 3x: a 40px character lands around 120 screen pixels, which is roughly the proportion Mother
// and Pokemon give theirs. At 2x he reads as a distant figure rather than the person you are.
int sZoom = 3;
} // namespace

void toggle()
{
    sVisible = !sVisible;
}

float walkSpeed()
{
    return sWalkSpeed;
}

int zoom()
{
    return sZoom;
}

void render(Engine& engine, EntityManager& em)
{
    if (!sVisible)
        return;

    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Point of Entry -- dev (F1)"))
    {
        ImGui::Text("%.1f fps  (%.2f ms)", 1.0 / engine.frameDt(), engine.frameDt() * 1000.0);
        ImGui::Separator();

        ImGui::TextUnformatted("Movement");
        // Stepped in whole pixels-per-tick, because anything between them scrolls unevenly.
        int perTick = static_cast<int>(sWalkSpeed / 60.0f + 0.5f);
        if (ImGui::SliderInt("px per tick", &perTick, 1, 6))
            sWalkSpeed = static_cast<float>(perTick) * 60.0f;
        ImGui::TextDisabled("%.0f px/s -- a 32px tile every %.2fs", static_cast<double>(sWalkSpeed),
                            32.0 / static_cast<double>(sWalkSpeed));

        ImGui::Separator();
        ImGui::TextUnformatted("View");
        // Integer only -- see zoom() in the header for why.
        if (ImGui::SliderInt("zoom", &sZoom, 1, 6))
            poe::log().info("view: zoom {}x", sZoom);
        ImGui::TextDisabled("internal %d x %d", engine.windowWidth() / sZoom,
                            engine.windowHeight() / sZoom);

        ImGui::Separator();
        ImGui::TextUnformatted("World");
        ImGui::TextDisabled("%d drawn things",
                            static_cast<int>(em.registry().view<Sprite>().size()));
        ImGui::TextDisabled("map %d x %d @ %dpx", em.tile_map.width, em.tile_map.height,
                            em.tile_map.tile_size);

        ImGui::Separator();
        ImGui::TextDisabled("F12 writes frame.png beside the exe");
    }
    ImGui::End();
}

} // namespace debug_panel
