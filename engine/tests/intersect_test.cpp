#include "geom/Intersect.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using geom::HitResult;

// Helpers.
static CollisionShape aabb(float x, float y, float w, float h)
{
    CollisionShape s;
    s.kind = ShapeKind::AABB;
    s.x = x;
    s.y = y;
    s.w = w;
    s.h = h;
    return s;
}
static CollisionShape circle(float x, float y, float r)
{
    CollisionShape s;
    s.kind = ShapeKind::Circle;
    s.x = x;
    s.y = y;
    s.r = r;
    return s;
}
static CollisionShape capsule(float x, float y, float x2, float y2, float r)
{
    CollisionShape s;
    s.kind = ShapeKind::Capsule;
    s.x = x;
    s.y = y;
    s.x2 = x2;
    s.y2 = y2;
    s.r = r;
    return s;
}

TEST_CASE("AABB-AABB overlap basics", "[geom]")
{
    auto a = aabb(0, 0, 10, 10);
    auto b = aabb(0, 0, 10, 10);
    REQUIRE(geom::overlaps(a, 0, 0, b, 0, 0));
    REQUIRE(geom::overlaps(a, 0, 0, b, 9, 0));
    REQUIRE_FALSE(geom::overlaps(a, 0, 0, b, 11, 0));
}

TEST_CASE("Circle-Circle overlap basics", "[geom]")
{
    auto a = circle(0, 0, 5);
    auto b = circle(0, 0, 5);
    REQUIRE(geom::overlaps(a, 0, 0, b, 0, 0));
    REQUIRE(geom::overlaps(a, 0, 0, b, 9, 0));
    REQUIRE_FALSE(geom::overlaps(a, 0, 0, b, 11, 0));
}

TEST_CASE("AABB-Circle overlap", "[geom]")
{
    auto a = aabb(0, 0, 10, 10);
    auto c = circle(0, 0, 2);
    // Circle center just outside AABB edge but within radius.
    REQUIRE(geom::overlaps(a, 0, 0, c, 6, 0));
    REQUIRE_FALSE(geom::overlaps(a, 0, 0, c, 8, 0));
}

TEST_CASE("Swept segment vs AABB", "[geom][swept]")
{
    auto target = aabb(0, 0, 20, 20);
    // Ray goes from (-50, 0) to (50, 0) through target at origin.
    HitResult r = geom::sweptSegment(-50, 0, 50, 0, target, 0, 0);
    REQUIRE(r.hit);
    // t such that contact_x == -10 (left edge of AABB): -50 + t*100 = -10 -> t = 0.4
    REQUIRE(r.t == Catch::Approx(0.4f).margin(0.01f));
}

TEST_CASE("Swept segment vs Circle tunneling prevention", "[geom][swept]")
{
    // Classic tunneling scenario: bullet traveling past a small target in one
    // frame. Discrete overlap at both endpoints would miss; swept must catch it.
    auto target = circle(0, 0, 5);
    // Ray passes through the circle center.
    HitResult r = geom::sweptSegment(-100, 0, 100, 0, target, 0, 0);
    REQUIRE(r.hit);
    REQUIRE(r.t < 1.0f);
    REQUIRE(r.t > 0.0f);
}

TEST_CASE("Swept segment miss", "[geom][swept]")
{
    auto target = circle(0, 0, 5);
    HitResult r = geom::sweptSegment(-100, 20, 100, 20, target, 0, 0);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("Capsule-Circle overlap", "[geom]")
{
    auto cap = capsule(-10, 0, 10, 0, 3);
    auto c = circle(0, 2, 1);
    REQUIRE(geom::overlaps(cap, 0, 0, c, 0, 0));
    REQUIRE_FALSE(geom::overlaps(cap, 0, 0, c, 0, 10));
}

TEST_CASE("Capsule-Capsule overlap", "[geom]")
{
    auto a = capsule(-10, 0, 10, 0, 2);
    auto b = capsule(0, -10, 0, 10, 2);
    // Cross at origin.
    REQUIRE(geom::overlaps(a, 0, 0, b, 0, 0));
    REQUIRE_FALSE(geom::overlaps(a, 0, 0, b, 20, 0));
}

TEST_CASE("Swept segment with radius vs small target", "[geom][swept]")
{
    // Simulate a bullet with radius sweeping at high speed past a small target.
    auto target = circle(0, 0, 2);
    // Zero-radius segment misses (passes 3px above).
    HitResult r1 = geom::sweptSegment(-100, 3, 100, 3, target, 0, 0);
    REQUIRE_FALSE(r1.hit);
    // Same segment with radius 2 should hit (swept circle touches target).
    HitResult r2 = geom::sweptSegment(-100, 3, 100, 3, 2.0f, target, 0, 0);
    REQUIRE(r2.hit);
}
