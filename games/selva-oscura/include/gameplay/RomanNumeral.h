#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Roman numeral encoder for the HUD vessel counter.
//
// Encodes using subtractive form (4 = IV, 9 = IX, etc.) per standard
// rules. Supports single AND double vinculum so the cosmological cap
// (999,999,999 = 9^9 = "all of Hell completed", per
//) renders cleanly. A
// single bar over a glyph means ×1000; a double bar means ×1,000,000.
//
// The output is two parallel arrays of the same length:
//   - glyphs: ASCII Roman characters (I V X L C D M)
//   - bars:   per-glyph bar count (0 = plain, 1 = single, 2 = double)
//
// The renderer draws the bars manually above the marked glyphs since
// ImGui's default font lacks combining diacritics. Double bar = two
// parallel horizontal lines stacked.
//
// Per setting.md *Hell's accounting cap*: the vessel hard-caps at
// 999,999,999. Both storage and display ceilings agree -- the
// renderer can express every value the vessel can hold. No overflow
// indicator needed because no overflow is possible.

namespace selva::gameplay
{

struct RomanRendering
{
    std::string glyphs;             // ASCII Roman chars
    std::vector<std::uint8_t> bars; // 0/1/2 per glyph; size == glyphs.size()
};

// Encode amount as Roman numerals with subtractive forms, single
// vinculum for thousands, and double vinculum for millions.
// Examples:
//   0           -> {"",       []}
//   1           -> {"I",      [0]}
//   4           -> {"IV",     [0, 0]}
//   1234        -> {"MCCXXXIV", [0]*8}
//   4000        -> {"IV",     [1, 1]}      // overbarred IV = 4000
//   1000000     -> {"M",      [2]}         // double-overbarred M = 1M
//   1234567     -> "MCCXXXIVDLXVII" with first 8 single-barred, last 6 plain
//   999999999   -> "CMXCIX CMXCIX CMXCIX" (concatenated) with bars [2]*6 + [1]*6 + [0]*6
RomanRendering encodeRoman(std::uint32_t amount);

} // namespace selva::gameplay
