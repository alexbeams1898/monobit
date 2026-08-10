#pragma once

#include <string>

class Engine;

// THE PROMPT: the one surface through which the world offers something -- an interaction now;
// dialog lines and choices later, through the same band. The genre's shape: a quiet dark strip,
// bottom-centre, naming the action and the key. One piece of furniture for every "the world is
// offering" moment, so nothing else ever invents its own popup.
//
// Offered per frame by whoever has something in reach; drawing consumes the offer, so a prompt
// vanishes the frame its reason does.
namespace prompt
{

// Offer an action for this frame, e.g. "Rest".
void offer(const std::string& action);

// Draw the band if anything was offered, and clear the offer.
void render(Engine& engine);

} // namespace prompt
