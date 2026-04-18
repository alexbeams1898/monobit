#pragma once

// CollisionShape -- tagged-union geometric primitive for damage geometry
// (hurtboxes and hitboxes). Kept intentionally small (16 bytes of geometry +
// tag) so vectors of shapes fit easily in cache lines.
//
// All coordinates are LOCAL-space offsets from an entity's Transform origin.
// Callers apply the entity's world position + facing rotation when testing.
//
// Primitives:
//   - AABB:    axis-aligned rectangle centered at (x, y), size (w, h)
//   - Circle:  center (x, y), radius r
//   - Capsule: line segment (x, y) -> (x2, y2), radius r (= swept circle)
//
// The struct layout is shared across all three so serialization can use a
// single schema with a "shape" discriminator field.

enum class ShapeKind : unsigned char
{
    AABB = 0,
    Circle,
    Capsule,
};

struct CollisionShape
{
    ShapeKind kind = ShapeKind::AABB;
    float x = 0.0f;  // AABB/Circle center; Capsule endpoint 1
    float y = 0.0f;
    float w = 0.0f;  // AABB width
    float h = 0.0f;  // AABB height
    float r = 0.0f;  // Circle/Capsule radius
    float x2 = 0.0f; // Capsule endpoint 2
    float y2 = 0.0f;
};
