#include "systems/WaveSystem.h"

#include "SpriteDefLoader.h"
#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/LogUtils.h"
#include "ops/NavUtils.h"
#include "systems/SpriteAnimSystem.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

namespace swarm
{
namespace
{

// A species, read whole from its file. `evolves_*` is the depth law's swap: past the
// threshold, this species stops coming through and its worse self comes instead.
struct Creature
{
    sprite_def::Def def;
    int health = 7;
    float contact = 4.0f;
    float speed = 46.0f;
    int xp = 2;
    float scale = 1.0f;
    Motion motion;
    std::vector<DropEntry> drops;
    std::string evolves_into; // creature file path; empty = terminal form
    int evolves_at_depth = 0;
};

// One line of a seep's fauna.
struct SeepEntry
{
    std::size_t creature = 0;
    int weight = 1;
    int min_wave = 1;
};

// A seep's program: its fauna and how its waves run. Fields default from the floor-wide
// assault curves; a seep file may override any of them.
struct SeepProgram
{
    std::vector<SeepEntry> entries;
    int waves = 3;
    int per_wave = 6;
    float growth = 1.4f;
    float spacing = 0.25f;
    float breath = 3.0f;
};

// One placed hole, running its program.
struct ActiveSeep
{
    Seep at;
    SeepProgram program;
    int wave = 0;
    int to_emerge = 0;
    float timer = 0.0f;
    bool emerging = false;
    bool done = false;
};

// How depth juices what emerges: multiplicative per level of depth, from config. One formula
// for every species -- the law scales the world, not cases.
struct Juice
{
    float health = 0.12f;
    float damage = 0.10f;
    float speed = 0.03f;
    float xp = 0.15f;
};

std::vector<Creature> sCreatures;
std::unordered_map<std::string, std::size_t> sCreatureIndex;
std::unordered_map<std::string, SeepProgram> sSeepTypes;
std::vector<ActiveSeep> sSeeps;
int sDepth = 0;
Juice sJuice;
float sSurgeSpeed = 150.0f;
float sSurgeDuration = 0.35f;
unsigned sSpawnCounter = 0;

// Assault defaults at this depth, before any per-seep overrides.
SeepProgram sBaseProgram;

float juiceFactor(float perDepth)
{
    return 1.0f + perDepth * static_cast<float>(sDepth);
}

std::size_t loadCreature(const std::string& path)
{
    if (const auto it = sCreatureIndex.find(path); it != sCreatureIndex.end())
        return it->second;
    std::ifstream in(path);
    if (!in)
    {
        poe::log().error("swarm: no creature file at '{}'", path);
        return static_cast<std::size_t>(-1);
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().error("swarm: creature file '{}' is not valid JSON", path);
        return static_cast<std::size_t>(-1);
    }
    Creature c;
    c.def = sprite_def::load(j.value("sprite", std::string{}));
    c.health = j.value("health", c.health);
    c.contact = j.value("contact_damage", c.contact);
    c.speed = j.value("speed", c.speed);
    c.xp = j.value("xp", c.xp);
    c.scale = j.value("scale", c.scale);
    const auto& mv = j.value("movement", nlohmann::json::object());
    c.motion.buzz_hz = mv.value("buzz_hz", 0.0f);
    c.motion.buzz_amount = mv.value("buzz_amount", 0.0f);
    c.motion.drift_hz = mv.value("drift_hz", 0.0f);
    c.motion.drift_amount = mv.value("drift_amount", 0.0f);
    for (const auto& d : j.value("drops", nlohmann::json::array()))
    {
        DropEntry entry;
        entry.item = d.value("item", std::string{});
        entry.min = d.value("min", 1);
        entry.max = d.value("max", 1);
        entry.chance = d.value("chance", 1.0f);
        c.drops.push_back(std::move(entry));
    }
    const auto& ev = j.value("evolves", nlohmann::json::object());
    c.evolves_into = ev.value("into", std::string{});
    c.evolves_at_depth = ev.value("at_depth", 0);
    sCreatures.push_back(std::move(c));
    sCreatureIndex.emplace(path, sCreatures.size() - 1);
    // Resolve the evolved form up front, so depth can swap without touching disk mid-wave.
    const std::size_t idx = sCreatures.size() - 1;
    if (!sCreatures[idx].evolves_into.empty())
        loadCreature(sCreatures[idx].evolves_into);
    return idx;
}

SeepProgram loadSeepType(const std::string& path)
{
    if (const auto it = sSeepTypes.find(path); it != sSeepTypes.end())
        return it->second;
    SeepProgram program = sBaseProgram;
    std::ifstream in(path);
    if (!in)
    {
        poe::log().error("swarm: no seep file at '{}' -- using floor defaults", path);
        sSeepTypes.emplace(path, program);
        return program;
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().error("swarm: seep file '{}' is not valid JSON", path);
        sSeepTypes.emplace(path, program);
        return program;
    }
    for (const auto& entry : j.value("creatures", nlohmann::json::array()))
    {
        const std::size_t idx = loadCreature(entry.value("creature", std::string{}));
        if (idx == static_cast<std::size_t>(-1))
            continue;
        program.entries.push_back(
            SeepEntry{idx, entry.value("weight", 1), entry.value("min_wave", 1)});
    }
    // A seep may override any part of its wave program; unspecified fields keep the floor's.
    const auto& w = j.value("waves", nlohmann::json::object());
    program.waves = w.value("count", program.waves);
    program.per_wave = w.value("per_wave", program.per_wave);
    program.growth = w.value("growth", program.growth);
    program.spacing = w.value("spacing", program.spacing);
    program.breath = w.value("breath", program.breath);
    sSeepTypes.emplace(path, program);
    return program;
}

int countForWave(const SeepProgram& p, int wave)
{
    float n = static_cast<float>(p.per_wave);
    for (int i = 1; i < wave; ++i)
        n *= p.growth;
    return static_cast<int>(n);
}

// Which species surfaces from THIS hole: weighted round-robin among its fauna whose min_wave
// has come, then the depth law's swap -- past the threshold, the evolved form comes instead.
const Creature* pickCreature(const ActiveSeep& seep, unsigned n)
{
    if (seep.program.entries.empty())
        return nullptr;
    const auto& entries = seep.program.entries;
    int totalWeight = 0;
    for (const auto& entry : entries)
        if (seep.wave >= entry.min_wave)
            totalWeight += entry.weight;
    std::size_t chosen = entries.front().creature;
    if (totalWeight > 0)
    {
        int ticket = static_cast<int>(n % static_cast<unsigned>(totalWeight));
        for (const auto& entry : entries)
        {
            if (seep.wave < entry.min_wave)
                continue;
            ticket -= entry.weight;
            if (ticket < 0)
            {
                chosen = entry.creature;
                break;
            }
        }
    }
    // The swap walks the chain, so deep enough digs can be two evolutions past the surface form.
    while (!sCreatures[chosen].evolves_into.empty() &&
           sDepth >= sCreatures[chosen].evolves_at_depth && sCreatures[chosen].evolves_at_depth > 0)
    {
        const auto it = sCreatureIndex.find(sCreatures[chosen].evolves_into);
        if (it == sCreatureIndex.end())
            break;
        chosen = it->second;
    }
    return &sCreatures[chosen];
}

void emerge(EntityManager& em, float atX, float atY, int seepIndex, const Creature& kind)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();

    // Scatter INSIDE the pit; the crowd spreads on its own once it walks.
    const auto n = static_cast<float>(sSpawnCounter++);
    const float angle = n * 2.39996f; // golden angle: successive spawns never line up
    const float spread = 2.0f + std::fmod(n, 5.0f) * 1.5f;
    Transform t{};
    t.x = atX + std::cos(angle) * spread;
    t.y = atY + std::sin(angle) * spread;
    // Nothing materialises inside a wall; the seep itself is floor by generation's guarantee.
    if (!world::boxFree(em, t.x, t.y, 3.0f, 3.0f))
    {
        t.x = atX;
        t.y = atY;
    }
    t.scale = kind.scale;
    reg.emplace<Transform>(e, t);
    reg.emplace<PreviousTransform>(e, PreviousTransform{t.x, t.y});

    Sprite spr{};
    spr.layer = 2;
    if (kind.def.ok)
    {
        spr.texture_path = kind.def.sheet;
        spr.src_w = kind.def.frame_w;
        spr.src_h = kind.def.frame_h;
        reg.emplace<Sprite>(e, spr);
    }
    else
    {
        spr.src_w = 5;
        spr.src_h = 3;
        reg.emplace<Sprite>(e, spr);
        reg.emplace<SolidColor>(e, SolidColor{0.14f, 0.11f, 0.10f});
    }
    reg.emplace<FacingDirection>(e, FacingDirection{});
    reg.emplace<Velocity>(e, Velocity{});
    sprite_anim::attach(em, e, kind.def);

    // THE JUICE: the law of depth, applied at the moment of emergence. Same species, worse.
    const int hp = std::max(1, static_cast<int>(std::lround(static_cast<float>(kind.health) *
                                                            juiceFactor(sJuice.health))));
    reg.emplace<Health>(e, Health{hp, hp});
    reg.emplace<Vermin>(e, Vermin{kind.contact * juiceFactor(sJuice.damage),
                                  kind.speed * juiceFactor(sJuice.speed)});
    reg.emplace<Worth>(e,
                       Worth{std::max(1, static_cast<int>(std::lround(static_cast<float>(kind.xp) *
                                                                      juiceFactor(sJuice.xp))))});
    reg.emplace<Motion>(e, kind.motion);
    if (!kind.drops.empty())
        reg.emplace<DropTable>(e, DropTable{kind.drops});
    reg.emplace<Surge>(e, Surge{std::cos(angle), std::sin(angle), sSurgeSpeed,
                                sSurgeDuration * (0.7f + std::fmod(n, 7.0f) * 0.09f)});
    reg.emplace<SeepSource>(e, SeepSource{seepIndex});
}

// Anything this hole scheduled still standing? Its next wave waits until the answer is no --
// CLEARING gates progress, per hole; the breath begins only over a quiet yard.
int livingFrom(const EntityManager& em, int seepIndex)
{
    int n = 0;
    for (auto [entity, vermin, source] : em.registry().view<Vermin, SeepSource>().each())
        if (source.index == seepIndex && !em.registry().all_of<Dying>(entity))
            ++n;
    return n;
}

} // namespace

void begin(const std::string& configPath, const std::vector<Seep>& seeps, int depth)
{
    sDepth = depth;
    sCreatures.clear();
    sCreatureIndex.clear();
    sSeepTypes.clear();
    sSeeps.clear();

    std::ifstream in(configPath);
    const nlohmann::json j =
        in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
    if (j.is_discarded() || !in)
        poe::log().warn("swarm: no usable config at '{}' -- using defaults", configPath);

    // The floor's base program: the assault curves, bent by depth. Deeper floors press harder
    // before any seep says a word -- the law again, applied to pacing.
    const auto& base = j.value("base", nlohmann::json::object());
    const auto& per = j.value("per_depth", nlohmann::json::object());
    const auto d = static_cast<float>(depth);
    sBaseProgram = SeepProgram{};
    sBaseProgram.waves = base.value("waves", 3) + static_cast<int>(per.value("waves", 0.34f) * d);
    sBaseProgram.per_wave =
        base.value("per_wave", 6) + static_cast<int>(per.value("per_wave", 1.5f) * d);
    sBaseProgram.growth = base.value("growth", 1.4f);
    sBaseProgram.spacing =
        std::max(0.05f, base.value("spacing", 0.25f) - per.value("spacing", 0.01f) * d);
    sBaseProgram.breath =
        std::max(1.0f, base.value("breath", 3.0f) - per.value("breath", 0.1f) * d);

    const auto& juice = j.value("juice", nlohmann::json::object());
    sJuice.health = juice.value("health_per_depth", sJuice.health);
    sJuice.damage = juice.value("damage_per_depth", sJuice.damage);
    sJuice.speed = juice.value("speed_per_depth", sJuice.speed);
    sJuice.xp = juice.value("xp_per_depth", sJuice.xp);

    const auto& surge = j.value("surge", nlohmann::json::object());
    sSurgeSpeed = surge.value("speed", sSurgeSpeed);
    sSurgeDuration = surge.value("duration", sSurgeDuration);

    for (const auto& seep : seeps)
    {
        ActiveSeep active;
        active.at = seep;
        active.program = loadSeepType(seep.type);
        sSeeps.push_back(std::move(active));
    }
    restart();
    poe::log().info("swarm: {} seep(s) at depth {}", sSeeps.size(), depth);
    for (std::size_t i = 0; i < sSeeps.size(); ++i)
        poe::log().info("swarm:   seep[{}] at ({:.0f},{:.0f}) type '{}' ({} fauna)", i,
                        sSeeps[i].at.x, sSeeps[i].at.y, sSeeps[i].at.type,
                        sSeeps[i].program.entries.size());
}

void restart()
{
    for (auto& seep : sSeeps)
    {
        seep.wave = 0;
        seep.to_emerge = 0;
        seep.timer = seep.program.breath;
        seep.emerging = false;
        seep.done = seep.program.entries.empty();
    }
}

void spawnOne(EntityManager& em, const std::string& creaturePath, float x, float y)
{
    const std::size_t idx = loadCreature(creaturePath);
    if (idx == static_cast<std::size_t>(-1))
        return;
    // Belongs to no seep's kill gate -- the trickle answers to no wave clock.
    emerge(em, x, y, -1, sCreatures[idx]);
}

void update(EntityManager& em, float dt)
{
    // Every hole on its own clock, but CLEARING gates each: a seep's breath toward its next
    // wave only runs while nothing it scheduled remains standing. Two holes therefore press in
    // staggered rhythm -- whichever yard you clear first starts gathering first.
    for (int i = 0; i < static_cast<int>(sSeeps.size()); ++i)
    {
        auto& seep = sSeeps[static_cast<std::size_t>(i)];
        if (seep.done)
            continue;
        if (seep.emerging)
        {
            seep.timer -= dt;
            while (seep.timer <= 0.0f && seep.to_emerge > 0)
            {
                if (const Creature* kind = pickCreature(seep, sSpawnCounter); kind != nullptr)
                {
                    emerge(em, seep.at.x, seep.at.y, i, *kind);
                    if (seep.to_emerge == countForWave(seep.program, seep.wave))
                        poe::log().info("swarm:   seep[{}] first emergence at ({:.0f},{:.0f})", i,
                                        seep.at.x, seep.at.y);
                }
                --seep.to_emerge;
                seep.timer += seep.program.spacing;
            }
            if (seep.to_emerge <= 0)
                seep.emerging = false;
            continue;
        }
        // Its output must die before its clock runs.
        if (livingFrom(em, i) > 0)
        {
            seep.timer = seep.program.breath;
            continue;
        }
        seep.timer -= dt;
        if (seep.timer > 0.0f)
            continue;
        ++seep.wave;
        if (seep.wave > seep.program.waves)
        {
            seep.done = true;
            continue;
        }
        seep.to_emerge = countForWave(seep.program, seep.wave);
        seep.emerging = true;
        seep.timer = 0.0f;
        poe::log().info("swarm: a seep begins wave {} of {} ({} of them)", seep.wave,
                        seep.program.waves, seep.to_emerge);
    }
}

Phase phase()
{
    if (sSeeps.empty())
        return Phase::Quiet;
    bool allDone = true;
    bool anyEmerging = false;
    bool anyMidProgram = false;
    for (const auto& seep : sSeeps)
    {
        if (!seep.done)
            allDone = false;
        if (seep.emerging)
            anyEmerging = true;
        if (!seep.done && seep.wave > 0)
            anyMidProgram = true;
    }
    if (allDone)
        return Phase::Cleared;
    if (anyEmerging)
        return Phase::Emerging;
    return anyMidProgram ? Phase::Fighting : Phase::Breath;
}

int waveNumber()
{
    int furthest = 0;
    for (const auto& seep : sSeeps)
        furthest = std::max(furthest, seep.wave);
    return furthest;
}

int totalWaves()
{
    int longest = 0;
    for (const auto& seep : sSeeps)
        longest = std::max(longest, seep.program.waves);
    return longest;
}

int remaining(const EntityManager& em)
{
    int n = 0;
    for (auto [entity, vermin] : em.registry().view<Vermin>().each())
        if (!em.registry().all_of<Dying>(entity))
            ++n;
    return n;
}

} // namespace swarm
