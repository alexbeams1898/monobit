#pragma once

namespace selva::interact
{

// What action the interactable represents. Drives the verb in the
// "[E] {verb} {label}" prompt. Add a new kind only when no existing
// verb fits the new behavior; otherwise prefer Custom.
enum class Kind
{
    Talk,    // NPCs
    Pickup,  // items in the world
    Examine, // lore objects (gravestones, signs)
    Open,    // chests, manually-opened doors
    Use,     // vestigia, riversamento sites, the Hand
    Custom,  // one-off scripted things; prompt uses raw label as-is
};

// Returns the verb shown in the prompt. Empty for Custom (label is
// then used as-is, e.g. "Read the inscription").
const char* kindVerb(Kind k);

} // namespace selva::interact
