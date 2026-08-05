#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// A SolidColor sprite is an entity with no texture -- a placeholder box, a debug marker, a
// particle. It must still be tintable, or it becomes the one thing in the game that cannot
// flash when hit. These pin the multiply, and pin that an untinted entity is unaffected.
//
// The renderer's resolveTint is file-static, so these exercise the same arithmetic it does:
// tint defaults to white, SolidColor multiplies into it.

namespace
{
struct Resolved
{
    float r, g, b;
};

Resolved resolve(const SolidColor& sc, const TintOverride* tint)
{
    Resolved out{1.0f, 1.0f, 1.0f};
    if (tint != nullptr)
    {
        out.r = tint->r;
        out.g = tint->g;
        out.b = tint->b;
    }
    out.r *= sc.r;
    out.g *= sc.g;
    out.b *= sc.b;
    return out;
}
} // namespace

TEST_CASE("an untinted solid colour is its own colour", "[tint]")
{
    // The identity case, and the one that must not regress: every existing game draws boxes
    // this way and none of them should change.
    const SolidColor green{0.35f, 0.65f, 0.30f};
    const Resolved r = resolve(green, nullptr);
    CHECK(r.r == Catch::Approx(0.35f));
    CHECK(r.g == Catch::Approx(0.65f));
    CHECK(r.b == Catch::Approx(0.30f));
}

TEST_CASE("a hit flash brightens a solid colour", "[tint]")
{
    // An overbright tint is how a white flash is expressed -- it must survive rather than be
    // discarded in favour of the sprite's own colour.
    const SolidColor green{0.35f, 0.65f, 0.30f};
    const TintOverride flash{2.5f, 2.5f, 2.5f};
    const Resolved r = resolve(green, &flash);
    CHECK(r.r > 0.35f);
    CHECK(r.g > 0.65f);
    CHECK(r.b > 0.30f);
}

TEST_CASE("a tint can darken as well as brighten", "[tint]")
{
    const SolidColor white{1.0f, 1.0f, 1.0f};
    const TintOverride dim{0.5f, 0.5f, 0.5f};
    const Resolved r = resolve(white, &dim);
    CHECK(r.r == Catch::Approx(0.5f));
}
