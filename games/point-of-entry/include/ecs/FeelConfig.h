#pragma once

#include <string>

// THE NUMBERS YOU TUNE BY PLAYING.
//
// Not the derivation formulas (config/stats.json owns those -- what a body's health IS made of),
// and not the assault curves (config/swarm.json owns those). These are the ones that are only
// ever settled by walking around: how far a stride carries, how close a pest has to get, how
// long a curtain hangs. Every one of them was a constexpr somewhere until it turned out that
// answering "is that too fast?" meant a rebuild.
//
// Grouped by the thing they belong to rather than by the file that reads them, so a number is
// where you would look for it rather than where it happens to be used.
namespace feel
{

struct Numbers
{
    struct Aim
    {
        // Below this the cursor is not pointing anywhere -- it is his hand shaking on the mouse.
        float dead_zone = 8.0f;
    } aim;

    struct Walk
    {
        float stride = 34.0f;        // world px per step; the CADENCE knob
        float hop = 2.0f;            // how far he rises at the peak of one
        float tilt = 0.12f;          // radians of rock, ~7 degrees
        float poses_per_step = 4.0f; // held poses per step: fewer is snappier cutout
        // Corner forgiveness: the collider is shrunk by this so a doorway taken at a slight
        // angle does not catch on the jamb.
        float inset = 2.0f;
        float brace_time = 0.18f; // how long a guard takes to set the feet
    } walk;

    struct Contact
    {
        float interval = 0.6f; // seconds before the same pest can touch him again
        float radius = 12.0f;  // how close touching is
        float spacing = 5.0f;  // two pests closer than this get pushed apart
    } contact;

    struct Wand
    {
        // Seconds of spray worth starting for. A sliver of stamina buys a millisecond of it,
        // which is a stutter rather than a spray.
        float resume_seconds = 0.4f;
    } wand;

    struct Fade
    {
        float death = 0.4f;   // how long a body takes to go out
        float travel = 0.35f; // each way; the cut hides under it
    } fade;

    struct Reach
    {
        float pickup = 14.0f;  // how close he has to be to take something up
        float passage = 16.0f; // and to be offered a way through
    } reach;

    struct Reek
    {
        float every = 0.5f;  // seconds between puffs
        float rise = 24.0f;  // how fast it leaves the hole
        float drift = 9.0f;  // the wander across that
        float mouth = 10.0f; // back from the mouth to the opening it comes out of
        float stand = 8.0f;  // how far outside a wall the mouth itself stands
    } reek;
};

// Read config/feel.json. Anything missing keeps the value above, so the game runs with no file
// at all and a half-written one is not a crash.
bool load(const std::string& path = "config/feel.json");
const Numbers& current();

} // namespace feel
