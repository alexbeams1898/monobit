#include "TextField.h"

#include <catch2/catch_test_macros.hpp>

// The field is pure logic -- the drawing belongs to whatever screen owns it. These pin
// the rules a typist would notice breaking.

using text_field::Input;
using text_field::State;

namespace
{
Input typed(const char* s)
{
    Input in;
    in.typed = s;
    return in;
}

// A frame long enough that a held key's repeat delay has certainly elapsed.
constexpr double kLongFrame = 1.0;
} // namespace

TEST_CASE("begin seeds the text and puts the cursor at the end", "[textfield]")
{
    State s;
    text_field::begin(s, "Ash");
    REQUIRE(s.text == "Ash");
    REQUIRE(s.cursor == 3);
}

TEST_CASE("begin truncates an over-long seed to the cap", "[textfield]")
{
    State s;
    text_field::begin(s, std::string(text_field::kMaxLength + 10, 'x'));
    REQUIRE(static_cast<int>(s.text.size()) == text_field::kMaxLength);
    REQUIRE(s.cursor == text_field::kMaxLength);
}

TEST_CASE("typing inserts at the cursor and advances it", "[textfield]")
{
    State s;
    text_field::begin(s);
    text_field::update(s, typed("As"), 0.016);
    text_field::update(s, typed("h"), 0.016);
    REQUIRE(s.text == "Ash");
    REQUIRE(s.cursor == 3);
}

TEST_CASE("typing mid-line inserts rather than overwrites", "[textfield]")
{
    State s;
    text_field::begin(s, "Ah");
    s.cursor = 1;
    text_field::update(s, typed("s"), 0.016);
    REQUIRE(s.text == "Ash");
    REQUIRE(s.cursor == 2);
}

TEST_CASE("the field will not grow past the cap", "[textfield]")
{
    State s;
    text_field::begin(s);
    text_field::update(s, typed(std::string(text_field::kMaxLength + 5, 'x').c_str()), 0.016);
    REQUIRE(static_cast<int>(s.text.size()) == text_field::kMaxLength);

    text_field::update(s, typed("more"), 0.016); // and stays capped
    REQUIRE(static_cast<int>(s.text.size()) == text_field::kMaxLength);
}

TEST_CASE("control characters never enter the text", "[textfield]")
{
    State s;
    text_field::begin(s);
    text_field::update(s, typed("A\tB\nC"), 0.016);
    REQUIRE(s.text == "ABC");
}

TEST_CASE("backspace removes before the cursor; the press acts immediately", "[textfield]")
{
    State s;
    text_field::begin(s, "Ash");
    Input in;
    in.backspace = true;
    text_field::update(s, in, 0.016);
    REQUIRE(s.text == "As");
    REQUIRE(s.cursor == 2);
}

TEST_CASE("backspace at the start does nothing", "[textfield]")
{
    State s;
    text_field::begin(s, "Ash");
    s.cursor = 0;
    Input in;
    in.backspace = true;
    text_field::update(s, in, 0.016);
    REQUIRE(s.text == "Ash");
    REQUIRE(s.cursor == 0);
}

TEST_CASE("delete removes after the cursor", "[textfield]")
{
    State s;
    text_field::begin(s, "Ash");
    s.cursor = 0;
    Input in;
    in.del = true;
    text_field::update(s, in, 0.016);
    REQUIRE(s.text == "sh");
    REQUIRE(s.cursor == 0);
}

TEST_CASE("delete at the end does nothing", "[textfield]")
{
    State s;
    text_field::begin(s, "Ash"); // cursor at end
    Input in;
    in.del = true;
    text_field::update(s, in, 0.016);
    REQUIRE(s.text == "Ash");
}

TEST_CASE("a held backspace pauses before repeating -- not one per frame", "[textfield]")
{
    // Without the delay, holding backspace for three short frames would eat three
    // characters instantly, which reads as a broken field.
    State s;
    text_field::begin(s, "Ashen");
    Input in;
    in.backspace = true;

    text_field::update(s, in, 0.016); // the press itself acts
    REQUIRE(s.text == "Ashe");
    text_field::update(s, in, 0.016); // still inside the delay -- nothing
    text_field::update(s, in, 0.016);
    REQUIRE(s.text == "Ashe");
}

TEST_CASE("a held backspace repeats once the delay has passed", "[textfield]")
{
    State s;
    text_field::begin(s, "Ashen");
    Input in;
    in.backspace = true;

    text_field::update(s, in, 0.016);      // press -> "Ashe"
    text_field::update(s, in, kLongFrame); // past the delay -> repeats
    REQUIRE(s.text == "Ash");
}

TEST_CASE("releasing backspace re-arms the immediate press", "[textfield]")
{
    // Tap-tap-tap must delete three, not one-then-wait.
    State s;
    text_field::begin(s, "Ashen");
    Input down;
    down.backspace = true;
    const Input up;

    text_field::update(s, down, 0.016);
    text_field::update(s, up, 0.016); // released
    text_field::update(s, down, 0.016);
    text_field::update(s, up, 0.016);
    text_field::update(s, down, 0.016);
    REQUIRE(s.text == "As");
}

TEST_CASE("the cursor moves and clamps at both ends", "[textfield]")
{
    State s;
    text_field::begin(s, "Ash");

    Input left;
    left.left = true;
    text_field::update(s, left, 0.016);
    REQUIRE(s.cursor == 2);

    for (int i = 0; i < 10; ++i)
        text_field::update(s, left, 0.016);
    REQUIRE(s.cursor == 0); // clamped, not negative

    Input right;
    right.right = true;
    for (int i = 0; i < 10; ++i)
        text_field::update(s, right, 0.016);
    REQUIRE(s.cursor == 3); // clamped to the end
}

TEST_CASE("home and end jump the cursor", "[textfield]")
{
    State s;
    text_field::begin(s, "Ash");

    Input home;
    home.home = true;
    text_field::update(s, home, 0.016);
    REQUIRE(s.cursor == 0);

    Input end;
    end.end = true;
    text_field::update(s, end, 0.016);
    REQUIRE(s.cursor == 3);
}

TEST_CASE("a name needs something other than whitespace", "[textfield]")
{
    REQUIRE(text_field::acceptable("Ash"));
    REQUIRE(text_field::acceptable("  Ash  ")); // padding is the player's business
    REQUIRE(text_field::acceptable("z"));
    REQUIRE_FALSE(text_field::acceptable(""));
    REQUIRE_FALSE(text_field::acceptable("   "));
    REQUIRE_FALSE(text_field::acceptable("\t"));
}

TEST_CASE("duplicate names are acceptable -- a pilgrim is their id", "[textfield]")
{
    // The rule that keeps prison-escape's duplicate-name bug from being possible here.
    REQUIRE(text_field::acceptable("Ash"));
    REQUIRE(text_field::acceptable("Ash")); // again, and still fine
}
