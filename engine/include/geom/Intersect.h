#pragma once

#include "geom/Shapes.h"

namespace geom
{

// HitResult -- carries the intersection parameter t and contact point for
// swept queries. For discrete overlap tests, only `hit` is meaningful.
struct HitResult
{
    bool hit = false;
    float t = 0.0f;       // swept tests: 0..1 along the sweep segment
    float contact_x = 0.0f;
    float contact_y = 0.0f;
};

// ---------------------------------------------------------------------------
// Discrete overlap: does shape A overlap shape B when placed at world origins
// (ax, ay) and (bx, by)? Shape offsets are added to the world origin.
// ---------------------------------------------------------------------------
bool overlaps(const CollisionShape& a, float ax, float ay, const CollisionShape& b, float bx,
              float by);

// ---------------------------------------------------------------------------
// Swept: segment (x0, y0) -> (x1, y1) against a static shape at (sx, sy).
// Returns the earliest intersection t in [0, 1] if any.
// Treats the segment as a point (zero radius). For swept capsules, pass the
// capsule's radius via the overload below.
// ---------------------------------------------------------------------------
HitResult sweptSegment(float x0, float y0, float x1, float y1, const CollisionShape& shape,
                       float sx, float sy);

// Swept segment with radius: treats the moving point as a circle of radius r.
// Equivalent to sweeping a capsule whose endpoints are (x0, y0) and (x1, y1).
HitResult sweptSegment(float x0, float y0, float x1, float y1, float r,
                       const CollisionShape& shape, float sx, float sy);

} // namespace geom
