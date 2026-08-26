#pragma once

#include "Growth.h"
#include "UIRenderer.h"

#include <algorithm>
#include <cctype>
#include <string>

// Shared tint for an observation reading, used by the thought box (as a reading
// surfaces) and the notebook: the HUE comes from the faculty that pulled it, and
// the BRIGHTNESS from its difficulty (rarity) -- so color says "which part of you
// saw it" and brightness says "how rare". Baseline readings (no faculty /
// difficulty 0) stay the neutral off-white text color.
namespace reading_color
{

// alpha lets callers fade the line (thought box) without recomputing the hue.
inline Color forReading(const growth::GrowthState& growth, const std::string& faculty,
                        int difficulty, float alpha = 1.0f)
{
    if (faculty.empty() || difficulty <= 0)
        return {0.95f, 0.95f, 0.92f, alpha}; // neutral (baseline / conclusion)

    const growth::Rgb hue = growth::facultyColor(growth, faculty);
    // Difficulty 1..5 -> brightness ramp: common reads dim, legendary vivid.
    const float t = std::clamp(static_cast<float>(difficulty - 1) / 4.0f, 0.0f, 1.0f);
    const float bright = 0.55f + 0.45f * t; // 0.55 (dim) .. 1.0 (vivid)
    return {hue.r * bright, hue.g * bright, hue.b * bright, alpha};
}

// A faculty name capitalized for display ("perception" -> "Perception").
inline std::string facultyLabel(std::string s)
{
    if (!s.empty())
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

// The player-facing rarity name for a reading difficulty (1..5). Difficulty 0
// (a baseline reading / conclusion) has no rarity word.
inline const char* rarityWord(int difficulty)
{
    switch (difficulty)
    {
    case 1:
        return "Common";
    case 2:
        return "Uncommon";
    case 3:
        return "Notable";
    case 4:
        return "Rare";
    case 5:
        return "Legendary";
    default:
        return "";
    }
}

// The rarity word's own color -- a rarity ramp INDEPENDENT of the faculty
// hue, so rarity pops on its own (faculty = name/border color, rarity = this).
inline Color rarityColor(int difficulty, float alpha = 1.0f)
{
    switch (difficulty)
    {
    case 1:
        return {0.72f, 0.72f, 0.70f, alpha}; // Common -- grey
    case 2:
        return {0.55f, 0.80f, 0.50f, alpha}; // Uncommon -- green
    case 3:
        return {0.45f, 0.68f, 0.95f, alpha}; // Notable -- blue
    case 4:
        return {0.72f, 0.52f, 0.92f, alpha}; // Rare -- purple
    case 5:
        return {0.98f, 0.80f, 0.38f, alpha}; // Legendary -- gold
    default:
        return {0.95f, 0.95f, 0.92f, alpha};
    }
}

} // namespace reading_color
