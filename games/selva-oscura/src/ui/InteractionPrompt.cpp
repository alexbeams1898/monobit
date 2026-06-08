#include "ui/InteractionPrompt.h"

#include "interact/Interaction.h"
#include "ui/ClassPickerScreen.h"

#include <imgui.h>

#include <string>

namespace selva::ui
{

void renderInteractionPrompt()
{
    // Modals own the screen while they're up; the prompt would draw
    // over the modal's panel and look like a UI glitch. Suppress here
    // (not at the interact::tick layer) because targeting state stays
    // valid -- the player resumes interacting with the same actor the
    // moment the modal closes.
    if (classPickerActive())
        return;
    const auto* target = selva::interact::currentTarget();
    if (target == nullptr)
        return;

    // Doctrine (insight pillar): the prompt is verb-only. The
    // interactable is in the world right in front of the player; the
    // noun would just be chrome AND would name things the player may
    // not have insight for yet. Talk / Examine / Open / Pickup / Use.
    // The target's label is still computed (used elsewhere, e.g.
    // dialog speaker resolution + debug logs) but never rendered to
    // the prompt.
    const char* verb = selva::interact::kindVerb(target->kind);
    std::string prompt = "[E] ";
    if (verb != nullptr && verb[0] != '\0')
        prompt += verb;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float vw = vp->Size.x;
    const float vh = vp->Size.y;
    const float pad_x = 20.0f;
    const float pad_y = 8.0f;
    const ImVec2 text_size = ImGui::CalcTextSize(prompt.c_str());
    const float box_w = text_size.x + pad_x * 2.0f;
    const float box_h = text_size.y + pad_y * 2.0f;
    const float box_x = vp->Pos.x + (vw - box_w) * 0.5f;
    // Sit above where the dialog box would land so the prompt
    // doesn't collide with that area.
    const float box_y = vp->Pos.y + vh * 0.72f;

    ImGui::SetNextWindowPos(ImVec2(box_x, box_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(box_w, box_h), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.04f, 0.03f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.45f, 0.30f, 0.75f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad_x, pad_y));
    ImGui::Begin("##interact_prompt", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.92f, 0.86f, 1.0f));
    ImGui::TextUnformatted(prompt.c_str());
    ImGui::PopStyleColor();
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

} // namespace selva::ui
