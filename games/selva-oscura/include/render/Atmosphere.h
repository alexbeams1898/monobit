#pragma once

#include <glm/vec3.hpp>

namespace selva::render::atmosphere
{

// Centralized atmosphere state. Every shader that lights its
// fragments off the sun direction reads from these accessors -
// terrain, trees, scene (cube/floor), skeletal-mesh (player + enemies),
// sky, shadow pass. One source of truth so directions can't drift
// per-pass (a real bug we had: trees lit from elevation 0.7 while
// terrain lit from elevation 0.061 - shadows would have pointed
// opposite directions).
//
// Direction convention: world-space unit vector pointing FROM the
// scene TO the sun. The wood's sun is the low twilight beacon at the
// colle (dilettoso monte) per docs/design/wood.md - low elevation,
// roughly toward -Z where the colle plateau sits. Cool→warm gradient
// is wired into kAtmosphereGLSL.

// Direction TO the sun (already normalized). Stable for v1; will
// animate later if a day/night cycle ships.
glm::vec3 sunDirection();

// Sun radiance feeding the scattering integral + direct shading.
// Mystical-register tuning: red/green > blue so direct light reads
// warm against the cool-violet sky.
glm::vec3 sunIntensity();

// Tonemap exposure multiplier. 1.0 = engine default; bumped if the
// scene reads too dark or crushed.
float exposure();

} // namespace selva::render::atmosphere
