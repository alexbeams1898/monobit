#include "systems/CollisionSystem.h"

#include "ecs/Components.h"

#include <cmath>
#include <vector>

void CollisionSystem::update(EntityManager& em)
{
    em.clearCollisionEvents();

    // Collect every entity that participates in collision.
    auto view = em.registry().view<Transform, Collider>();
    std::vector<entt::entity> entities(view.begin(), view.end());

    // Two resolution passes per frame.
    // Pass 1 resolves the primary overlaps; pass 2 catches any secondary overlaps
    // that were introduced when fixing a corner (e.g. pushing the player out of
    // wall A slightly changes the overlap with wall B).
    // Events are only emitted on the first pass to avoid duplicates.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (size_t i = 0; i < entities.size(); ++i)
        {
            for (size_t j = i + 1; j < entities.size(); ++j)
            {
                const entt::entity ea = entities[i];
                const entt::entity eb = entities[j];

                auto& ta = view.get<Transform>(ea);
                auto& ca = view.get<Collider>(ea);
                auto& tb = view.get<Transform>(eb);
                auto& cb = view.get<Collider>(eb);

                // AABB overlap test (center-based, matching how RenderSystem draws sprites).
                // overlapX/Y > 0 means the boxes are penetrating on that axis.
                const float halfWA = ca.width * 0.5f;
                const float halfHA = ca.height * 0.5f;
                const float halfWB = cb.width * 0.5f;
                const float halfHB = cb.height * 0.5f;

                const float dx = ta.x - tb.x;
                const float dy = ta.y - tb.y;

                const float overlapX = (halfWA + halfWB) - std::abs(dx);
                const float overlapY = (halfHA + halfHB) - std::abs(dy);

                if (overlapX <= 0.0f || overlapY <= 0.0f)
                    continue; // no overlap — next pair

                // Emit a collision event on the first pass only (avoid duplicates).
                if (pass == 0)
                    em.collisionEvents.push_back({ea, eb});

                // Only resolve solid pairs.
                if (!ca.isSolid || !cb.isSolid)
                    continue;

                const bool dynA = em.registry().all_of<Velocity>(ea);
                const bool dynB = em.registry().all_of<Velocity>(eb);

                if (!dynA && !dynB)
                    continue; // two immovable statics — just recorded the event

                // Choose the push axis based on the velocity of the moving entity.
                //
                // Velocity-based selection produces correct wall sliding: if the entity
                // is moving mostly to the right and hits a right wall, it gets pushed
                // back left while vertical movement is left untouched (slide up/down).
                //
                // For 45° diagonals (|dx| == |dy|) or two-dynamic pairs (player vs
                // guard) we fall back to the MTV (push along the smaller overlap axis).
                //
                // SEPARATION_BIAS: push a hair beyond the exact overlap so the entity
                // ends up just outside the wall, preventing re-penetration next frame.
                constexpr float SEPARATION_BIAS = 0.1f;

                float pushX = 0.0f;
                float pushY = 0.0f;

                // Helper: set pushX/pushY from velocity magnitudes or MTV fallback.
                auto selectAxis = [&](float absVx, float absVy)
                {
                    if (absVx > absVy)
                    {
                        // Moving primarily horizontally — resolve on X.
                        const float mag = overlapX + SEPARATION_BIAS;
                        pushX = (dx >= 0.0f) ? mag : -mag;
                    }
                    else if (absVy > absVx)
                    {
                        // Moving primarily vertically — resolve on Y.
                        const float mag = overlapY + SEPARATION_BIAS;
                        pushY = (dy >= 0.0f) ? mag : -mag;
                    }
                    else
                    {
                        // Equal magnitudes (45° or stationary) — use MTV.
                        if (overlapX < overlapY)
                        {
                            const float mag = overlapX + SEPARATION_BIAS;
                            pushX = (dx >= 0.0f) ? mag : -mag;
                        }
                        else
                        {
                            const float mag = overlapY + SEPARATION_BIAS;
                            pushY = (dy >= 0.0f) ? mag : -mag;
                        }
                    }
                };

                if (dynA && !dynB)
                {
                    const auto& vel = em.registry().get<Velocity>(ea);
                    selectAxis(std::abs(vel.dx), std::abs(vel.dy));
                }
                else if (!dynA && dynB)
                {
                    const auto& vel = em.registry().get<Velocity>(eb);
                    selectAxis(std::abs(vel.dx), std::abs(vel.dy));
                }
                else
                {
                    // Both dynamic (e.g. player vs guard) — MTV, no velocity bias.
                    selectAxis(0.0f, 0.0f);
                }

                if (dynA && !dynB)
                {
                    ta.x += pushX;
                    ta.y += pushY;
                }
                else if (!dynA && dynB)
                {
                    tb.x -= pushX;
                    tb.y -= pushY;
                }
                else
                {
                    // Both dynamic — split the correction evenly.
                    ta.x += pushX * 0.5f;
                    ta.y += pushY * 0.5f;
                    tb.x -= pushX * 0.5f;
                    tb.y -= pushY * 0.5f;
                }
            }
        }
    } // end pass
}
