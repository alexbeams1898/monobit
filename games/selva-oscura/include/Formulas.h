#pragma once

#include "ecs/FormulaConfig.h"

#include <string>

namespace selva::formulas
{

// Single global instance. Loaded once at boot from config/balance/formulas.json
// and reloaded by the F1 panel's reload key. Every system that derives a
// pool size, regen rate, or stat-scaled stat reads from here -- the ONE
// source of truth for RPG math.
//
// Selva's Tunables struct is for camera/animation/combat-FEEL knobs (turn
// rates, blend speeds, hitbox sizes). FormulaConfig is for stat/pool MATH
// (END -> max HP, END -> max stamina, sprint stamina cost, etc.).
engine::ecs::FormulaConfig& current();

// JSON I/O. Path is relative to the working directory (typically build/bin/).
// Silent fallback to defaults on missing/malformed file.
bool loadFromFile(const std::string& path);

} // namespace selva::formulas
