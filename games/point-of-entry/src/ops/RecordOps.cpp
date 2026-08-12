#include "ops/RecordOps.h"

namespace record
{
namespace
{
std::map<std::string, int> sKills;
} // namespace

void countKill(const std::string& species)
{
    ++sKills[species];
}

int kills(const std::string& species)
{
    const auto it = sKills.find(species);
    return it != sKills.end() ? it->second : 0;
}

const std::map<std::string, int>& all()
{
    return sKills;
}

void reset()
{
    sKills.clear();
}

} // namespace record
