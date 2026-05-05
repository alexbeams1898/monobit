#include "geom/Intersect.h"

#include <algorithm>
#include <cmath>

namespace geom
{

// --- Primitive helpers ---

static float clamp01(float t) { return std::max(0.0f, std::min(1.0f, t)); }

// Squared distance from point (px, py) to segment (x0, y0) -> (x1, y1).
// Returns the clamped parameter t in [0, 1] via out-param.
static float distSqPointSegment(float px, float py, float x0, float y0, float x1, float y1,
                                float& out_t)
{
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len_sq = dx * dx + dy * dy;
    if (len_sq <= 1e-8f)
    {
        out_t = 0.0f;
        const float qx = px - x0;
        const float qy = py - y0;
        return qx * qx + qy * qy;
    }
    const float t = clamp01(((px - x0) * dx + (py - y0) * dy) / len_sq);
    out_t = t;
    const float cx = x0 + t * dx;
    const float cy = y0 + t * dy;
    const float qx = px - cx;
    const float qy = py - cy;
    return qx * qx + qy * qy;
}

// Squared distance between two segments (a0-a1) and (b0-b1).
// Out params hold clamped parameters on each segment.
static float distSqSegmentSegment(float ax0, float ay0, float ax1, float ay1, float bx0, float by0,
                                  float bx1, float by1, float& out_s, float& out_t)
{
    // Based on Christer Ericson, Real-Time Collision Detection §5.1.9.
    const float dx1 = ax1 - ax0;
    const float dy1 = ay1 - ay0;
    const float dx2 = bx1 - bx0;
    const float dy2 = by1 - by0;
    const float rx = ax0 - bx0;
    const float ry = ay0 - by0;

    const float a = dx1 * dx1 + dy1 * dy1;
    const float e = dx2 * dx2 + dy2 * dy2;
    const float f = dx2 * rx + dy2 * ry;

    float s = 0.0f;
    float t = 0.0f;

    if (a <= 1e-8f && e <= 1e-8f)
    {
        out_s = 0.0f;
        out_t = 0.0f;
        return rx * rx + ry * ry;
    }
    if (a <= 1e-8f)
    {
        t = clamp01(f / e);
    }
    else
    {
        const float c = dx1 * rx + dy1 * ry;
        if (e <= 1e-8f)
        {
            s = clamp01(-c / a);
        }
        else
        {
            const float b = dx1 * dx2 + dy1 * dy2;
            const float denom = a * e - b * b;
            if (denom != 0.0f)
                s = clamp01((b * f - c * e) / denom);
            t = (b * s + f) / e;
            if (t < 0.0f)
            {
                t = 0.0f;
                s = clamp01(-c / a);
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = clamp01((b - c) / a);
            }
        }
    }

    out_s = s;
    out_t = t;
    const float cx1 = ax0 + s * dx1;
    const float cy1 = ay0 + s * dy1;
    const float cx2 = bx0 + t * dx2;
    const float cy2 = by0 + t * dy2;
    const float qx = cx1 - cx2;
    const float qy = cy1 - cy2;
    return qx * qx + qy * qy;
}

// Closest point on an AABB (center cx, cy, half-extents hx, hy) to a point (px, py).
static void closestPointOnAABB(float px, float py, float cx, float cy, float hx, float hy,
                               float& out_x, float& out_y)
{
    out_x = std::max(cx - hx, std::min(px, cx + hx));
    out_y = std::max(cy - hy, std::min(py, cy + hy));
}

// --- Discrete pair tests ---

static bool aabbAABB(const CollisionShape& a, float ax, float ay, const CollisionShape& b,
                     float bx, float by)
{
    const float acx = ax + a.x;
    const float acy = ay + a.y;
    const float bcx = bx + b.x;
    const float bcy = by + b.y;
    const float hxa = a.w * 0.5f;
    const float hya = a.h * 0.5f;
    const float hxb = b.w * 0.5f;
    const float hyb = b.h * 0.5f;
    return std::abs(acx - bcx) <= (hxa + hxb) && std::abs(acy - bcy) <= (hya + hyb);
}

static bool aabbCircle(const CollisionShape& a, float ax, float ay, const CollisionShape& c,
                       float cx, float cy)
{
    const float acx = ax + a.x;
    const float acy = ay + a.y;
    const float ccx = cx + c.x;
    const float ccy = cy + c.y;
    float qx = 0.0f;
    float qy = 0.0f;
    closestPointOnAABB(ccx, ccy, acx, acy, a.w * 0.5f, a.h * 0.5f, qx, qy);
    const float dx = qx - ccx;
    const float dy = qy - ccy;
    return dx * dx + dy * dy <= c.r * c.r;
}

static bool aabbCapsule(const CollisionShape& a, float ax, float ay, const CollisionShape& c,
                        float cx, float cy)
{
    // Capsule vs AABB: find segment point closest to AABB center, test against
    // AABB expanded by capsule radius.
    const float acx = ax + a.x;
    const float acy = ay + a.y;
    const float hx = a.w * 0.5f + c.r;
    const float hy = a.h * 0.5f + c.r;

    // Clip the capsule segment against the expanded AABB; if any part lies
    // inside, there's an overlap. Simpler: just test the closest segment point
    // to the AABB center against the un-expanded AABB + radius.
    const float x0 = cx + c.x;
    const float y0 = cy + c.y;
    const float x1 = cx + c.x2;
    const float y1 = cy + c.y2;

    // Closest point on segment to AABB center.
    float t = 0.0f;
    distSqPointSegment(acx, acy, x0, y0, x1, y1, t);
    const float sx = x0 + t * (x1 - x0);
    const float sy = y0 + t * (y1 - y0);

    // Now test the point (sx, sy) against the expanded AABB (center acx/acy,
    // half-extents hx, hy).
    const float dx = std::abs(sx - acx);
    const float dy = std::abs(sy - acy);
    if (dx > hx || dy > hy)
        return false;

    // Already inside the expanded AABB -- the rounded corners need a check:
    // if both dx > w/2 AND dy > h/2, test distance to corner against radius.
    const float half_w = a.w * 0.5f;
    const float half_h = a.h * 0.5f;
    if (dx <= half_w || dy <= half_h)
        return true;
    const float cdx = dx - half_w;
    const float cdy = dy - half_h;
    return cdx * cdx + cdy * cdy <= c.r * c.r;
}

static bool circleCircle(const CollisionShape& a, float ax, float ay, const CollisionShape& b,
                         float bx, float by)
{
    const float dx = (ax + a.x) - (bx + b.x);
    const float dy = (ay + a.y) - (by + b.y);
    const float r = a.r + b.r;
    return dx * dx + dy * dy <= r * r;
}

static bool circleCapsule(const CollisionShape& c, float cx, float cy, const CollisionShape& cap,
                          float capx, float capy)
{
    // Distance from circle center to capsule segment, compare to sum of radii.
    const float ccx = cx + c.x;
    const float ccy = cy + c.y;
    const float x0 = capx + cap.x;
    const float y0 = capy + cap.y;
    const float x1 = capx + cap.x2;
    const float y1 = capy + cap.y2;
    float t = 0.0f;
    const float d_sq = distSqPointSegment(ccx, ccy, x0, y0, x1, y1, t);
    const float r = c.r + cap.r;
    return d_sq <= r * r;
}

static bool capsuleCapsule(const CollisionShape& a, float ax, float ay, const CollisionShape& b,
                           float bx, float by)
{
    float s = 0.0f;
    float t = 0.0f;
    const float d_sq =
        distSqSegmentSegment(ax + a.x, ay + a.y, ax + a.x2, ay + a.y2, bx + b.x, by + b.y,
                             bx + b.x2, by + b.y2, s, t);
    const float r = a.r + b.r;
    return d_sq <= r * r;
}

bool overlaps(const CollisionShape& a, float ax, float ay, const CollisionShape& b, float bx,
              float by)
{
    // Normalize so the lower enum value is always first.
    if (static_cast<int>(b.kind) < static_cast<int>(a.kind))
        return overlaps(b, bx, by, a, ax, ay);

    switch (a.kind)
    {
    case ShapeKind::AABB:
        switch (b.kind)
        {
        case ShapeKind::AABB:
            return aabbAABB(a, ax, ay, b, bx, by);
        case ShapeKind::Circle:
            return aabbCircle(a, ax, ay, b, bx, by);
        case ShapeKind::Capsule:
            return aabbCapsule(a, ax, ay, b, bx, by);
        }
        break;
    case ShapeKind::Circle:
        switch (b.kind)
        {
        case ShapeKind::Circle:
            return circleCircle(a, ax, ay, b, bx, by);
        case ShapeKind::Capsule:
            return circleCapsule(a, ax, ay, b, bx, by);
        default:
            break;
        }
        break;
    case ShapeKind::Capsule:
        if (b.kind == ShapeKind::Capsule)
            return capsuleCapsule(a, ax, ay, b, bx, by);
        break;
    }
    return false;
}

// --- Swept tests ---

// Ray vs AABB (slab method). Ray (x0, y0) -> (x1, y1), AABB center (cx, cy),
// half-extents (hx, hy). Returns earliest t in [0, 1] if it intersects.
static HitResult rayAABB(float x0, float y0, float x1, float y1, float cx, float cy, float hx,
                         float hy)
{
    HitResult r;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    float t_near = 0.0f;
    float t_far = 1.0f;

    for (int axis = 0; axis < 2; ++axis)
    {
        const float origin = (axis == 0) ? x0 : y0;
        const float dir = (axis == 0) ? dx : dy;
        const float center = (axis == 0) ? cx : cy;
        const float half = (axis == 0) ? hx : hy;
        const float amin = center - half;
        const float amax = center + half;

        if (std::abs(dir) < 1e-8f)
        {
            if (origin < amin || origin > amax)
                return r;
            continue;
        }
        float t0 = (amin - origin) / dir;
        float t1 = (amax - origin) / dir;
        if (t0 > t1)
            std::swap(t0, t1);
        t_near = std::max(t_near, t0);
        t_far = std::min(t_far, t1);
        if (t_near > t_far)
            return r;
    }

    r.hit = true;
    r.t = t_near;
    r.contact_x = x0 + t_near * dx;
    r.contact_y = y0 + t_near * dy;
    return r;
}

// Ray vs circle (quadratic). Ray (x0,y0) -> (x1,y1), circle (cx, cy, r).
static HitResult rayCircle(float x0, float y0, float x1, float y1, float cx, float cy, float r)
{
    HitResult res;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float fx = x0 - cx;
    const float fy = y0 - cy;

    const float a = dx * dx + dy * dy;
    const float b = 2.0f * (fx * dx + fy * dy);
    const float c = fx * fx + fy * fy - r * r;

    if (a < 1e-8f)
    {
        // Zero-length segment: overlap test only.
        if (c <= 0.0f)
        {
            res.hit = true;
            res.contact_x = x0;
            res.contact_y = y0;
        }
        return res;
    }

    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f)
        return res;
    const float sqrt_disc = std::sqrt(disc);
    const float t0 = (-b - sqrt_disc) / (2.0f * a);
    const float t1 = (-b + sqrt_disc) / (2.0f * a);

    // If t0 is negative but t1 is positive, the ray starts inside the circle.
    float t = t0;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        return res;
    if (t1 < 0.0f)
        return res;

    res.hit = true;
    res.t = t;
    res.contact_x = x0 + t * dx;
    res.contact_y = y0 + t * dy;
    return res;
}

// Ray vs capsule: minimum parameter t such that segment-point at t is within
// capsule.r of the capsule segment. Approximated by sampling endpoints (circles)
// and the capsule's "tube" (rectangle in 2D), taking earliest hit.
static HitResult rayCapsule(float x0, float y0, float x1, float y1, const CollisionShape& cap,
                            float capx, float capy)
{
    const float cx0 = capx + cap.x;
    const float cy0 = capy + cap.y;
    const float cx1 = capx + cap.x2;
    const float cy1 = capy + cap.y2;

    HitResult best;
    best.t = 2.0f; // sentinel > 1

    // End-cap circles.
    HitResult h0 = rayCircle(x0, y0, x1, y1, cx0, cy0, cap.r);
    if (h0.hit && h0.t < best.t)
        best = h0;
    HitResult h1 = rayCircle(x0, y0, x1, y1, cx1, cy1, cap.r);
    if (h1.hit && h1.t < best.t)
        best = h1;

    // "Tube" rectangle: the region between the two end caps, extended by r
    // perpendicular to the capsule axis. Skip if capsule has zero length.
    const float cdx = cx1 - cx0;
    const float cdy = cy1 - cy0;
    const float clen_sq = cdx * cdx + cdy * cdy;
    if (clen_sq > 1e-8f)
    {
        // Test ray vs segment inflated by r perpendicular, i.e. capsule body
        // as an oriented strip. Easiest: solve distSqSegmentSegment(ray,
        // capsule) for the earliest t along the ray where the closest distance
        // first equals r. Fall back to linearly scanning ray samples against
        // the swept circle distance.
        // For simplicity and adequate accuracy, perform a coarse-to-fine scan.
        // Good enough for gameplay at our scales; if precision becomes an
        // issue, replace with analytical segment-cylinder intersection.
        const int kSamples = 16;
        const float rdx = x1 - x0;
        const float rdy = y1 - y0;
        for (int i = 0; i <= kSamples; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kSamples);
            const float px = x0 + t * rdx;
            const float py = y0 + t * rdy;
            float cs = 0.0f;
            const float d_sq = distSqPointSegment(px, py, cx0, cy0, cx1, cy1, cs);
            if (d_sq <= cap.r * cap.r)
            {
                if (t < best.t)
                {
                    best.hit = true;
                    best.t = t;
                    best.contact_x = px;
                    best.contact_y = py;
                }
                break;
            }
        }
    }

    if (!best.hit)
        return {};
    return best;
}

HitResult sweptSegment(float x0, float y0, float x1, float y1, const CollisionShape& shape,
                       float sx, float sy)
{
    switch (shape.kind)
    {
    case ShapeKind::AABB:
        return rayAABB(x0, y0, x1, y1, sx + shape.x, sy + shape.y, shape.w * 0.5f,
                       shape.h * 0.5f);
    case ShapeKind::Circle:
        return rayCircle(x0, y0, x1, y1, sx + shape.x, sy + shape.y, shape.r);
    case ShapeKind::Capsule:
        return rayCapsule(x0, y0, x1, y1, shape, sx, sy);
    }
    return {};
}

HitResult sweptSegment(float x0, float y0, float x1, float y1, float r,
                       const CollisionShape& shape, float sx, float sy)
{
    // Minkowski sum: expand the target shape by r, then do zero-radius segment test.
    CollisionShape expanded = shape;
    switch (shape.kind)
    {
    case ShapeKind::AABB:
        // Expanded AABB has rounded corners (a rounded rect). Approximate: if
        // the ray's closest approach passes through a corner, the simple AABB
        // expansion overshoots diagonally. For our gameplay usage (bullets are
        // small relative to hurtboxes) the approximation is fine.
        expanded.w += 2.0f * r;
        expanded.h += 2.0f * r;
        return rayAABB(x0, y0, x1, y1, sx + expanded.x, sy + expanded.y, expanded.w * 0.5f,
                       expanded.h * 0.5f);
    case ShapeKind::Circle:
        expanded.r += r;
        return rayCircle(x0, y0, x1, y1, sx + expanded.x, sy + expanded.y, expanded.r);
    case ShapeKind::Capsule:
        expanded.r += r;
        return rayCapsule(x0, y0, x1, y1, expanded, sx, sy);
    }
    return {};
}

} // namespace geom
