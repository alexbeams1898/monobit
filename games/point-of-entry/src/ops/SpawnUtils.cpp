#include "ops/SpawnUtils.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"

namespace spawn
{

entt::entity box(EntityManager& em, float x, float y, float size, float r, float g, float b)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    // Set Transform fields by name rather than by position -- a mis-counted
    // brace silently lands in the wrong one (scale 0 draws a sprite zero
    // pixels wide, which looks exactly like the sprite not existing).
    Transform t{};
    t.x = x;
    t.y = y;
    reg.emplace<Transform>(e, t);
    reg.emplace<PreviousTransform>(e, PreviousTransform{x, y});
    Sprite spr{};
    spr.src_w = static_cast<int>(size);
    spr.src_h = static_cast<int>(size);
    spr.layer = 2;
    reg.emplace<Sprite>(e, spr);
    reg.emplace<SolidColor>(e, SolidColor{r, g, b});
    return e;
}

} // namespace spawn
