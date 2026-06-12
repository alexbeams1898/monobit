#include "ui/UIComponents.h"

namespace selva::ui
{

void beginCenteredWindow(const char* name, ImVec2 size)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pos(vp->Pos.x + (vp->Size.x - size.x) * 0.5f,
                     vp->Pos.y + (vp->Size.y - size.y) * 0.5f);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::Begin(name, nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar);
}

void drawFullScreenBackdrop(float alpha)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->Size, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, alpha));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##backdrop", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void drawHintBar(const char* text, ImVec2 modal_size)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    constexpr float kGap = 8.0f;
    const float modal_top = vp->Pos.y + (vp->Size.y - modal_size.y) * 0.5f;
    const float modal_bottom = modal_top + modal_size.y;

    const float w = ImGui::CalcTextSize(text).x + 24.0f;
    const float h = ImGui::GetFontSize() + 16.0f;
    const ImVec2 pos(vp->Pos.x + (vp->Size.x - w) * 0.5f, modal_bottom + kGap);
    const ImVec2 size(w, h);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##hint", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("%s", text);
    ImGui::End();
}

bool rmbClicked()
{
    return ImGui::IsMouseClicked(ImGuiMouseButton_Right);
}

bool wantBack()
{
    return ImGui::IsKeyPressed(ImGuiKey_Escape) || rmbClicked();
}

bool centeredButton(const char* label, float width)
{
    const float region = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (region - width) * 0.5f);
    return ImGui::Button(label, ImVec2(width, 0));
}

float drawRomanGlyphs(ImDrawList* draw, const ImVec2& pos,
                      const selva::gameplay::RomanRendering& rendering, ImU32 color)
{
    if (rendering.glyphs.empty())
        return 0.0f;
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();
    // Bar layout. Single bar sits a hair above the glyph row; the
    // second bar (double vinculum, x1,000,000) stripes above that.
    constexpr float kSingleBarOffsetY = 2.0f;
    constexpr float kDoubleBarStripeGapY = 2.5f;
    float pen_x = pos.x;
    for (std::size_t i = 0; i < rendering.glyphs.size(); ++i)
    {
        const char buf[2] = {rendering.glyphs[i], '\0'};
        const ImVec2 size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf);
        draw->AddText(font, font_size, ImVec2(pen_x, pos.y), color, buf);
        const std::uint8_t bar_count = rendering.bars[i];
        if (bar_count >= 1u)
        {
            const float y1 = pos.y - kSingleBarOffsetY;
            draw->AddLine(ImVec2(pen_x, y1), ImVec2(pen_x + size.x, y1), color, 1.0f);
        }
        if (bar_count >= 2u)
        {
            const float y2 = pos.y - kSingleBarOffsetY - kDoubleBarStripeGapY;
            draw->AddLine(ImVec2(pen_x, y2), ImVec2(pen_x + size.x, y2), color, 1.0f);
        }
        pen_x += size.x;
    }
    return pen_x - pos.x;
}

} // namespace selva::ui
