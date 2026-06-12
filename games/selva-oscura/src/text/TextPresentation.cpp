#include "text/TextPresentation.h"

#include <cstdio>

namespace selva::text
{

namespace
{

struct State
{
    bool is_active = false;
    bool just_began = false;
    ActiveTextView view;
    SessionHandlers handlers;
};

State& state()
{
    static State s;
    return s;
}

} // namespace

void begin(const ActiveTextView& initial, SessionHandlers handlers)
{
    auto& s = state();
    // Forcibly end whatever's currently active. The displaced
    // producer's on_end runs BEFORE the new session is installed so
    // it doesn't see the new state during teardown.
    if (s.is_active && s.handlers.on_end)
        s.handlers.on_end();
    s.is_active = true;
    s.just_began = true;
    s.view = initial;
    s.handlers = std::move(handlers);
}

void updateView(const ActiveTextView& v)
{
    auto& s = state();
    if (!s.is_active)
        return;
    s.view = v;
}

void end()
{
    auto& s = state();
    if (!s.is_active)
        return;
    // Snapshot the handler before clearing, so if on_end itself calls
    // begin() the new session's handlers aren't overwritten by our
    // clearing pass.
    auto on_end = s.handlers.on_end;
    s.is_active = false;
    s.just_began = false;
    s.view = ActiveTextView{};
    s.handlers = SessionHandlers{};
    if (on_end)
        on_end();
}

bool active()
{
    return state().is_active;
}

const ActiveTextView* currentView()
{
    auto& s = state();
    return s.is_active ? &s.view : nullptr;
}

void selectChoice(int choice_index)
{
    auto& s = state();
    if (!s.is_active || s.just_began)
        return;
    if (s.handlers.on_select_choice)
        s.handlers.on_select_choice(choice_index);
}

void confirmAdvance()
{
    auto& s = state();
    if (!s.is_active || s.just_began)
        return;
    if (s.handlers.on_confirm_advance)
        s.handlers.on_confirm_advance();
}

void tick()
{
    state().just_began = false;
}

void hardReset()
{
    auto& s = state();
    // Discard handlers without invoking on_end -- character-switch
    // hard reset is a "throw everything away" operation; producers
    // are expected to reinitialize from scratch.
    s.is_active = false;
    s.just_began = false;
    s.view = ActiveTextView{};
    s.handlers = SessionHandlers{};
}

} // namespace selva::text
