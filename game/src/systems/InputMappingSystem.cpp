#include "systems/InputMappingSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"

#include <SDL.h>
#include <cmath>

// InputMappingSystem -- maps raw SDL input to game action booleans.
// All key-to-action bindings live here. To rebind a key, change it here only.

void InputMappingSystem::update(EntityManager& em)
{
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    int mouseX = 0;
    int mouseY = 0;
    const Uint32 mouseButtons = SDL_GetMouseState(&mouseX, &mouseY);
    const bool lmbHeld = (mouseButtons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    const bool rmbHeld = (mouseButtons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;

    // Normalized directional intent from WASD / arrow keys.
    float mx = 0.0f;
    float my = 0.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
        mx -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
        mx += 1.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
        my -= 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
        my += 1.0f;
    const float mlen = std::sqrt(mx * mx + my * my);
    if (mlen > 0.0f)
    {
        mx /= mlen;
        my /= mlen;
    }

    const bool attackHeld = lmbHeld || keys[SDL_SCANCODE_E] != 0;
    const bool skillHeld = keys[SDL_SCANCODE_Q] != 0;
    const bool blockHeld = rmbHeld;
    const bool sprintHeld = keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0;

    // Edge-detect: dodge (Space).
    static bool prevSpace = false;
    const bool spaceHeld = keys[SDL_SCANCODE_SPACE] != 0;
    const bool dodgeJust = spaceHeld && !prevSpace;
    prevSpace = spaceHeld;

    // Edge-detect: auto-attack toggle (P).
    static bool prevP = false;
    const bool curP = keys[SDL_SCANCODE_P] != 0;
    const bool autoJust = curP && !prevP;
    prevP = curP;

    // Edge-detect: block parry window.
    static bool prevBlock = false;
    const bool blockJust = blockHeld && !prevBlock;
    prevBlock = blockHeld;

    // Edge-detect: wave start (R).
    static bool prevR = false;
    const bool curR = keys[SDL_SCANCODE_R] != 0;
    const bool waveStartJust = curR && !prevR;
    prevR = curR;

    // Edge-detect: stat allocation (1/2/3/4).
    static bool prev1 = false, prev2 = false, prev3 = false, prev4 = false;
    const bool cur1 = keys[SDL_SCANCODE_1] != 0;
    const bool cur2 = keys[SDL_SCANCODE_2] != 0;
    const bool cur3 = keys[SDL_SCANCODE_3] != 0;
    const bool cur4 = keys[SDL_SCANCODE_4] != 0;
    const bool alloc1 = cur1 && !prev1;
    const bool alloc2 = cur2 && !prev2;
    const bool alloc3 = cur3 && !prev3;
    const bool alloc4 = cur4 && !prev4;
    prev1 = cur1;
    prev2 = cur2;
    prev3 = cur3;
    prev4 = cur4;

    for (auto [entity, actions] : em.registry().view<PlayerActions>().each())
    {
        actions.move_x = mx;
        actions.move_y = my;
        actions.attack = attackHeld;
        actions.dodge = dodgeJust;
        actions.skill = skillHeld;
        actions.sprint = sprintHeld;
        actions.block_held = blockHeld;
        actions.block_just_pressed = blockJust;
        actions.auto_toggle_just_pressed = autoJust;
        actions.start_wave = waveStartJust;
        actions.alloc_str = alloc1;
        actions.alloc_dex = alloc2;
        actions.alloc_end = alloc3;
        actions.alloc_lck = alloc4;

        // MovementIntent bridge: lets engine AnimationSystem read movement direction
        // for walk-direction snapping without knowing about game components.
        em.registry().emplace_or_replace<MovementIntent>(entity, MovementIntent{mx, my});
    }
}
