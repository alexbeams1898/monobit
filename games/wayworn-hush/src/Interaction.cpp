#include "Interaction.h"

#include "Inventory.h"
#include "Yields.h"
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

// How far off dead-ahead still counts as facing a thing, as a dot product of
// unit vectors: 0 = a full half-plane (anything not behind you). Generous on
// purpose -- turning toward something should be enough, not aiming at it.
constexpr float kFacingDot = 0.0f;

// Is the box in front of the player? Measured to the box's NEAREST POINT, not its
// center, so a wide thing you stand beside still reads as ahead. A box you are
// standing INSIDE is always faced (there is no "behind" at zero distance).
bool inFront(const Intent& intent, const Candidate& c)
{
    const float nx = std::clamp(intent.px, c.cx - c.w * 0.5f, c.cx + c.w * 0.5f);
    const float ny = std::clamp(intent.py, c.cy - c.h * 0.5f, c.cy + c.h * 0.5f);
    const float dx = nx - intent.px;
    const float dy = ny - intent.py;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0001f)
        return true;
    return (dx / len) * intent.face_dx + (dy / len) * intent.face_dy > kFacingDot;
}
} // namespace

Resolution resolve(const std::vector<Candidate>& items, const Intent& intent, float reach)
{
    // Two independent nearest-wins passes (prison-escape's proven shape):
    //   hover  = nearest box the cursor is INSIDE (mouse targeting)
    //   prox   = nearest box within `reach` of the PLAYER AND in front of them
    // Hover beats proximity so the cursor overrides where you're standing; both must be in
    // reach of the player (you can't act on something across the map by hovering it).
    // Facing gates only the proximity pass: with WASD, turning toward a thing is how you
    // say which thing you mean, while a cursor already says it outright.
    int hover = -1;
    int prox = -1;
    float hoverDist = 0.0f;
    float proxDist = reach;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        const Candidate& c = items[static_cast<std::size_t>(i)];
        const float pd = distanceToBox(intent.px, intent.py, c);
        if (pd <= proxDist && inFront(intent, c))
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
        it.active = false; // cleared each frame; the resolved target is re-set below
        // A not-present interactable (a hidden encounter) is no candidate at all -- it can't
        // be targeted, so neither verb can fire on it. This is the one gate; observe and act
        // downstream never see a spot that isn't here.
        if (!it.present)
            continue;
        items.push_back({t.x, t.y, it.w, it.h});
        ents.push_back(e);
    }

    const Resolution r = resolve(items, intent, ctx.reach);
    if (r.index < 0)
        return {};

    const entt::entity targetEnt = ents[static_cast<std::size_t>(r.index)];
    Interactable& target = reg.get<Interactable>(targetEnt);
    target.active = true; // drives the highlight
    if (!r.fire)
        return {};

    Outcome out;
    out.fired = true;

    // Encounter spot: report which spot fired, but do NOT observe here -- the caller routes
    // it (a spot offering both observe + deeds shows a verb picker; one verb fires directly).
    // Keeps this layer generic: it resolves the target, the game decides the verb.
    if (!target.observe_id.empty())
    {
        out.observe_target = target.observe_id;
        out.act = intent.act; // running -> the caller skips the reading to the deed menu
        return out;
    }

    // Actionable-only: fire the direct action (no menu), deposit, then remove
    // the world item. Destroying the entity drops its Interactable + floor sprite with it, so
    // the highlight can't linger on empty space.
    switch (target.action)
    {
    case ActionKind::Pickup:
        out.items.push_back(inventory::ItemInstance{target.target});
        break;
    case ActionKind::Gather:
        if (const yields::Table* table = ctx.yields.find(target.target))
            out.items = yields::roll(*table, ctx.rng);
        break;
    case ActionKind::None:
        break; // a spot with neither observe nor action shouldn't be a candidate; no-op
    }
    for (const auto& item : out.items)
        inventory::add(ctx.satchel, ctx.items, item);
    if (target.action != ActionKind::None)
    {
        // Report what left the world before destroying it -- afterwards the entity (and
        // its identity) is gone, and the caller would have nothing to remember.
        out.removed_placement = target.placement_id;
        reg.destroy(targetEnt);
    }
    return out;
}

} // namespace interaction
