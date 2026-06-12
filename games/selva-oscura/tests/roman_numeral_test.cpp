// Roman numeral encoder (subtractive + single-vinculum + double-
// vinculum). Drives the HUD vessel counter rendering.
//
// Single vinculum (×1000) kicks in at 4000+; double vinculum
// (×1,000,000) kicks in at 4,000,000+. The cosmological cap
// (999,999,999) renders cleanly as three groups of CMXCIX with
// 2/1/0 bars respectively.
//
// Pure-compute. No engine globals.

#include "gameplay/RomanNumeral.h"

#include <catch2/catch_test_macros.hpp>

namespace
{

bool allBarsAre(const std::vector<std::uint8_t>& bars, std::uint8_t expected)
{
    for (const std::uint8_t b : bars)
        if (b != expected)
            return false;
    return true;
}

} // namespace

TEST_CASE("encode 0 yields empty rendering", "[roman-numeral]")
{
    const auto r = selva::gameplay::encodeRoman(0u);
    REQUIRE(r.glyphs.empty());
    REQUIRE(r.bars.empty());
}

TEST_CASE("encode small numbers uses subtractive form", "[roman-numeral][subtractive]")
{
    REQUIRE(selva::gameplay::encodeRoman(1u).glyphs == "I");
    REQUIRE(selva::gameplay::encodeRoman(4u).glyphs == "IV");
    REQUIRE(selva::gameplay::encodeRoman(9u).glyphs == "IX");
    REQUIRE(selva::gameplay::encodeRoman(49u).glyphs == "XLIX");
    REQUIRE(selva::gameplay::encodeRoman(90u).glyphs == "XC");
    REQUIRE(selva::gameplay::encodeRoman(400u).glyphs == "CD");
    REQUIRE(selva::gameplay::encodeRoman(900u).glyphs == "CM");
    REQUIRE(selva::gameplay::encodeRoman(999u).glyphs == "CMXCIX");
}

TEST_CASE("encode 1234 = MCCXXXIV (no vinculum below threshold)", "[roman-numeral]")
{
    // Below 4000 the standard M-in-table handles thousands; no
    // vinculum required. 1234 decomposes additively in subtractive
    // form: M + CC + (3 tens) + IV, all plain.
    const auto r = selva::gameplay::encodeRoman(1234u);
    REQUIRE(r.glyphs == "MCCXXXIV");
    REQUIRE(r.bars.size() == 8);
    REQUIRE(allBarsAre(r.bars, 0));
}

TEST_CASE("encode 3999 = MMMCMXCIX (no vinculum, all plain)", "[roman-numeral][edge]")
{
    // 3999 is the largest fully-plain Roman number. 4000 triggers
    // single vinculum.
    const auto r = selva::gameplay::encodeRoman(3999u);
    REQUIRE(r.glyphs == "MMMCMXCIX");
    REQUIRE(r.bars.size() == 9);
    REQUIRE(allBarsAre(r.bars, 0));
}

TEST_CASE("encode 4000 yields single-vinculum IV", "[roman-numeral][vinculum]")
{
    const auto r = selva::gameplay::encodeRoman(4000u);
    REQUIRE(r.glyphs == "IV");
    REQUIRE(r.bars.size() == 2);
    REQUIRE(r.bars[0] == 1u);
    REQUIRE(r.bars[1] == 1u);
}

TEST_CASE("encode 5000 yields single-vinculum V", "[roman-numeral][vinculum]")
{
    const auto r = selva::gameplay::encodeRoman(5000u);
    REQUIRE(r.glyphs == "V");
    REQUIRE(r.bars.size() == 1);
    REQUIRE(r.bars[0] == 1u);
}

TEST_CASE("encode 1000000 yields single-vinculum M", "[roman-numeral][vinculum]")
{
    // 1,000,000 < 4,000,000 so it uses single vinculum: M with one
    // bar = M̄ = 1000 * 1000.
    const auto r = selva::gameplay::encodeRoman(1000000u);
    REQUIRE(r.glyphs == "M");
    REQUIRE(r.bars.size() == 1);
    REQUIRE(r.bars[0] == 1u);
}

TEST_CASE("encode 3999999 yields max single-vinculum form", "[roman-numeral][vinculum][edge]")
{
    // 3999999 = 3999 thousands + 999 ones
    //         = MMMCMXCIX (single bar) + CMXCIX (plain)
    const auto r = selva::gameplay::encodeRoman(3999999u);
    REQUIRE(r.glyphs == "MMMCMXCIXCMXCIX");
    REQUIRE(r.bars.size() == 15);
    for (std::size_t i = 0; i < 9; ++i)
        REQUIRE(r.bars[i] == 1u);
    for (std::size_t i = 9; i < 15; ++i)
        REQUIRE(r.bars[i] == 0u);
}

TEST_CASE("encode 4000000 yields double-vinculum IV", "[roman-numeral][double-vinculum]")
{
    // 4,000,000 = 4 millions. Renders as IV with double bars.
    const auto r = selva::gameplay::encodeRoman(4000000u);
    REQUIRE(r.glyphs == "IV");
    REQUIRE(r.bars.size() == 2);
    REQUIRE(r.bars[0] == 2u);
    REQUIRE(r.bars[1] == 2u);
}

TEST_CASE("encode 5000000 yields double-vinculum V", "[roman-numeral][double-vinculum]")
{
    const auto r = selva::gameplay::encodeRoman(5000000u);
    REQUIRE(r.glyphs == "V");
    REQUIRE(r.bars[0] == 2u);
}

TEST_CASE("encode 999999999 yields three groups (double / single / plain)",
          "[roman-numeral][double-vinculum][edge]")
{
    // 999,999,999 = 999 millions + 999 thousands + 999 ones
    //             = CMXCIX (double) + CMXCIX (single) + CMXCIX (plain)
    // Total: 18 glyphs, bars [2]*6 + [1]*6 + [0]*6.
    const auto r = selva::gameplay::encodeRoman(999999999u);
    REQUIRE(r.glyphs == "CMXCIXCMXCIXCMXCIX");
    REQUIRE(r.bars.size() == 18);
    for (std::size_t i = 0; i < 6; ++i)
        REQUIRE(r.bars[i] == 2u);
    for (std::size_t i = 6; i < 12; ++i)
        REQUIRE(r.bars[i] == 1u);
    for (std::size_t i = 12; i < 18; ++i)
        REQUIRE(r.bars[i] == 0u);
}

TEST_CASE("encode mid-double-vinculum range numbers", "[roman-numeral][double-vinculum]")
{
    // 4,000,001 = 4 millions + 0 thousands + 1 one
    //           = IV (double bar) + I (plain)
    const auto r = selva::gameplay::encodeRoman(4000001u);
    REQUIRE(r.glyphs == "IVI");
    REQUIRE(r.bars.size() == 3);
    REQUIRE(r.bars[0] == 2u);
    REQUIRE(r.bars[1] == 2u);
    REQUIRE(r.bars[2] == 0u);
}

TEST_CASE("encode 12345678 mid-range double-vinculum", "[roman-numeral][double-vinculum]")
{
    // 12,345,678 = 12 millions + 345 thousands + 678 ones
    //            = XII (double) + CCCXLV (single) + DCLXXVIII (plain)
    const auto r = selva::gameplay::encodeRoman(12345678u);
    REQUIRE(r.glyphs == "XIICCCXLVDCLXXVIII");
    REQUIRE(r.bars.size() == 18);
    for (std::size_t i = 0; i < 3; ++i)
        REQUIRE(r.bars[i] == 2u); // XII
    for (std::size_t i = 3; i < 9; ++i)
        REQUIRE(r.bars[i] == 1u); // CCCXLV
    for (std::size_t i = 9; i < 18; ++i)
        REQUIRE(r.bars[i] == 0u); // DCLXXVIII
}

TEST_CASE("encode 1234567 within single-vinculum range", "[roman-numeral][vinculum]")
{
    // 1,234,567 < 4,000,000 so it's single vinculum only.
    // = 1234 thousands + 567 ones
    // = MCCXXXIV (single) + DLXVII (plain)
    const auto r = selva::gameplay::encodeRoman(1234567u);
    REQUIRE(r.glyphs == "MCCXXXIVDLXVII");
    REQUIRE(r.bars.size() == 14);
    for (std::size_t i = 0; i < 8; ++i)
        REQUIRE(r.bars[i] == 1u);
    for (std::size_t i = 8; i < 14; ++i)
        REQUIRE(r.bars[i] == 0u);
}

TEST_CASE("bars vector length always matches glyph string length", "[roman-numeral][invariant]")
{
    for (const std::uint32_t amount :
         {0u, 1u, 4u, 9u, 49u, 100u, 999u, 1000u, 3999u, 4000u, 99999u, 999999u, 3999999u, 4000000u,
          4000001u, 12345678u, 99999999u, 999999999u})
    {
        const auto r = selva::gameplay::encodeRoman(amount);
        REQUIRE(r.glyphs.size() == r.bars.size());
    }
}
