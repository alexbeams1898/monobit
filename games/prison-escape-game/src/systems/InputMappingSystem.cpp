#include "systems/InputMappingSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <tracy/Tracy.hpp>

// InputMappingSystem -- maps raw SDL input to game action booleans.
// All key-to-action bindings live here. To rebind a key, change it here only.
//
// One-shot inputs (dodge, craft, stat alloc, etc.) read from the event buffer
// filled by Engine::processEvents(). This guarantees brief key taps are never
// lost between fixed-step ticks. Continuous inputs (move, attack, sprint) still
// poll SDL_GetKeyboardState for the current held state.

static bool hasKey(const std::vector<int>& events, int scancode)
{
    return std::find(events.begin(), events.end(), scancode) != events.end();
}

static bool hasMouse(const std::vector<uint8_t>& events, uint8_t button)
{
    return std::find(events.begin(), events.end(), button) != events.end();
}

void InputMappingSystem::update(EntityManager& em)
{
    ZoneScopedN("InputMappingSystem");
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

    // Continuous (held) inputs -- polled from current keyboard/mouse state.
    // Right hand: E key or RMB. Left hand: Q key or LMB. Skill: Ctrl.
    const bool rightAttackHeld = rmbHeld || keys[SDL_SCANCODE_E] != 0;
    const bool leftAttackHeld = lmbHeld || keys[SDL_SCANCODE_Q] != 0;
    const bool skillHeld = keys[SDL_SCANCODE_LCTRL] != 0 || keys[SDL_SCANCODE_RCTRL] != 0;
    const bool blockHeld = false; // block is now per-hand via shield detection
    const bool sprintHeld = keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0;

    // One-shot inputs -- from event buffer so brief taps between ticks aren't lost.
    // Only fire on the FIRST tick of the frame; on later ticks within the same
    // frame the buffer is still populated (it's cleared in gameRenderUI), but
    // the press has already been delivered. Without this gate, a 2-tick frame
    // would let cycle_weapon (and every other one-shot) fire twice from a
    // single key press -- visible as "X skipped a weapon".
    const bool firstTickOfFrame = (em.ticks_this_frame == 0);
    const auto& kd = em.key_down_events;
    const auto& md = em.mouse_down_events;

    const bool dodgeJust = firstTickOfFrame && hasKey(kd, SDL_SCANCODE_SPACE);
    const bool autoJust = firstTickOfFrame && hasKey(kd, SDL_SCANCODE_P);
    const bool blockJust = false; // block handled per-hand
    // Right-hand weapon cycle: V=forward, C=backward. Left-hand: X=forward, Z=backward.
    const bool cycleWeaponJust = firstTickOfFrame && hasKey(kd, SDL_SCANCODE_V);
    const bool cycleWeaponPrevJust = firstTickOfFrame && hasKey(kd, SDL_SCANCODE_C);
    const bool cycleLeftWeaponJust = firstTickOfFrame && hasKey(kd, SDL_SCANCODE_X);
    const bool cycleLeftWeaponPrevJust = firstTickOfFrame && hasKey(kd, SDL_SCANCODE_Z);
    const bool interactJust = firstTickOfFrame && hasKey(kd, SDL_SCANCODE_F);
    const bool lmbJust = firstTickOfFrame && hasMouse(md, SDL_BUTTON_LEFT);
    const bool lockOnJust = firstTickOfFrame && hasMouse(md, SDL_BUTTON_MIDDLE);
    const bool reloadJust = firstTickOfFrame && hasKey(kd, SDL_SCANCODE_R);
    const bool inventoryJust = firstTickOfFrame && hasKey(kd, SDL_SCANCODE_I);
    const bool pauseJust =
        firstTickOfFrame && (hasKey(kd, SDL_SCANCODE_ESCAPE) || hasKey(kd, SDL_SCANCODE_TAB));
    const bool twoHandJust =
        firstTickOfFrame && (hasKey(kd, SDL_SCANCODE_LALT) || hasKey(kd, SDL_SCANCODE_RALT));

    for (auto [entity, actions] : em.registry().view<PlayerActions>().each())
    {
        actions.move_x = mx;
        actions.move_y = my;
        actions.right_attack = rightAttackHeld;
        actions.left_attack = leftAttackHeld;
        actions.dodge = dodgeJust;
        actions.skill = skillHeld;
        actions.sprint = sprintHeld;
        actions.block_held = blockHeld;
        actions.block_just_pressed = blockJust;
        actions.auto_toggle_just_pressed = autoJust;
        actions.cycle_weapon = cycleWeaponJust;
        actions.cycle_weapon_prev = cycleWeaponPrevJust;
        actions.cycle_left_weapon = cycleLeftWeaponJust;
        actions.cycle_left_weapon_prev = cycleLeftWeaponPrevJust;
        actions.interact = interactJust;
        actions.mouse_click = lmbJust;
        actions.lock_on_toggle = lockOnJust;
        actions.reload = reloadJust;
        actions.toggle_inventory = inventoryJust;
        actions.toggle_pause = pauseJust;
        actions.toggle_two_hand = twoHandJust;

        // MovementIntent bridge: lets engine AnimationSystem read movement direction
        // for walk-direction snapping without knowing about game components.
        em.registry().emplace_or_replace<MovementIntent>(entity, MovementIntent{mx, my});
    }

    // Event buffers (key_down_events, mouse_down_events) are cleared after
    // the render UI pass so that UI screens can read them. See gameRenderUI().
}
