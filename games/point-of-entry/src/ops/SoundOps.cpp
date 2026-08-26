#include "ops/SoundOps.h"

#include "ops/LogUtils.h"
#include "systems/AudioSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <numeric>
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
    // PITCH. The same recording at the same pitch every time reads as a sample being retriggered
    // rather than as a thing happening twice -- most of all for a voice, where a man grunting on
    // exactly one note is the giveaway. Equal bounds means play it as recorded.
    float min_pitch = 1.0f;
    float max_pitch = 1.0f;
    // THE BAG: which variations are still to be drawn before the pool refills. Drawing without
    // replacement rather than at random, because random gives clumps -- eight footsteps rolled
    // independently will repeat one back-to-back about once every eight steps, and a repeat is
    // the one thing the ear picks out of a sequence meant to sound incidental.
    std::vector<std::size_t> bag;
    std::size_t last = 0; // what came out most recently, so a refill cannot repeat it
    bool drawn = false;
};

std::unordered_map<std::string, Entry> sBank;
std::unordered_set<std::string> sMissingSaid;

std::mt19937& rng()
{
    static std::mt19937 gen(0xA0D10u); // cosmetic only: nothing here is saved or replayed
    return gen;
}

// One variation, without replacement.
std::size_t draw(Entry& e)
{
    if (e.bag.empty())
    {
        e.bag.resize(e.variations.size());
        std::iota(e.bag.begin(), e.bag.end(), std::size_t{0});
        std::shuffle(e.bag.begin(), e.bag.end(), rng());
        // A fresh bag may open with the one that just played, which is the only seam a
        // back-to-back repeat can still come through -- so trade it away from the front.
        if (e.drawn && e.bag.size() > 1 && e.bag.back() == e.last)
            std::swap(e.bag.back(), e.bag.front());
    }
    e.last = e.bag.back();
    e.drawn = true;
    e.bag.pop_back();
    return e.last;
}
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
        entry.min_pitch = body.value("min_pitch", entry.min_pitch);
        entry.max_pitch = body.value("max_pitch", entry.max_pitch);
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

std::string play(const std::string& name)
{
    const auto found = sBank.find(name);
    if (found == sBank.end())
    {
        // Said ONCE. A name missing from the bank is usually asked for from a frame loop, and a
        // log line per frame buries the thing it is trying to report.
        if (sMissingSaid.insert(name).second)
            poe::log().error("sound: nothing in the bank is called '{}'", name);
        return {};
    }
    Entry& entry = found->second;
    const float pitch =
        entry.max_pitch > entry.min_pitch
            ? std::uniform_real_distribution<float>(entry.min_pitch, entry.max_pitch)(rng())
            : entry.min_pitch;
    const std::string& file = entry.variations[draw(entry)];
    AudioSystem::playSfx(file, entry.volume, pitch);
    return file;
}

} // namespace sound
