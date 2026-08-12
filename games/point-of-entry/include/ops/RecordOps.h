#pragma once

#include <map>
#include <string>

// THE RECORD: per-species kill tallies, the one ledger of everything he has put down. Guide
// pages, rank and achievements all READ this; nothing keeps a count of its own. A species is
// its creature file path -- the same id the swarm spawns by. Session-only for now; saving it
// is a later system.
namespace record
{

void countKill(const std::string& species);
int kills(const std::string& species);

// Ordered by key, so a listing built from it never reshuffles between frames.
const std::map<std::string, int>& all();

void reset();

// Put a remembered ledger back, replacing whatever is counted now.
void restore(const std::map<std::string, int>& tallies);

} // namespace record
