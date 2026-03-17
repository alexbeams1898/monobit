#include "systems/InputSystem.h"

#include "ecs/Components.h"

#include <SDL.h>
#include <cmath>
#include <iostream>

void InputSystem::update(EntityManager& em)
{
    // SDL_GetKeyboardState returns a pointer into SDL's internal key table.
    // It is updated by SDL_PollEvent — call this after the event loop, not before.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    // Mouse button state — LMB = attack, RMB = block.
    const Uint32 mouseButtons = SDL_GetMouseState(nullptr, nullptr);
    const bool lmbHeld = (mouseButtons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    const bool rmbHeld = (mouseButtons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;

    // --- Movement -----------------------------------------------------------
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

    // Normalise diagonal movement so holding W+D is the same speed as holding W alone.
    const float len = std::sqrt(mx * mx + my * my);
    if (len > 0.0f)
    {
        mx /= len;
        my /= len;
    }

    // --- Combat / action inputs --------------------------------------------
    // Attack: LMB (primary) or E (keyboard backup).
    const bool attackHeld = lmbHeld || keys[SDL_SCANCODE_E] != 0;
    // Dodge: Space.
    const bool dodgeHeld = keys[SDL_SCANCODE_SPACE] != 0;
    // Skill / parry: Q (context-sensitive in CombatSystem).
    const bool skillHeld = keys[SDL_SCANCODE_Q] != 0;
    // Block: RMB (hold).
    const bool block_held = rmbHeld;

    // Auto-attack toggle (P) — edge-detect via static previous-frame state.
    static bool prevP = false;
    const bool curP = keys[SDL_SCANCODE_P] != 0;
    const bool autoJust = curP && !prevP;
    prevP = curP;

    // Block edge-detect (for parry window — Shift + RMB, or just RMB tap).
    static bool prevBlock = false;
    const bool blockJust = block_held && !prevBlock;
    prevBlock = block_held;

    // Debug stat allocation (1/2/3/4) — edge-detect per key.
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

    // --- Write to Input components ----------------------------------------
    for (auto [entity, input] : em.registry().view<Input>().each())
    {
        input.move_x = mx;
        input.move_y = my;

        // Persist last non-zero facing direction for hitbox targeting.
        if (mx != 0.0f || my != 0.0f)
        {
            input.last_facing_x = mx;
            input.last_facing_y = my;
        }

        input.attack = attackHeld;
        input.dodge = dodgeHeld;
        input.skill = skillHeld;
        input.block_held = block_held;
        input.block_just_pressed = blockJust;
        input.auto_toggle_just_pressed = autoJust;

        input.alloc_str = alloc1;
        input.alloc_dex = alloc2;
        input.alloc_end = alloc3;
        input.alloc_lck = alloc4;

        // Debug stat-alloc hint when points are available (only fires on key press).
        if ((alloc1 || alloc2 || alloc3 || alloc4) && em.registry().all_of<Experience>(entity))
        {
            const auto& exp = em.registry().get<Experience>(entity);
            if (exp.stat_points <= 0)
                std::cout << "[InputSystem] No stat points available.\n";
        }
    }
}
