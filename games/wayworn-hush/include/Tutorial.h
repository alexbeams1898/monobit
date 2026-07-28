#pragma once

#include "HudCanvas.h"

#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

// First-time teaching cards: the moment a mechanic first happens, the world
// stops and the screen hones in on the thing being taught -- one card, once per
// pilgrim, dismissed with Space. Cards are authored in config/tutorial.json
// against a small vocabulary of first-time events the game emits (see the
// tutorial pump in GameLoop). See docs/design/GAME-SYSTEMS.md section 9.
namespace tutorial
{

using FontHandle = int;

struct Card
{
    std::string id;
    std::string on; // event that triggers it (game-emitted, e.g. "reading")
    std::string title;
    std::string body;
    std::string focus; // hud region left lit while the card is up:
                       // "content" / "notification"; empty = full dim
};

struct Config
{
    std::vector<Card> cards;
};

// Runtime: which cards this pilgrim has seen (saved), what is queued to show.
// `queue` points into the Config (loaded once at boot, stable for the run).
// The on-screen edge-detect flags live here too -- per-walk, like everything
// else in this struct (a function-local static would leak across walks).
struct State
{
    std::unordered_set<std::string> seen;
    std::deque<const Card*> queue;
    bool line_was_up = false;
    bool menu_was_up = false;
};

// Load config/tutorial.json (silent no-op -> no cards if missing).
void load(Config& cfg, const std::string& path);

// An event happened: queue every unseen card listening on it. Marked seen as it
// queues, so a repeated event can never double-queue.
void fire(State& st, const Config& cfg, const std::string& event);

// The card on screen, if any (front of the queue).
const Card* current(const State& st);
void dismiss(State& st);

// Rendering (needs GL -- integration-tested by running the game, like the box).
void init(FontHandle body_font, FontHandle heading_font);
void render(const Card& card, const hud::Regions& regions, int windowW, int windowH);

} // namespace tutorial
