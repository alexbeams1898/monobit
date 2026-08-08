#include "Floaters.h"

#include "Engine.h"
#include "ScreenStyle.h"
#include "UIRenderer.h"

#include <algorithm>
#include <vector>

namespace floaters
{
namespace
{

struct Floater
{
    float x = 0.0f; // world
    float y = 0.0f;
    float age = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;
    std::string text;
};

std::vector<Floater> sFloaters;

constexpr float kLife = 0.65f;
constexpr float kRise = 26.0f; // world px travelled over a lifetime

// A swarm generates hundreds of numbers a second and they would bury the screen. The oldest go
// first, so what stays is what just happened.
constexpr size_t kMaxFloaters = 64;

} // namespace

void add(float worldX, float worldY, const std::string& text, float r, float g, float b)
{
    if (sFloaters.size() >= kMaxFloaters)
        sFloaters.erase(sFloaters.begin());
    sFloaters.push_back(Floater{worldX, worldY, 0.0f, r, g, b, text});
}

void update(float dt)
{
    for (auto& f : sFloaters)
        f.age += dt;
    sFloaters.erase(std::remove_if(sFloaters.begin(), sFloaters.end(),
                                   [](const Floater& f) { return f.age >= kLife; }),
                    sFloaters.end());
}

void render(Engine& engine, float camX, float camY, int zoom)
{
    const auto z = static_cast<float>(zoom);
    const float halfW = static_cast<float>(engine.windowWidth()) / (2.0f * z);
    const float halfH = static_cast<float>(engine.windowHeight()) / (2.0f * z);

    for (const auto& f : sFloaters)
    {
        const float t = f.age / kLife;
        // Rises fast then slows, which reads as a thing thrown off the hit rather than a label
        // sliding upward at a constant rate.
        const float rise = kRise * (1.0f - (1.0f - t) * (1.0f - t));
        const float sx = (f.x - (camX - halfW)) * z;
        const float sy = (f.y - rise - (camY - halfH)) * z;
        screen_style::textCentered(f.text, sx, sy, Color{f.r, f.g, f.b, 1.0f - t});
    }
}

void clear()
{
    sFloaters.clear();
}

} // namespace floaters
