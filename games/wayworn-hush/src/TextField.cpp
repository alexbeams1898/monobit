#include "TextField.h"

#include <algorithm>
#include <cctype>

namespace text_field
{
namespace
{
// Key-repeat feel: how long a key must be held before it starts repeating, and
// how fast it repeats after that. Matches the platform-typical shape (a pause,
// then a steady stream) so held backspace behaves the way a typist expects.
constexpr double kRepeatDelaySecs = 0.4;
constexpr double kRepeatEverySecs = 0.03;

// Whether the held edit key should act this frame: immediately on the press,
// then not again until the delay has passed, then at the repeat rate.
bool fires(State& s, double dt)
{
    const double before = s.held_secs;
    s.held_secs += dt;
    if (before == 0.0)
    {
        s.last_fire_secs = 0.0;
        return true; // the initial press always acts
    }
    if (s.held_secs < kRepeatDelaySecs)
        return false;
    if (s.held_secs - s.last_fire_secs < kRepeatEverySecs)
        return false;
    s.last_fire_secs = s.held_secs;
    return true;
}

void clampCursor(State& s)
{
    s.cursor = std::clamp(s.cursor, 0, static_cast<int>(s.text.size()));
}
} // namespace

void begin(State& state, const std::string& initial)
{
    state.text = initial;
    if (static_cast<int>(state.text.size()) > kMaxLength)
        state.text.resize(kMaxLength);
    state.cursor = static_cast<int>(state.text.size());
    state.held_secs = 0.0;
    state.last_fire_secs = 0.0;
}

void update(State& state, const Input& input, double dt)
{
    clampCursor(state); // the caller may have touched the text between frames

    // Typed characters land at the cursor. Control characters never arrive here
    // (the platform's text input excludes them), but a stray one would corrupt a
    // name, so they're dropped rather than trusted.
    for (const char c : input.typed)
    {
        if (static_cast<int>(state.text.size()) >= kMaxLength)
            break;
        if (static_cast<unsigned char>(c) < 32)
            continue;
        state.text.insert(static_cast<std::size_t>(state.cursor), 1, c);
        ++state.cursor;
    }

    // One edit key at a time: holding backspace while tapping delete is not a
    // thing to support, and sharing one timer keeps the repeat honest.
    const bool editing = input.backspace || input.del;
    if (!editing)
    {
        state.held_secs = 0.0;
        state.last_fire_secs = 0.0;
    }
    else if (fires(state, dt))
    {
        if (input.backspace && state.cursor > 0)
        {
            state.text.erase(static_cast<std::size_t>(state.cursor - 1), 1);
            --state.cursor;
        }
        else if (input.del && state.cursor < static_cast<int>(state.text.size()))
        {
            state.text.erase(static_cast<std::size_t>(state.cursor), 1);
        }
    }

    // Cursor movement is edge-triggered by the caller (a held arrow scrolling a
    // 20-character line is not worth the machinery).
    if (input.left)
        --state.cursor;
    if (input.right)
        ++state.cursor;
    if (input.home)
        state.cursor = 0;
    if (input.end)
        state.cursor = static_cast<int>(state.text.size());
    clampCursor(state);
}

bool acceptable(const std::string& text)
{
    return std::any_of(text.begin(), text.end(),
                       [](unsigned char c) { return std::isspace(c) == 0; });
}

} // namespace text_field
