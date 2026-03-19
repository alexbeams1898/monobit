#include "systems/ParticleSystem.h"

#include "ecs/Components.h"

#include <cstdlib>
#include <tracy/Tracy.hpp>

void ParticleSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("ParticleSystem");
    auto& reg = em.registry();
    const auto fdt = static_cast<float>(dt);

    // Collect expired particles first to avoid invalidating the view.
    std::vector<entt::entity> expired;

    auto view = reg.view<Particle, Transform>();
    for (auto entity : view)
    {
        auto& p = view.get<Particle>(entity);
        p.age += fdt;

        if (p.age >= p.lifetime)
        {
            expired.push_back(entity);
            continue;
        }

        // Lerp scale from start to end over lifetime.
        float t = p.age / p.lifetime;
        auto& transform = view.get<Transform>(entity);
        transform.scale = p.start_scale + (p.end_scale - p.start_scale) * t;
    }

    for (auto entity : expired)
        reg.destroy(entity);
}

void ParticleSystem::spawnEmberBurst(EntityManager& em, float x, float y, int count)
{
    TracyMessageL("EmberBurst");
    auto& reg = em.registry();

    for (int i = 0; i < count; ++i)
    {
        auto entity = reg.create();

        // Random offset from center (-6 to +6 px).
        float ox = static_cast<float>(std::rand() % 13 - 6);
        float oy = static_cast<float>(std::rand() % 13 - 6);

        reg.emplace<Transform>(entity, x + ox, y + oy, 1.0f);
        reg.emplace<Tag>(entity, std::string("ember"));

        // Upward drift with slight horizontal wander.
        float vx = static_cast<float>(std::rand() % 21 - 10); // -10 to +10
        float vy = -15.0f - static_cast<float>(std::rand() % 21); // -15 to -35
        reg.emplace<Velocity>(entity, vx, vy);

        // Sprite: ember.png, 8x8, layer 3 (above characters).
        Sprite spr;
        spr.texture_path = "assets/sprites/ember.png";
        spr.src_w = 8;
        spr.src_h = 8;
        spr.layer = 3;
        reg.emplace<Sprite>(entity, spr);

        // Particle lifetime 0.6 - 1.0s.
        Particle p;
        p.lifetime = 0.6f + static_cast<float>(std::rand() % 5) * 0.1f;
        p.age = 0.0f;
        p.start_scale = 1.0f;
        p.end_scale = 0.3f;
        reg.emplace<Particle>(entity, p);
    }
}
