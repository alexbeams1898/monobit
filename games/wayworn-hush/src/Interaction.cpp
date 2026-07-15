#include "Interaction.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <algorithm>
#include <cmath>

namespace interaction
{
namespace
{
// Distance from a point to the nearest edge of a box (0 if inside).
float distanceToBox(float px, float py, const Candidate& c)
{
    const float ddx = std::max({0.0f, c.cx - c.w * 0.5f - px, px - (c.cx + c.w * 0.5f)});
    const float ddy = std::max({0.0f, c.cy - c.h * 0.5f - py, py - (c.cy + c.h * 0.5f)});
    return std::sqrt(ddx * ddx + ddy * ddy);
}

bool boxContains(float px, float py, const Candidate& c)
{
    return px >= c.cx - c.w * 0.5f && px <= c.cx + c.w * 0.5f && py >= c.cy - c.h * 0.5f &&
           py <= c.cy + c.h * 0.5f;
}
} // namespace

Resolution resolve(const std::vector<Candidate>& items, const Intent& intent, float reach)
{
    // Two independent nearest-wins passes (prison-escape's proven shape):
    //   hover  = nearest box the cursor is INSIDE (mouse targeting)
    //   prox   = nearest box within `reach` of the PLAYER (keyboard targeting)
    // Hover beats proximity so the cursor overrides where you're standing; both must be in
    // reach of the player (you can't act on something across the map by hovering it).
    int hover = -1;
    int prox = -1;
    float hoverDist = 0.0f;
    float proxDist = reach;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        const Candidate& c = items[static_cast<std::size_t>(i)];
        const float pd = distanceToBox(intent.px, intent.py, c);
        if (pd <= proxDist)
        {
            prox = i;
            proxDist = pd;
        }
        if (intent.mouse_valid && boxContains(intent.mouse_x, intent.mouse_y, c) && pd <= reach)
        {
            const float md = distanceToBox(intent.mouse_x, intent.mouse_y, c); // 0 inside; tiebreak
            if (hover < 0 || md < hoverDist)
            {
                hover = i;
                hoverDist = md;
            }
        }
    }

    Resolution out;
    out.index = (hover >= 0) ? hover : prox;
    if (out.index < 0)
        return out;
    // Fire on the interact key, or a click that landed on a HOVERED interactable (a click
    // in empty space -- no hover -- must not fire the proximity target).
    out.fire = intent.pressed || (intent.clicked && hover >= 0 && out.index == hover);
    return out;
}

Outcome update(EntityManager& em, const Intent& intent, const Context& ctx)
{
    auto& reg = em.registry();

    // Build the candidate list + a parallel entity list (same order) so the resolution
    // index maps back to the ECS entity.
    std::vector<Candidate> items;
    std::vector<entt::entity> ents;
    for (auto [e, t, it] : reg.view<Transform, Interactable>().each())
    {
        items.push_back({t.x, t.y, it.w, it.h});
        ents.push_back(e);
        it.active = false; // cleared each frame; the resolved target is re-set below
    }

    const Resolution r = resolve(items, intent, ctx.reach);
    if (r.index < 0)
        return {};

    Interactable& target = reg.get<Interactable>(ents[static_cast<std::size_t>(r.index)]);
    target.active = true; // drives the highlight
    if (!r.fire)
        return {};

    Outcome out;
    out.fired = true;
    out.kind = target.kind;
    out.target = target.target;
    switch (target.kind)
    {
    case Kind::Observe:
        out.earned = observations::observeById(ctx.obs, ctx.growth, target.target, ctx.rng).earned;
        break;
    }
    return out;
}

} // namespace interaction
