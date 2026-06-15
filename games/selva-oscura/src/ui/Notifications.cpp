#include "ui/Notifications.h"

#include "WallClock.h"

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace selva::ui
{

namespace
{

constexpr float kDuration = 2.0f;          // total lifetime (seconds)
constexpr float kFadeWindow = 0.5f;        // fade duration at end of life
constexpr float kRiseSpeedPxPerSec = 30.f; // upward drift
constexpr int kMaxVisible = 8;
constexpr float kRightMarginPx = 16.f;
constexpr float kBottomMarginPx = 120.f;
constexpr float kPadX = 10.f;
constexpr float kPadY = 6.f;
constexpr float kGapBetweenEntriesPx = 4.f;
constexpr float kIconSizePx = 24.f; // reserved slot width when icon_path is set

std::vector<Notification>& stack()
{
    static std::vector<Notification> v;
    return v;
}

} // namespace

void pushNotification(std::string text, glm::vec4 color, std::string icon_path)
{
    auto& s = stack();
    if (static_cast<int>(s.size()) >= kMaxVisible)
        s.erase(s.begin());
    Notification n;
    n.text = std::move(text);
    n.color = color;
    n.icon_path = std::move(icon_path);
    n.timer = kDuration;
    n.y_offset = 0.0f;
    s.push_back(std::move(n));
}

void tickNotifications(float dt)
{
    auto& s = stack();
    for (auto& n : s)
    {
        n.timer -= dt;
        n.y_offset += kRiseSpeedPxPerSec * dt;
    }
    s.erase(
        std::remove_if(s.begin(), s.end(), [](const Notification& n) { return n.timer <= 0.0f; }),
        s.end());
}

void renderNotifications()
{
    auto& s = stack();
    if (s.empty())
        return;

    const ImGuiIO& io = ImGui::GetIO();
    const float screen_w = io.DisplaySize.x;
    const float screen_h = io.DisplaySize.y;
    ImDrawList* fg = ImGui::GetForegroundDrawList();

    float baseline_y = screen_h - kBottomMarginPx;

    // Iterate newest-to-oldest so the newest entry sits at the
    // bottom (just above kBottomMarginPx) and older entries stack
    // upward.
    for (auto it = s.rbegin(); it != s.rend(); ++it)
    {
        const Notification& n = *it;
        const float fade_alpha = std::clamp(n.timer / kFadeWindow, 0.0f, 1.0f);

        const ImVec2 text_size = ImGui::CalcTextSize(n.text.c_str());
        const float icon_slot = n.icon_path.empty() ? 0.0f : (kIconSizePx + kPadX);
        const float box_w = kPadX * 2.0f + icon_slot + text_size.x;
        const float box_h = kPadY * 2.0f + std::max(text_size.y, kIconSizePx);

        const float box_x = screen_w - kRightMarginPx - box_w;
        const float box_y = baseline_y - n.y_offset - box_h;

        // Background: dark panel scaled by fade.
        const int bg_alpha = static_cast<int>(140.0f * fade_alpha);
        const ImU32 bg_color = IM_COL32(0, 0, 0, bg_alpha);
        fg->AddRectFilled(ImVec2(box_x, box_y), ImVec2(box_x + box_w, box_y + box_h), bg_color,
                          4.0f);

        // Icon slot reserved (filled when texture binding lands).
        // For now: a faint rounded square placeholder so authors
        // can see where the icon will sit.
        if (!n.icon_path.empty())
        {
            const float ix = box_x + kPadX;
            const float iy = box_y + (box_h - kIconSizePx) * 0.5f;
            const int ph_alpha = static_cast<int>(60.0f * fade_alpha);
            fg->AddRectFilled(ImVec2(ix, iy), ImVec2(ix + kIconSizePx, iy + kIconSizePx),
                              IM_COL32(200, 200, 200, ph_alpha), 3.0f);
        }

        const float text_x = box_x + kPadX + icon_slot;
        const float text_y = box_y + (box_h - text_size.y) * 0.5f;
        const int tr = static_cast<int>(n.color.r * 255.0f);
        const int tg = static_cast<int>(n.color.g * 255.0f);
        const int tb = static_cast<int>(n.color.b * 255.0f);
        const int ta = static_cast<int>(255.0f * fade_alpha * n.color.a);
        fg->AddText(ImVec2(text_x, text_y), IM_COL32(tr, tg, tb, ta), n.text.c_str());

        baseline_y -= (box_h + kGapBetweenEntriesPx);
    }
}

void clearNotifications()
{
    stack().clear();
}

} // namespace selva::ui
