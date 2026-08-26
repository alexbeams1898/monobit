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

// Offer a WAY somewhere: an arrow and where it goes. The arrow says which way (down +1, up -1,
// across 0) so no word has to, and the destination says where -- never where he already is,
// which the HUD says permanently and does not need repeating every time he stands on a hole.
void offerStep(const std::string& destination, int dir);

// Draw the band if anything was offered, and clear the offer.
void render(Engine& engine);

} // namespace prompt
