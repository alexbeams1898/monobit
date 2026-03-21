#include "systems/ParticleSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"

#include <cstdlib>
#include <iostream>
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

    // Continuous ember trickle on entities with unspent stat points.
    // One gentle particle every ~0.3s per entity reinforces the gold tint.
    static float s_stream_timer = 0.0f;
    s_stream_timer -= fdt;
    if (s_stream_timer <= 0.0f)
    {
        s_stream_timer = 0.3f;
        for (auto [entity, exp, transform] : reg.view<Experience, Transform>().each())
        {
            if (exp.stat_points > 0)
                spawnEmberTrickle(reg, transform.x, transform.y);
        }
    }
}

// Single gentle particle for the continuous stat-point indicator.
void ParticleSystem::spawnEmberTrickle(entt::registry& reg, float x, float y)
{
    auto entity = reg.create();

    float ox = static_cast<float>(std::rand() % 17 - 8); // -8 to +8
    float oy = static_cast<float>(std::rand() % 17 - 8);

    reg.emplace<Transform>(entity, x + ox, y + oy);
    reg.emplace<Tag>(entity, std::string("ember"));

    // Gentle upward float with minimal horizontal wander.
    float vx = static_cast<float>(std::rand() % 11 - 5);    // -5 to +5
    float vy = -8.0f - static_cast<float>(std::rand() % 8); // -8 to -15
    reg.emplace<Velocity>(entity, vx, vy);

    Sprite spr;
    spr.texture_path = "assets/sprites/ember.png";
    spr.src_w = 8;
    spr.src_h = 8;
    spr.layer = 3;
    reg.emplace<Sprite>(entity, spr);

    Particle p;
    p.lifetime = 1.0f + static_cast<float>(std::rand() % 6) * 0.1f; // 1.0 - 1.5s
    p.age = 0.0f;
    p.start_scale = 0.8f;
    p.end_scale = 0.2f;
    reg.emplace<Particle>(entity, p);
}

void ParticleSystem::spawnEmberBurst(EntityManager& em, float x, float y, int count)
{
    TracyMessageL("EmberBurst");
    std::cout << "[ParticleSystem] Ember burst at (" << x << ", " << y << ") x" << count << "\n";
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
        float vx = static_cast<float>(std::rand() % 21 - 10);     // -10 to +10
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
