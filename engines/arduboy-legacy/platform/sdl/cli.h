// SDL-only CLI parser for debug/test overrides. Consumes argv, builds a
// game::DebugCfg that the game applies after init(). All flags are
// PC-only — none touch the Arduboy build.

#pragma once

#include "game.h"

namespace cli {

// Parse argv into a DebugCfg. Returns false on --help or parse error
// (in which case the process should exit with usage already printed to
// stderr). Returns true and fills `out` otherwise.
bool parse(int argc, char** argv, game::DebugCfg& out);

}  // namespace cli
