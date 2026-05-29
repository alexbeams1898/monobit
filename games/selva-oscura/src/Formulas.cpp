#include "Formulas.h"

#include "ecs/ConfigLoaders.h"

#include <cstdio>

namespace selva::formulas
{

static engine::ecs::FormulaConfig sCurrent;

engine::ecs::FormulaConfig& current()
{
    return sCurrent;
}

bool loadFromFile(const std::string& path)
{
    return engine::ecs::loadFormulaConfig(sCurrent, path);
}

} // namespace selva::formulas
