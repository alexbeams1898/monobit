#pragma once

// Shared test scaffolding for Selva-side tests that need an active
// PlayerProfile (cycleHand, setQuickSlot, cyclePrimed, healAction,
// any future use_handler test, etc).
//
// The production runtime sets gameState().phase = Playing and an
// active_character name; activePlayerProfile() returns the
// PlayerProfile matching that name from saveData().characters.
// Tests don't go through the main-menu->Playing transition so we
// set the same state directly via this RAII helper.
//
// Usage:
//   TEST_CASE("...") {
//     selva::tests::ActiveProfileScope scope{"PILGRIM"};
//     auto& profile = scope.profile();
//     // ... mutate inventory / equipment / etc and call cycleHand /
//     //     setQuickSlot / etc; activePlayerProfile() returns
//     //     &profile inside this scope.
//   }
//
// Restores prior gameState + saveData on destruction so test order
// doesn't matter and concurrent test cases don't leak state.

#include "AppState.h"
#include "AppStateGlobal.h"

#include <string>
#include <utility>
#include <vector>

namespace selva::tests
{

class ActiveProfileScope
{
  public:
    explicit ActiveProfileScope(std::string character_name)
    {
        saved_phase_ = selva::gameState().phase;
        saved_active_name_ = selva::gameState().active_character;
        saved_characters_ = std::move(selva::saveData().characters);

        selva::saveData().characters.clear();
        selva::PlayerProfile p;
        p.name = character_name;
        selva::saveData().characters.push_back(std::move(p));
        selva::gameState().active_character = std::move(character_name);
        selva::gameState().phase = selva::GameState::Phase::Playing;
    }

    ~ActiveProfileScope()
    {
        selva::gameState().phase = saved_phase_;
        selva::gameState().active_character = std::move(saved_active_name_);
        selva::saveData().characters = std::move(saved_characters_);
    }

    ActiveProfileScope(const ActiveProfileScope&) = delete;
    ActiveProfileScope& operator=(const ActiveProfileScope&) = delete;

    selva::PlayerProfile& profile()
    {
        return selva::saveData().characters.front();
    }

  private:
    selva::GameState::Phase saved_phase_;
    std::string saved_active_name_;
    std::vector<selva::PlayerProfile> saved_characters_;
};

} // namespace selva::tests
