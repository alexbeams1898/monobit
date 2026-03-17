#pragma once

#include "ecs/EntityManager.h"

#include <string>

// ---------------------------------------------------------------------------
// ConfigLoader — reads a JSON entity definition and spawns it into the ECS.
//
// File format (see config/entities/guard.json for a full example):
//   {
//     "tag": "guard",
//     "components": {
//       "transform": { "x": 0.0, "y": 0.0, "rotation": 0.0, "scale": 1.0 },
//       "health":    { "current": 100, "max": 100 },
//       ...
//     }
//   }
//
// Missing fields fall back to component struct defaults.
// Unknown component names are warned about and skipped — forward-compatible
// so modders can add new component types without breaking older engine builds.
//
// JS analogy: this is JSON.parse() + a factory function that hydrates plain
// objects into live ECS entities.
// ---------------------------------------------------------------------------

class ConfigLoader
{
  public:
    // Load one entity definition from filePath and add it to em.
    // Returns entt::null if the file cannot be opened or is malformed.
    static entt::entity loadEntity(EntityManager& em, const std::string& filePath);

    // Parse config/balance/formulas.json and populate em.formulas.
    // Call once at startup before any system that reads em.formulas.
    // Returns true on success; false if the file cannot be opened or parsed
    // (em.formulas keeps its hardcoded defaults in that case).
    static bool loadFormulas(EntityManager& em, const std::string& filePath);
};
