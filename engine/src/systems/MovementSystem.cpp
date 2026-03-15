#include "systems/MovementSystem.h"

#include "ecs/Components.h"

// Pixels per second at full input deflection.
// Will eventually come from a component or entity config value.
static constexpr float PLAYER_SPEED = 200.0f;

void MovementSystem::update(EntityManager& em, double dt)
{
    const float fdt = static_cast<float>(dt);

    // Pass 1: Input intent → Velocity.
    // Only entities with both Input and Velocity are affected here.
    for (auto [entity, input, vel] : em.registry().view<Input, Velocity>().each())
    {
        vel.dx = input.moveX * PLAYER_SPEED;
        vel.dy = input.moveY * PLAYER_SPEED;
    }

    // Pass 2: Velocity → Transform (position integration).
    // All entities with Velocity and Transform move, regardless of whether
    // they also have Input. AI systems can write to Velocity and get free
    // position updates from this pass.
    for (auto [entity, vel, transform] : em.registry().view<Velocity, Transform>().each())
    {
        transform.x += vel.dx * fdt;
        transform.y += vel.dy * fdt;
    }
}
