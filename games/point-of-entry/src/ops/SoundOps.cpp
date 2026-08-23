#include "ops/SoundOps.h"

#include "ops/LogUtils.h"
#include "systems/AudioSystem.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sound
{
namespace
{
struct Entry
{
    std::vector<std::string> variations;
    float volume = 1.0f;
};

std::unordered_map<std::string, Entry> sBank;
std::unordered_set<std::string> sMissingSaid;
} // namespace

bool load(const std::string& path)
{
    sBank.clear();
    sMissingSaid.clear();
    std::ifstream in(path);
    const nlohmann::json j =
        in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
    if (j.is_discarded() || !j.is_object())
    {
        poe::log().warn("sound: no usable bank at '{}' -- the game runs silent", path);
        return false;
    }
    // HELD BY NAME. `items()` is a call ON the object, so the temporary a `value()` returns is
    // NOT lifetime-extended by the loop -- only a range expression that IS the temporary gets
    // that. Iterating it reads a destroyed object, which surfaces as a null somewhere further in
    // and throws whenever the freed memory happens to look wrong.
    const nlohmann::json bank = j.value("sounds", nlohmann::json::object());
    for (const auto& [name, body] : bank.items())
    {
        Entry entry;
        entry.volume = body.value("volume", entry.volume);
        entry.variations = body.value("variations", std::vector<std::string>{});
        if (const auto one = body.value("path", std::string{}); !one.empty())
            entry.variations.push_back(one);
        if (entry.variations.empty())
        {
            poe::log().error("sound: '{}' names no file", name);
            continue;
        }
        sBank.emplace(name, std::move(entry));
    }
    poe::log().info("sound: {} in the bank", sBank.size());
    return !sBank.empty();
}

void play(const std::string& name)
{
    const auto found = sBank.find(name);
    if (found == sBank.end())
    {
        // Said ONCE. A name missing from the bank is usually asked for from a frame loop, and a
        // log line per frame buries the thing it is trying to report.
        if (sMissingSaid.insert(name).second)
            poe::log().error("sound: nothing in the bank is called '{}'", name);
        return;
    }
    const Entry& entry = found->second;
    static std::mt19937 rng(0xA0D10u); // cosmetic only: nothing here is saved or replayed
    std::uniform_int_distribution<std::size_t> pick(0, entry.variations.size() - 1);
    AudioSystem::playSfx(entry.variations[pick(rng)], entry.volume);
}

} // namespace sound
