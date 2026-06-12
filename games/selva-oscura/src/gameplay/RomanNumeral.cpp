#include "gameplay/RomanNumeral.h"

namespace selva::gameplay
{

namespace
{

struct RomanPair
{
    std::uint32_t value;
    const char* glyphs; // 1 or 2 chars
};

// Standard subtractive table for the 1-3999 range. Greedy peels off
// the largest pair whose value <= remainder.
constexpr RomanPair kPairs[] = {
    {1000u, "M"}, {900u, "CM"}, {500u, "D"}, {400u, "CD"}, {100u, "C"}, {90u, "XC"}, {50u, "L"},
    {40u, "XL"},  {10u, "X"},   {9u, "IX"},  {5u, "V"},    {4u, "IV"},  {1u, "I"},
};

// Below-4000 portion. Appends glyphs + bar-count flags to the output.
// `bar_count` selects whether emitted glyphs carry plain (0), single
// vinculum (1, ×1000), or double vinculum (2, ×1,000,000).
void encodeBelowFourThousand(std::uint32_t amount, std::uint8_t bar_count, RomanRendering& out)
{
    for (const auto& p : kPairs)
    {
        while (amount >= p.value)
        {
            for (const char* g = p.glyphs; *g != '\0'; ++g)
            {
                out.glyphs.push_back(*g);
                out.bars.push_back(bar_count);
            }
            amount -= p.value;
        }
        if (amount == 0u)
            break;
    }
}

} // namespace

RomanRendering encodeRoman(std::uint32_t amount)
{
    RomanRendering out;
    if (amount == 0u)
        return out;
    // Threshold split. Below 4000 the standard table (M = 1000)
    // handles thousands without a bar. At 4000+ the thousands portion
    // gets single-vinculum; at 4,000,000+ the millions portion gets
    // double-vinculum. The split is conditional, not unconditional,
    // because 1234 must render as plain MCCXXXIV (not bar-I + CCXXXIV).
    constexpr std::uint32_t kSingleVinculumThreshold = 4000u;
    constexpr std::uint32_t kDoubleVinculumThreshold = 4000000u;
    std::uint32_t remaining = amount;
    if (remaining >= kDoubleVinculumThreshold)
    {
        const std::uint32_t millions = remaining / 1000000u;
        remaining %= 1000000u;
        encodeBelowFourThousand(millions, /*bar_count=*/2u, out);
    }
    if (remaining >= kSingleVinculumThreshold)
    {
        const std::uint32_t thousands = remaining / 1000u;
        remaining %= 1000u;
        encodeBelowFourThousand(thousands, /*bar_count=*/1u, out);
    }
    if (remaining > 0u)
        encodeBelowFourThousand(remaining, /*bar_count=*/0u, out);
    return out;
}

} // namespace selva::gameplay
