#include "gameplay/Faction.h"

namespace selva::gameplay
{

Faction parseFaction(const std::string& s)
{
    if (s == "Player")
        return Faction::Player;
    if (s == "Allied")
        return Faction::Allied;
    if (s == "Neutral")
        return Faction::Neutral;
    // "Hostile", empty, or unknown -> Hostile. An archetype that omits
    // `faction` is an enemy.
    return Faction::Hostile;
}

const char* factionName(Faction f)
{
    switch (f)
    {
    case Faction::Player:
        return "Player";
    case Faction::Hostile:
        return "Hostile";
    case Faction::Neutral:
        return "Neutral";
    case Faction::Allied:
        return "Allied";
    }
    return "?";
}

const char* formName(Form f)
{
    switch (f)
    {
    case Form::UnjudgedSoul:
        return "UnjudgedSoul";
    case Form::DamnedSoul:
        return "DamnedSoul";
    case Form::Animal:
        return "Animal";
    case Form::HellMachinery:
        return "HellMachinery";
    case Form::Divine:
        return "Divine";
    }
    return "?";
}

Form parseForm(const std::string& s)
{
    if (s == "UnjudgedSoul")
        return Form::UnjudgedSoul;
    if (s == "Animal")
        return Form::Animal;
    if (s == "HellMachinery")
        return Form::HellMachinery;
    if (s == "Divine")
        return Form::Divine;
    // "DamnedSoul", empty, or unknown -> DamnedSoul. An archetype that
    // omits `form` is one of the damned.
    return Form::DamnedSoul;
}

} // namespace selva::gameplay
