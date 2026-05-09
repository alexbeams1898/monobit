#pragma once

#include "anim/AnimationClip.h"

#include <string>
#include <unordered_map>

namespace selva::anim
{

// Loads every .ozz file in a directory and stores them by file-stem name.
// e.g. assets/characters/soldier/sword_and_shield_slash.ozz becomes
// the key "sword_and_shield_slash". Designed to be populated once at
// startup and queried by name from JSON config (weapon attack clips).
//
// Owns the AnimationClips (heavy, non-copyable). Lookup returns a const
// pointer that's stable for the registry's lifetime; nullptr if the name
// wasn't loaded.
//
// Built-in clips (Idle, Walk, Run, TPose) come from Soldier.glb's bundled
// animation tracks and use their original-case names. Mixamo attack clips
// come in lowercase + underscore form via the build's gltf2ozz step.
struct ClipRegistry
{
    std::unordered_map<std::string, AnimationClip> by_name;

    // Load every *.ozz file in `dir`, keyed by file stem. Returns the
    // number of clips loaded. The "skeleton.ozz" file is skipped — it's
    // a skeleton archive, not an animation, and trying to load it as
    // one would just log noise.
    int loadDirectory(const std::string& dir);

    // Load a single clip by absolute/relative path; key it under `name`.
    // Useful for the few clips whose runtime name differs from filename.
    bool loadClip(const std::string& path, const std::string& name);

    // Returns nullptr if `name` isn't in the registry.
    const AnimationClip* get(const std::string& name) const;
};

} // namespace selva::anim
