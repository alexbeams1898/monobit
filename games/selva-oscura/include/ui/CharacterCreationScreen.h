#pragma once

// Pre-Selva soul-shaping screen. Sits between the New Game button
// and the Phase::Playing wake-scene. The player adjusts the
// Appearance struct's identity-defining fields + claims a name;
// the resulting body + name are written atomically onto a fresh
// PlayerProfile when the player confirms.
//
// Doctrine the screen embodies:
//   "The soul has a form before it has a name; the name comes when
//    judgment is faced."
// In v1 this is reinforced by labels (Bearing/Cast/Reach/Stride/
// Tint) and a single closing line; the environment is a neutral
// void (the orbit-preview's flat-gray FBO background). Future
// iterations can add atmospheric framing.
//
// Failure-mode design:
//   - Quit-mid-creation: the placeholder PlayerProfile that the
//     New Game button created is left as an empty-name draft. The
//     LoadGame screen lets the user delete it; resuming from the
//     main menu re-enters this screen for the same draft. No half-
//     committed state escapes.
//   - Quit-just-after-confirm: profile is fully persisted; resume
//     fires the wake scene as if New Game had just been pressed.

namespace selva::ui
{

// True iff the player is currently in the creation screen (Phase
// == CharacterCreation AND not yet confirmed). Read by the mouse-
// capture state machine so cursor frees while the screen is open.
bool characterCreationActive();

// Render entry point. Called from renderScreens() when the phase
// is CharacterCreation. Returns true if the player confirmed this
// frame (caller transitions to Playing); false otherwise.
bool renderCharacterCreationScreen();

// Open the creation screen for the placeholder character at
// saveData().characters.back(). Called from doMainMenuAction() on
// the New Game path AFTER the placeholder PlayerProfile is added.
// Sets the phase to CharacterCreation + seeds the screen state
// from default Appearance.
void openCharacterCreationScreen();

} // namespace selva::ui
