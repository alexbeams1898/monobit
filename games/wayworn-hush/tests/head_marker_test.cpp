#include "HeadMarker.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{
// Read the single marker's Sprite alpha (the displayed opacity).
float markerAlpha(EntityManager& em)
{
    for (auto [e, hm, spr] : em.registry().view<HeadMarker, Sprite>().each())
        return spr.alpha;
    return -1.0f;
}

const Sprite& markerSprite(EntityManager& em)
{
    auto view = em.registry().view<HeadMarker, Sprite>();
    return em.registry().get<Sprite>(view.front());
}
} // namespace

TEST_CASE("The bubble spawns hidden (alpha 0) above nothing yet", "[head_marker]")
{
    EntityManager em;
    HeadMarkerConfig const cfg;
    head_marker::spawn(em, cfg);
    REQUIRE(markerAlpha(em) == Approx(0.0f));
    REQUIRE(markerSprite(em).layer == 3); // above the player (layer 2)
}

TEST_CASE("Shown -> the bubble fades up toward peak; hidden -> back toward 0", "[head_marker]")
{
    EntityManager em;
    HeadMarkerConfig const cfg;
    head_marker::spawn(em, cfg);

    // Show it and tick a while: alpha climbs but never exceeds peak (plus a small
    // breathing wobble bounded by pulse_amp).
    head_marker::set(em, true);
    for (int i = 0; i < 120; ++i)
        head_marker::update(em, cfg, 100.0f, 100.0f, 1.0f / 60.0f);
    const float lit = markerAlpha(em);
    REQUIRE(lit > 0.5f); // clearly visible
    REQUIRE(lit <= cfg.peak_alpha * (1.0f + cfg.pulse_amp) + 0.001f);

    // Hide it and tick: alpha decays back toward invisible.
    head_marker::set(em, false);
    for (int i = 0; i < 240; ++i)
        head_marker::update(em, cfg, 100.0f, 100.0f, 1.0f / 60.0f);
    REQUIRE(markerAlpha(em) < 0.02f);
}

TEST_CASE("The stem base sits at the head; the sprite floats above it", "[head_marker]")
{
    EntityManager em;
    HeadMarkerConfig const cfg;
    head_marker::spawn(em, cfg);

    head_marker::update(em, cfg, 200.0f, 150.0f, 1.0f / 60.0f);
    auto view = em.registry().view<HeadMarker, Transform>();
    const Transform& t = em.registry().get<Transform>(view.front());
    // Sprite is centered on the Transform; its BOTTOM edge (the stem base) must land
    // at the head point (py - head_offset), so the center is half a sprite up from it.
    const float headY = 150.0f - cfg.head_offset;
    REQUIRE(t.x == Approx(200.0f + cfg.side_offset)); // beside the head
    REQUIRE(t.y == Approx(headY - static_cast<float>(cfg.size) * 0.5f));

    // Move the player: the bubble follows.
    head_marker::update(em, cfg, 260.0f, 140.0f, 1.0f / 60.0f);
    const Transform& t2 = em.registry().get<Transform>(view.front());
    REQUIRE(t2.x == Approx(260.0f + cfg.side_offset));
    REQUIRE(t2.y == Approx((140.0f - cfg.head_offset) - static_cast<float>(cfg.size) * 0.5f));
}

TEST_CASE("The bubble carries no tint -- it stays its own white art", "[head_marker]")
{
    EntityManager em;
    HeadMarkerConfig const cfg;
    head_marker::spawn(em, cfg);
    // No TintOverride is attached (the bubble is white; faculty hue lives in the box).
    auto view = em.registry().view<HeadMarker, TintOverride>();
    REQUIRE(view.begin() == view.end());
}
