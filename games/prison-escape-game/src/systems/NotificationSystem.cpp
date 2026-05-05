#include "systems/NotificationSystem.h"

#include <algorithm>
#include <tracy/Tracy.hpp>
#include <vector>

static constexpr float DURATION = 2.0f;
static constexpr float RISE_SPEED = 30.0f; // pixels per second
static constexpr int MAX_VISIBLE = 8;
static constexpr float RIGHT_MARGIN = 16.0f;
static constexpr float BOTTOM_MARGIN = 120.0f;
static constexpr float NOTIF_PAD_X = 10.0f;
static constexpr float NOTIF_PAD_Y = 6.0f;

struct Notification
{
    std::string text;
    Color color{};
    float timer = 0.0f;
    float y_offset = 0.0f;
    bool use_title_font = false;
};

static std::vector<Notification> sNotifications;
static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;

void NotificationSystem::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
    sNotifications.reserve(16);
}

void NotificationSystem::push(const std::string& text, const Color& color)
{
    Notification n;
    n.text = text;
    n.color = color;
    n.timer = DURATION;
    n.y_offset = 0.0f;

    // Check if this is a "Level Up!" notification for title font.
    if (text.find("Level Up") != std::string::npos)
        n.use_title_font = true;

    sNotifications.push_back(n);

    // Cap visible notifications.
    while (static_cast<int>(sNotifications.size()) > MAX_VISIBLE)
        sNotifications.erase(sNotifications.begin());
}

void NotificationSystem::render(float dt, int window_w, int window_h)
{
    ZoneScopedN("NotificationSystem");

    if (sNotifications.empty())
        return;

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);

    // Update and render from bottom to top.
    float baseline_y = wh - BOTTOM_MARGIN;

    for (int i = static_cast<int>(sNotifications.size()) - 1; i >= 0; --i)
    {
        auto& n = sNotifications[static_cast<size_t>(i)];
        n.timer -= dt;
        n.y_offset += RISE_SPEED * dt;

        if (n.timer <= 0.0f)
            continue;

        // Fade out over the last 0.5 seconds.
        const float alpha = std::min(1.0f, n.timer / 0.5f);
        Color c = n.color;
        c.a *= alpha;

        const FontHandle font = sTitleFont;
        const TextSize sz = UIRenderer::measureText(font, n.text);
        const float x = ww - sz.width - RIGHT_MARGIN - NOTIF_PAD_X;
        const float y = baseline_y - n.y_offset;

        // Background panel.
        const Color bg{0.0f, 0.0f, 0.0f, 0.5f * alpha};
        UIRenderer::drawRect(x - NOTIF_PAD_X, y - NOTIF_PAD_Y, sz.width + NOTIF_PAD_X * 2.0f,
                             sz.height + NOTIF_PAD_Y * 2.0f, bg);

        UIRenderer::drawText(font, n.text, x, y, c);
        baseline_y -= sz.height + NOTIF_PAD_Y * 2.0f + 4.0f;
    }

    // Remove expired notifications.
    sNotifications.erase(std::remove_if(sNotifications.begin(), sNotifications.end(),
                                        [](const Notification& n) { return n.timer <= 0.0f; }),
                         sNotifications.end());
}
