#include "renderers/NotificationRenderer.h"

#include "Engine.h"
#include "UIRenderer.h"
#include "screens/ScreenStyle.h"

#include <algorithm>
#include <vector>

namespace notify
{
namespace
{
struct Note
{
    std::string key;
    std::string label;
    int count = 0;
    float life = 0.0f;  // remaining
    float flash = 0.0f; // brightness pop on arrival and on every bump
};

std::vector<Note> sNotes;

constexpr float kLife = 2.4f;
constexpr float kFlash = 0.25f;
constexpr float kFadeTail = 0.5f; // the last stretch of life fades out
constexpr std::size_t kMaxLines = 5;

} // namespace

void item(const std::string& key, const std::string& label, int count)
{
    for (auto& n : sNotes)
        if (n.key == key)
        {
            n.count += count;
            n.life = kLife; // a bump keeps the line warm
            n.flash = kFlash;
            return;
        }
    if (sNotes.size() >= kMaxLines)
        sNotes.erase(sNotes.begin()); // the oldest yields; the feed never buries the HUD
    sNotes.push_back(Note{key, label, count, kLife, kFlash});
}

void render(Engine& engine, float dt)
{
    for (auto& n : sNotes)
    {
        n.life -= dt;
        n.flash = std::max(0.0f, n.flash - dt);
    }
    sNotes.erase(
        std::remove_if(sNotes.begin(), sNotes.end(), [](const Note& n) { return n.life <= 0.0f; }),
        sNotes.end());

    // Bottom-left, stacked upward above the tool block; newest nearest the tool.
    const float lh = screen_style::lineHeight();
    const float x = screen_style::pad(4);
    float y = static_cast<float>(engine.windowHeight()) - screen_style::pad(4) - lh * 3.6f;
    for (auto it = sNotes.rbegin(); it != sNotes.rend(); ++it)
    {
        const float alpha = std::min(1.0f, it->life / kFadeTail);
        // The flash lifts the line toward white on every bump -- the "another one" pop.
        const float f = it->flash / kFlash;
        const Color c{0.85f + 0.15f * f, 0.82f + 0.18f * f, 0.7f + 0.3f * f, alpha};
        screen_style::text("+" + std::to_string(it->count) + "  " + it->label, x, y, c);
        y -= lh * 1.25f;
    }
}

void clear()
{
    sNotes.clear();
}

} // namespace notify
