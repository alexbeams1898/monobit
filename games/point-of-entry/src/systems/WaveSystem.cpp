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
#include <random>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

namespace swarm
{
namespace
{

// A species' three stats, all trade terms: resistance survives treatment (hp), defensiveness
// stings back (contact), dispersal moves through a structure (speed). Everything a body is
// derives from these -- same doctrine as the player's sheet, nothing authored twice.
struct Sheet
{
    int resistance = 1;
    int defensiveness = 1;
    int dispersal = 1;
};

// A species, read whole from its file. `evolves_*` is the smell's swap: an individual rolled
// hot enough comes through as this species' worse self instead.
struct Creature
{
    std::string path; // the file it was read from -- the species' one identity
    sprite_def::Def def;
    Sheet sheet;
    Sheet growth; // points the sheet gains per level of depth
    int base_hp = 5;
    float base_power = 3.0f;
    float base_speed = 40.0f;
    int base_xp = 2;
    float scale = 1.0f;
    Motion motion;
    std::vector<DropEntry> drops;
    std::string evolves_into; // creature file path; empty = terminal form
    int evolves_at_smell = 0;
};

// One line of a seep's fauna.
struct SeepEntry
{
    std::size_t creature = 0;
    int weight = 1;
    int min_wave = 1;
    // How deep before this one rides this hole at all. A species arrives at a DEPTH, which is
    // the law of depth applied to the bestiary rather than to the numbers: what comes through
    // a crack near the surface and what comes through the same crack far down are different
    // animals, and the hole's file says where the line is.
    int min_depth = 0;
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
    int depth = 0;
    int wave = 0;
    int to_emerge = 0;
    // Sealed: the program is loaded and waiting, and nothing comes up until he opens it.
    bool sealed_shut = true;
    // What is left of a wave he was part-way through when he last walked out.
    // 0 = the next wave musters whole.
    int owed = 0;
    float timer = 0.0f;
    bool emerging = false;
    bool done = false;
};

// The derivation constants, from config's bestiary block. Cross-feeds are deliberate:
// contact also reads body-weight (resistance, minor), speed also reads temper
// (defensiveness, minor).
struct Bestiary
{
    struct
    {
        float per_resistance = 2.0f;
    } hp;
    struct
    {
        float per_point = 0.08f;
        float defensiveness_weight = 1.0f;
        float resistance_weight = 0.4f;
    } contact;
    struct
    {
        float per_point = 0.08f;
        float dispersal_weight = 1.0f;
        float defensiveness_weight = 0.3f;
    } speed;
    struct
    {
        float per_total = 0.1f; // per effective sheet point above the all-ones baseline
    } xp;
    struct
    {
        // The per-individual roll's range, in percent. Both ends climb with depth -- deeper
        // individuals run hotter -- and the top climbs faster, so the first evolved form
        // arrives as a surprise among normals before its depth owns it.
        int base_min = 0;
        int base_max = 10;
        int min_per_depth = 3;
        int max_per_depth = 8;
    } smell;
};

std::vector<Creature> sCreatures;
std::unordered_map<std::string, std::size_t> sCreatureIndex;
std::unordered_map<std::string, SeepProgram> sSeepTypes;
std::vector<ActiveSeep> sSeeps;
int sDepth = 0;

// The assault curves as authored, kept rather than pre-baked: a passage carrying a deeper
// floor's hole needs that floor's program, which cannot be recovered from one already bent to
// the depth he happens to be standing at.
struct Curves
{
    int waves = 3;
    int per_wave = 6;
    float growth = 1.4f;
    float spacing = 0.25f;
    float breath = 3.0f;
    float waves_per_depth = 0.34f;
    float per_wave_per_depth = 1.5f;
    float spacing_per_depth = 0.01f;
    float breath_per_depth = 0.1f;
};
Curves sCurves;

SeepProgram programAt(int depth)
{
    const auto d = static_cast<float>(depth);
    SeepProgram p;
    p.waves = sCurves.waves + static_cast<int>(sCurves.waves_per_depth * d);
    p.per_wave = sCurves.per_wave + static_cast<int>(sCurves.per_wave_per_depth * d);
    p.growth = sCurves.growth;
    p.spacing = std::max(0.05f, sCurves.spacing - sCurves.spacing_per_depth * d);
    p.breath = std::max(1.0f, sCurves.breath - sCurves.breath_per_depth * d);
    return p;
}
Bestiary sBestiary;
std::mt19937 sRng{std::random_device{}()};
float sSurgeSpeed = 150.0f;
float sSurgeDuration = 0.35f;
unsigned sSpawnCounter = 0;

// Per hole: how many of its program have been killed. The one thing a visit
// leaves behind, and the reason walking out and back in is worth nothing.
std::vector<int> sKilled;

// Assault defaults at this depth, before any per-seep overrides.
SeepProgram sBaseProgram;

// THE SMELL: the law of depth on bodies, one roll per individual at emergence.
int rollSmell(int depth)
{
    const int lo = sBestiary.smell.base_min + sBestiary.smell.min_per_depth * depth;
    const int hi = sBestiary.smell.base_max + sBestiary.smell.max_per_depth * depth;
    std::uniform_int_distribution<int> roll(lo, std::max(lo, hi));
    return roll(sRng);
}

// A body's numbers at emergence: the authored sheet plus the species' growth spread times
// depth -- each kind deepens along its own character -- pushed through the formulas, all of
// it multiplied by the individual's smell.
struct Derived
{
    int hp = 1;
    float contact = 0.0f;
    float speed = 0.0f;
    int xp = 1;
};

Derived derive(const Creature& kind, int smell, int depth)
{
    const auto res = static_cast<float>(kind.sheet.resistance + kind.growth.resistance * depth);
    const auto dfn =
        static_cast<float>(kind.sheet.defensiveness + kind.growth.defensiveness * depth);
    const auto dsp = static_cast<float>(kind.sheet.dispersal + kind.growth.dispersal * depth);
    const float mul = 1.0f + static_cast<float>(smell) / 100.0f;
    Derived d;
    d.hp = std::max(
        1, static_cast<int>(std::lround(
               (static_cast<float>(kind.base_hp) + sBestiary.hp.per_resistance * res) * mul)));
    d.contact =
        kind.base_power *
        (1.0f + sBestiary.contact.per_point * (dfn * sBestiary.contact.defensiveness_weight +
                                               res * sBestiary.contact.resistance_weight)) *
        mul;
    d.speed = kind.base_speed *
              (1.0f + sBestiary.speed.per_point * (dsp * sBestiary.speed.dispersal_weight +
                                                   dfn * sBestiary.speed.defensiveness_weight)) *
              mul;
    const float total = res + dfn + dsp - 3.0f;
    d.xp =
        std::max(1, static_cast<int>(std::lround(static_cast<float>(kind.base_xp) *
                                                 (1.0f + sBestiary.xp.per_total * total) * mul)));
    return d;
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
    c.path = path;
    c.def = sprite_def::load(j.value("sprite", std::string{}));
    const auto& sheet = j.value("sheet", nlohmann::json::object());
    c.sheet.resistance = sheet.value("resistance", c.sheet.resistance);
    c.sheet.defensiveness = sheet.value("defensiveness", c.sheet.defensiveness);
    c.sheet.dispersal = sheet.value("dispersal", c.sheet.dispersal);
    const auto& base = j.value("base", nlohmann::json::object());
    c.base_hp = base.value("hp", c.base_hp);
    c.base_power = base.value("power", c.base_power);
    c.base_speed = base.value("speed", c.base_speed);
    c.base_xp = base.value("xp", c.base_xp);
    const auto& growth = j.value("growth", nlohmann::json::object());
    c.growth.resistance = growth.value("resistance", 0);
    c.growth.defensiveness = growth.value("defensiveness", 0);
    c.growth.dispersal = growth.value("dispersal", 0);
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
    c.evolves_at_smell = ev.value("at_smell", 0);
    sCreatures.push_back(std::move(c));
    sCreatureIndex.emplace(path, sCreatures.size() - 1);
    // Resolve the evolved form up front, so a hot roll can swap without touching disk mid-wave.
    const std::size_t idx = sCreatures.size() - 1;
    if (!sCreatures[idx].evolves_into.empty())
        loadCreature(sCreatures[idx].evolves_into);
    return idx;
}

SeepProgram loadSeepType(const std::string& path, int depth)
{
    const std::string key = path + "@" + std::to_string(depth);
    if (const auto it = sSeepTypes.find(key); it != sSeepTypes.end())
        return it->second;
    SeepProgram program = programAt(depth);
    std::ifstream in(path);
    if (!in)
    {
        poe::log().error("swarm: no seep file at '{}' -- using floor defaults", path);
        sSeepTypes.emplace(key, program);
        return program;
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().error("swarm: seep file '{}' is not valid JSON", path);
        sSeepTypes.emplace(key, program);
        return program;
    }
    for (const auto& entry : j.value("creatures", nlohmann::json::array()))
    {
        const std::size_t idx = loadCreature(entry.value("creature", std::string{}));
        if (idx == static_cast<std::size_t>(-1))
            continue;
        program.entries.push_back(SeepEntry{idx, entry.value("weight", 1),
                                            entry.value("min_wave", 1),
                                            entry.value("min_depth", 0)});
    }
    // A seep may override any part of its wave program; unspecified fields keep the floor's.
    const auto& w = j.value("waves", nlohmann::json::object());
    program.waves = w.value("count", program.waves);
    program.per_wave = w.value("per_wave", program.per_wave);
    program.growth = w.value("growth", program.growth);
    program.spacing = w.value("spacing", program.spacing);
    program.breath = w.value("breath", program.breath);
    sSeepTypes.emplace(key, program);
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
// has come. Evolution is not decided here -- that belongs to the individual's roll.
std::size_t pickCreature(const ActiveSeep& seep, unsigned n)
{
    if (seep.program.entries.empty())
        return static_cast<std::size_t>(-1);
    const auto& entries = seep.program.entries;
    const auto rides = [&](const SeepEntry& entry)
    { return seep.wave >= entry.min_wave && seep.depth >= entry.min_depth; };
    int totalWeight = 0;
    for (const auto& entry : entries)
        if (rides(entry))
            totalWeight += entry.weight;
    std::size_t chosen = entries.front().creature;
    if (totalWeight > 0)
    {
        int ticket = static_cast<int>(n % static_cast<unsigned>(totalWeight));
        for (const auto& entry : entries)
        {
            if (!rides(entry))
                continue;
            ticket -= entry.weight;
            if (ticket < 0)
            {
                chosen = entry.creature;
                break;
            }
        }
    }
    return chosen;
}

// Enough smell transforms bodies: a hot individual comes through as the species' worse self,
// re-rolled for the new form. The walk can chain, so the hottest rolls of a deep dig arrive
// two evolutions past the surface form.
std::size_t resolveForm(std::size_t idx, int& smell, int depth)
{
    while (!sCreatures[idx].evolves_into.empty() && sCreatures[idx].evolves_at_smell > 0 &&
           smell >= sCreatures[idx].evolves_at_smell)
    {
        const auto it = sCreatureIndex.find(sCreatures[idx].evolves_into);
        if (it == sCreatureIndex.end())
            break;
        idx = it->second;
        smell = rollSmell(depth);
    }
    return idx;
}

entt::entity emerge(EntityManager& em, float atX, float atY, int seepIndex, std::size_t kindIndex,
                    int depth)
{
    int smell = rollSmell(depth);
    kindIndex = resolveForm(kindIndex, smell, depth);
    const Creature& kind = sCreatures[kindIndex];

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

    const Derived d = derive(kind, smell, depth);
    reg.emplace<Health>(e, Health{d.hp, d.hp});
    reg.emplace<Vermin>(e, Vermin{d.contact, d.speed});
    // The resolved form's path, so an evolved individual goes on the record as what it became.
    reg.emplace<Species>(e, Species{kind.path});
    reg.emplace<Worth>(e, Worth{d.xp});
    reg.emplace<Smell>(e, Smell{smell});
    reg.emplace<Motion>(e, kind.motion);
    if (!kind.drops.empty())
        reg.emplace<DropTable>(e, DropTable{kind.drops});
    reg.emplace<Surge>(e, Surge{std::cos(angle), std::sin(angle), sSurgeSpeed,
                                sSurgeDuration * (0.7f + std::fmod(n, 7.0f) * 0.09f)});
    reg.emplace<SeepSource>(e, SeepSource{seepIndex});
    return e;
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

// Wind every hole's program back to its start. Called when a floor's assault
// is set up; not a way to make a floor happen twice, which is not a thing the
// descent does any more.
void restart()
{
    for (auto& seep : sSeeps)
    {
        seep.wave = 0;
        seep.to_emerge = 0;
        seep.owed = 0;
        seep.timer = seep.program.breath;
        seep.emerging = false;
        seep.done = seep.program.entries.empty();
    }
}

// Wind one hole's program forward past the creatures it has already lost. The
// program is a fixed sequence, so `killed` is simply how far into it he got.
//
// This moves the hole's PLACE in its program and never its clock: arriving on a
// floor is a fresh muster for every hole on it, so one carrying a remainder
// still waits its breath alongside the untouched ones. Waves count from one --
// the muster steps into the next before reading its size -- so `wave` is left
// pointing at the one BEFORE whatever is still owed.
void fastForward(ActiveSeep& seep, int killed)
{
    int left = killed;
    int wave = 1;
    for (; wave <= seep.program.waves; ++wave)
    {
        const int count = countForWave(seep.program, wave);
        if (left < count)
            break;
        left -= count;
    }
    if (wave > seep.program.waves)
    {
        seep.wave = seep.program.waves;
        seep.done = true;
        return;
    }
    seep.wave = wave - 1;
    seep.owed = countForWave(seep.program, wave) - left;
}

} // namespace

void begin(const std::string& configPath, const std::vector<Seep>& seeps, int depth,
           const std::vector<bool>& cleared, const std::vector<int>& killed,
           const std::vector<bool>& opened)
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
    sCurves = Curves{};
    sCurves.waves = base.value("waves", sCurves.waves);
    sCurves.per_wave = base.value("per_wave", sCurves.per_wave);
    sCurves.growth = base.value("growth", sCurves.growth);
    sCurves.spacing = base.value("spacing", sCurves.spacing);
    sCurves.breath = base.value("breath", sCurves.breath);
    sCurves.waves_per_depth = per.value("waves", sCurves.waves_per_depth);
    sCurves.per_wave_per_depth = per.value("per_wave", sCurves.per_wave_per_depth);
    sCurves.spacing_per_depth = per.value("spacing", sCurves.spacing_per_depth);
    sCurves.breath_per_depth = per.value("breath", sCurves.breath_per_depth);
    sBaseProgram = programAt(depth);
    (void)d;

    const auto& b = j.value("bestiary", nlohmann::json::object());
    const auto& bh = b.value("hp", nlohmann::json::object());
    sBestiary.hp.per_resistance = bh.value("per_resistance", sBestiary.hp.per_resistance);
    const auto& bc = b.value("contact", nlohmann::json::object());
    sBestiary.contact.per_point = bc.value("per_point", sBestiary.contact.per_point);
    sBestiary.contact.defensiveness_weight =
        bc.value("defensiveness_weight", sBestiary.contact.defensiveness_weight);
    sBestiary.contact.resistance_weight =
        bc.value("resistance_weight", sBestiary.contact.resistance_weight);
    const auto& bs = b.value("speed", nlohmann::json::object());
    sBestiary.speed.per_point = bs.value("per_point", sBestiary.speed.per_point);
    sBestiary.speed.dispersal_weight =
        bs.value("dispersal_weight", sBestiary.speed.dispersal_weight);
    sBestiary.speed.defensiveness_weight =
        bs.value("defensiveness_weight", sBestiary.speed.defensiveness_weight);
    const auto& bx = b.value("xp", nlohmann::json::object());
    sBestiary.xp.per_total = bx.value("per_total", sBestiary.xp.per_total);
    const auto& bm = b.value("smell", nlohmann::json::object());
    sBestiary.smell.base_min = bm.value("base_min", sBestiary.smell.base_min);
    sBestiary.smell.base_max = bm.value("base_max", sBestiary.smell.base_max);
    sBestiary.smell.min_per_depth = bm.value("min_per_depth", sBestiary.smell.min_per_depth);
    sBestiary.smell.max_per_depth = bm.value("max_per_depth", sBestiary.smell.max_per_depth);

    const auto& surge = j.value("surge", nlohmann::json::object());
    sSurgeSpeed = surge.value("speed", sSurgeSpeed);
    sSurgeDuration = surge.value("duration", sSurgeDuration);

    for (const auto& seep : seeps)
    {
        ActiveSeep active;
        active.at = seep;
        active.depth = seep.depth;
        active.program = loadSeepType(seep.type, seep.depth);
        sSeeps.push_back(std::move(active));
    }
    restart();
    sKilled.assign(sSeeps.size(), 0);
    for (std::size_t i = 0; i < sSeeps.size(); ++i)
        sSeeps[i].sealed_shut = i >= opened.size() || !opened[i];
    // What he has already taken out of each hole: the program resumes past it,
    // so a floor revisited is the floor he left rather than the floor he found.
    for (std::size_t i = 0; i < sSeeps.size() && i < killed.size(); ++i)
    {
        sKilled[i] = killed[i];
        fastForward(sSeeps[i], killed[i]);
    }
    // Spent holes stay spent across visits -- the source does not re-press a
    // hole whose assault it already exhausted.
    for (std::size_t i = 0; i < sSeeps.size() && i < cleared.size(); ++i)
        if (cleared[i])
            sSeeps[i].done = true;
    poe::log().info("swarm: {} seep(s) at depth {}", sSeeps.size(), depth);
    for (std::size_t i = 0; i < sSeeps.size(); ++i)
        poe::log().info("swarm:   seep[{}] at ({:.0f},{:.0f}) type '{}' ({} fauna)", i,
                        sSeeps[i].at.x, sSeeps[i].at.y, sSeeps[i].at.type,
                        sSeeps[i].program.entries.size());
}

bool seepCleared(const EntityManager& em, int seepIndex)
{
    if (seepIndex < 0 || seepIndex >= static_cast<int>(sSeeps.size()))
        return false;
    return sSeeps[static_cast<std::size_t>(seepIndex)].done && livingFrom(em, seepIndex) == 0;
}

void countKill(const EntityManager& em, entt::entity dead)
{
    const auto* source = em.registry().try_get<SeepSource>(dead);
    if (source == nullptr || source->index < 0)
        return; // belongs to no hole's program -- nothing to advance
    const auto i = static_cast<std::size_t>(source->index);
    if (i < sKilled.size())
        ++sKilled[i];
}

const std::vector<int>& progress()
{
    return sKilled;
}

entt::entity spawnOne(EntityManager& em, const std::string& creaturePath, float x, float y)
{
    const std::size_t idx = loadCreature(creaturePath);
    if (idx == static_cast<std::size_t>(-1))
        return entt::null;
    // Belongs to no hole ON THIS FLOOR -- what it costs is the floor below's, and the caller
    // is what says so.
    return emerge(em, x, y, -1, idx, sDepth);
}

void update(EntityManager& em, float dt)
{
    // Every hole on its own clock, but CLEARING gates each: a seep's breath toward its next
    // wave only runs while nothing it scheduled remains standing. Two holes therefore press in
    // staggered rhythm -- whichever yard you clear first starts gathering first.
    for (int i = 0; i < static_cast<int>(sSeeps.size()); ++i)
    {
        auto& seep = sSeeps[static_cast<std::size_t>(i)];
        if (seep.done || seep.sealed_shut)
            continue;
        if (seep.emerging)
        {
            seep.timer -= dt;
            while (seep.timer <= 0.0f && seep.to_emerge > 0)
            {
                if (const std::size_t kind = pickCreature(seep, sSpawnCounter);
                    kind != static_cast<std::size_t>(-1))
                {
                    emerge(em, seep.at.x, seep.at.y, i, kind, seep.depth);
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
        // Nothing left to send and nothing left standing: it is spent NOW. Running the breath
        // first would be a hole gathering itself for a wave that does not exist, and the player
        // waiting on a pause with nothing in it.
        if (seep.wave >= seep.program.waves)
        {
            seep.done = true;
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
        seep.to_emerge = seep.owed > 0 ? seep.owed : countForWave(seep.program, seep.wave);
        seep.owed = 0;
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

void retarget(int seepIndex, const Seep& to, int killed)
{
    if (seepIndex < 0 || seepIndex >= static_cast<int>(sSeeps.size()))
        return;
    ActiveSeep& seep = sSeeps[static_cast<std::size_t>(seepIndex)];
    if (to.type.empty())
    {
        seep.sealed_shut = true; // nothing left below it to carry
        return;
    }
    seep.at = to;
    seep.depth = to.depth;
    seep.program = loadSeepType(to.type, to.depth);
    seep.wave = 0;
    seep.to_emerge = 0;
    seep.owed = 0;
    seep.emerging = false;
    seep.sealed_shut = false;
    seep.done = seep.program.entries.empty();
    seep.timer = seep.program.breath;
    fastForward(seep, killed);
    if (static_cast<std::size_t>(seepIndex) < sKilled.size())
        sKilled[static_cast<std::size_t>(seepIndex)] = killed;
}

void wake(int seepIndex)
{
    if (seepIndex < 0 || seepIndex >= static_cast<int>(sSeeps.size()))
        return;
    auto& seep = sSeeps[static_cast<std::size_t>(seepIndex)];
    if (!seep.sealed_shut)
        return;
    seep.sealed_shut = false;
    // It presses after a breath, not on the same step he opened it on.
    seep.timer = seep.program.breath;
    poe::log().info("swarm: seep[{}] broken open", seepIndex);
}

// Is this hole sending anything, or could it be? A sealed one is neither.
bool seepSealed(int seepIndex)
{
    return seepIndex < 0 || seepIndex >= static_cast<int>(sSeeps.size()) ||
           sSeeps[static_cast<std::size_t>(seepIndex)].sealed_shut;
}

int seepWave(int seepIndex)
{
    if (seepIndex < 0 || seepIndex >= static_cast<int>(sSeeps.size()))
        return 0;
    return sSeeps[static_cast<std::size_t>(seepIndex)].wave;
}

int seepWaves(int seepIndex)
{
    if (seepIndex < 0 || seepIndex >= static_cast<int>(sSeeps.size()))
        return 0;
    return sSeeps[static_cast<std::size_t>(seepIndex)].program.waves;
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
