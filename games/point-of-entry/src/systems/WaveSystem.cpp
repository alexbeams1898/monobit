#include "systems/WaveSystem.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "formats/SpriteDefLoader.h"
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
struct PestKind
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
    std::string evolves_into; // pest file path; empty = terminal form
    int evolves_at_smell = 0;
};

// One line of a hole's fauna.
struct RosterEntry
{
    std::size_t pest = 0;
    int weight = 1;
    int min_wave = 1;
    // How deep before this one rides this hole at all. A species arrives at a DEPTH, which is
    // the law of depth applied to the field guide rather than to the numbers: what comes through
    // a crack near the surface and what comes through the same crack far down are different
    // animals, and the hole's file says where the line is.
    int min_depth = 0;
};

// A hole's program: its fauna and how its waves run. Fields default from the floor-wide
// assault curves; a hole file may override any of them.
struct Program
{
    std::vector<RosterEntry> entries;
    int waves = 3;
    int per_wave = 6;
    float growth = 1.4f;
    float spacing = 0.25f;
    float breath = 3.0f;
};

// One placed hole, running its program.
struct RunningHole
{
    Hole at;
    Program program;
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

// HOW A PEST'S SHEET BECOMES ITS NUMBERS -- the derivation constants, from swarm.json's
// formulas block. Not a guide of any kind: the FIELD GUIDE is the book he reads (ops/GuideOps),
// and this is arithmetic. Cross-feeds are deliberate: contact also reads body-weight
// (resistance, minor), speed also reads temper (defensiveness, minor).
struct Formulas
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

std::vector<PestKind> sPests;
std::unordered_map<std::string, std::size_t> sPestIndex;
std::unordered_map<std::string, Program> sHoleKinds;
std::vector<RunningHole> sHoles;
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

Program programAt(int depth)
{
    const auto d = static_cast<float>(depth);
    Program p;
    p.waves = sCurves.waves + static_cast<int>(sCurves.waves_per_depth * d);
    p.per_wave = sCurves.per_wave + static_cast<int>(sCurves.per_wave_per_depth * d);
    p.growth = sCurves.growth;
    p.spacing = std::max(0.05f, sCurves.spacing - sCurves.spacing_per_depth * d);
    p.breath = std::max(1.0f, sCurves.breath - sCurves.breath_per_depth * d);
    return p;
}
Formulas sFormulas;
std::mt19937 sRng{std::random_device{}()};
float sSurgeSpeed = 150.0f;
float sSurgeDuration = 0.35f;
unsigned sSpawnCounter = 0;

// Per hole: how many of its program have been killed. The one thing a visit
// leaves behind, and the reason walking out and back in is worth nothing.
std::vector<int> sKilled;

// Assault defaults at this depth, before any per-hole overrides.
Program sBaseProgram;

// THE SMELL: the law of depth on bodies, one roll per individual at emergence.
int rollSmell(int depth)
{
    const int lo = sFormulas.smell.base_min + sFormulas.smell.min_per_depth * depth;
    const int hi = sFormulas.smell.base_max + sFormulas.smell.max_per_depth * depth;
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

Derived derive(const PestKind& kind, int smell, int depth)
{
    const auto res = static_cast<float>(kind.sheet.resistance + kind.growth.resistance * depth);
    const auto dfn =
        static_cast<float>(kind.sheet.defensiveness + kind.growth.defensiveness * depth);
    const auto dsp = static_cast<float>(kind.sheet.dispersal + kind.growth.dispersal * depth);
    const float mul = 1.0f + static_cast<float>(smell) / 100.0f;
    Derived d;
    d.hp = std::max(
        1, static_cast<int>(std::lround(
               (static_cast<float>(kind.base_hp) + sFormulas.hp.per_resistance * res) * mul)));
    d.contact =
        kind.base_power *
        (1.0f + sFormulas.contact.per_point * (dfn * sFormulas.contact.defensiveness_weight +
                                               res * sFormulas.contact.resistance_weight)) *
        mul;
    d.speed = kind.base_speed *
              (1.0f + sFormulas.speed.per_point * (dsp * sFormulas.speed.dispersal_weight +
                                                   dfn * sFormulas.speed.defensiveness_weight)) *
              mul;
    const float total = res + dfn + dsp - 3.0f;
    d.xp =
        std::max(1, static_cast<int>(std::lround(static_cast<float>(kind.base_xp) *
                                                 (1.0f + sFormulas.xp.per_total * total) * mul)));
    return d;
}

std::size_t loadPest(const std::string& path)
{
    if (const auto it = sPestIndex.find(path); it != sPestIndex.end())
        return it->second;
    std::ifstream in(path);
    if (!in)
    {
        poe::log().error("swarm: no pest file at '{}'", path);
        return static_cast<std::size_t>(-1);
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().error("swarm: pest file '{}' is not valid JSON", path);
        return static_cast<std::size_t>(-1);
    }
    PestKind c;
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
    sPests.push_back(std::move(c));
    sPestIndex.emplace(path, sPests.size() - 1);
    // Resolve the evolved form up front, so a hot roll can swap without touching disk mid-wave.
    const std::size_t idx = sPests.size() - 1;
    if (!sPests[idx].evolves_into.empty())
        loadPest(sPests[idx].evolves_into);
    return idx;
}

Program loadHoleKind(const std::string& path, int depth)
{
    const std::string key = path + "@" + std::to_string(depth);
    if (const auto it = sHoleKinds.find(key); it != sHoleKinds.end())
        return it->second;
    Program program = programAt(depth);
    std::ifstream in(path);
    if (!in)
    {
        poe::log().error("swarm: no hole file at '{}' -- using floor defaults", path);
        sHoleKinds.emplace(key, program);
        return program;
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().error("swarm: hole file '{}' is not valid JSON", path);
        sHoleKinds.emplace(key, program);
        return program;
    }
    for (const auto& entry : j.value("pests", nlohmann::json::array()))
    {
        const std::size_t idx = loadPest(entry.value("pest", std::string{}));
        if (idx == static_cast<std::size_t>(-1))
            continue;
        program.entries.push_back(RosterEntry{idx, entry.value("weight", 1),
                                              entry.value("min_wave", 1),
                                              entry.value("min_depth", 0)});
    }
    // A hole may override any part of its wave program; unspecified fields keep the floor's.
    const auto& w = j.value("waves", nlohmann::json::object());
    program.waves = w.value("count", program.waves);
    program.per_wave = w.value("per_wave", program.per_wave);
    program.growth = w.value("growth", program.growth);
    program.spacing = w.value("spacing", program.spacing);
    program.breath = w.value("breath", program.breath);
    sHoleKinds.emplace(key, program);
    return program;
}

int countForWave(const Program& p, int wave)
{
    float n = static_cast<float>(p.per_wave);
    for (int i = 1; i < wave; ++i)
        n *= p.growth;
    return static_cast<int>(n);
}

// Which species surfaces from THIS hole: weighted round-robin among its fauna whose min_wave
// has come. Evolution is not decided here -- that belongs to the individual's roll.
std::size_t pickPest(const RunningHole& hole, unsigned n)
{
    if (hole.program.entries.empty())
        return static_cast<std::size_t>(-1);
    const auto& entries = hole.program.entries;
    const auto rides = [&](const RosterEntry& entry)
    { return hole.wave >= entry.min_wave && hole.depth >= entry.min_depth; };
    int totalWeight = 0;
    for (const auto& entry : entries)
        if (rides(entry))
            totalWeight += entry.weight;
    std::size_t chosen = entries.front().pest;
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
                chosen = entry.pest;
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
    while (!sPests[idx].evolves_into.empty() && sPests[idx].evolves_at_smell > 0 &&
           smell >= sPests[idx].evolves_at_smell)
    {
        const auto it = sPestIndex.find(sPests[idx].evolves_into);
        if (it == sPestIndex.end())
            break;
        idx = it->second;
        smell = rollSmell(depth);
    }
    return idx;
}

entt::entity emerge(EntityManager& em, float atX, float atY, int holeIndex, std::size_t kindIndex,
                    int depth)
{
    int smell = rollSmell(depth);
    kindIndex = resolveForm(kindIndex, smell, depth);
    const PestKind& kind = sPests[kindIndex];

    auto& reg = em.registry();
    const entt::entity e = reg.create();

    // Scatter INSIDE the pit; the crowd spreads on its own once it walks.
    const auto n = static_cast<float>(sSpawnCounter++);
    const float angle = n * 2.39996f; // golden angle: successive spawns never line up
    const float spread = 2.0f + std::fmod(n, 5.0f) * 1.5f;
    Transform t{};
    t.x = atX + std::cos(angle) * spread;
    t.y = atY + std::sin(angle) * spread;
    // Nothing materialises inside a wall; the hole itself is floor by generation's guarantee.
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
    reg.emplace<Pest>(e, Pest{d.contact, d.speed});
    // The resolved form's path, so an evolved individual goes on the record as what it became.
    reg.emplace<Species>(e, Species{kind.path});
    reg.emplace<Worth>(e, Worth{d.xp});
    reg.emplace<Smell>(e, Smell{smell});
    reg.emplace<Motion>(e, kind.motion);
    if (!kind.drops.empty())
        reg.emplace<DropTable>(e, DropTable{kind.drops});
    reg.emplace<Surge>(e, Surge{std::cos(angle), std::sin(angle), sSurgeSpeed,
                                sSurgeDuration * (0.7f + std::fmod(n, 7.0f) * 0.09f)});
    reg.emplace<FromHole>(e, FromHole{holeIndex});
    return e;
}

// Anything this hole scheduled still standing? Its next wave waits until the answer is no --
// CLEARING gates progress, per hole; the breath begins only over a quiet yard.
int livingFrom(const EntityManager& em, int holeIndex)
{
    int n = 0;
    for (auto [entity, pest, source] : em.registry().view<Pest, FromHole>().each())
        if (source.index == holeIndex && !em.registry().all_of<Dying>(entity))
            ++n;
    return n;
}

// Wind every hole's program back to its start. Called when a floor's assault
// is set up; not a way to make a floor happen twice, which is not a thing the
// descent does any more.
void restart()
{
    for (auto& hole : sHoles)
    {
        hole.wave = 0;
        hole.to_emerge = 0;
        hole.owed = 0;
        hole.timer = hole.program.breath;
        hole.emerging = false;
        hole.done = hole.program.entries.empty();
    }
}

// Wind one hole's program forward past the pests it has already lost. The
// program is a fixed sequence, so `killed` is simply how far into it he got.
//
// This moves the hole's PLACE in its program and never its clock: arriving on a
// floor is a fresh muster for every hole on it, so one carrying a remainder
// still waits its breath alongside the untouched ones. Waves count from one --
// the muster steps into the next before reading its size -- so `wave` is left
// pointing at the one BEFORE whatever is still owed.
void fastForward(RunningHole& hole, int killed)
{
    int left = killed;
    int wave = 1;
    for (; wave <= hole.program.waves; ++wave)
    {
        const int count = countForWave(hole.program, wave);
        if (left < count)
            break;
        left -= count;
    }
    if (wave > hole.program.waves)
    {
        hole.wave = hole.program.waves;
        hole.done = true;
        return;
    }
    hole.wave = wave - 1;
    hole.owed = countForWave(hole.program, wave) - left;
}

} // namespace

void begin(const std::string& configPath, const std::vector<Hole>& holes, int depth,
           const std::vector<bool>& cleared, const std::vector<int>& killed,
           const std::vector<bool>& opened)
{
    sDepth = depth;
    sPests.clear();
    sPestIndex.clear();
    sHoleKinds.clear();
    sHoles.clear();

    std::ifstream in(configPath);
    const nlohmann::json j =
        in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
    if (j.is_discarded() || !in)
        poe::log().warn("swarm: no usable config at '{}' -- using defaults", configPath);

    // The floor's base program: the assault curves, bent by depth. Deeper floors press harder
    // before any hole says a word -- the law again, applied to pacing.
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

    const auto& b = j.value("formulas", nlohmann::json::object());
    const auto& bh = b.value("hp", nlohmann::json::object());
    sFormulas.hp.per_resistance = bh.value("per_resistance", sFormulas.hp.per_resistance);
    const auto& bc = b.value("contact", nlohmann::json::object());
    sFormulas.contact.per_point = bc.value("per_point", sFormulas.contact.per_point);
    sFormulas.contact.defensiveness_weight =
        bc.value("defensiveness_weight", sFormulas.contact.defensiveness_weight);
    sFormulas.contact.resistance_weight =
        bc.value("resistance_weight", sFormulas.contact.resistance_weight);
    const auto& bs = b.value("speed", nlohmann::json::object());
    sFormulas.speed.per_point = bs.value("per_point", sFormulas.speed.per_point);
    sFormulas.speed.dispersal_weight =
        bs.value("dispersal_weight", sFormulas.speed.dispersal_weight);
    sFormulas.speed.defensiveness_weight =
        bs.value("defensiveness_weight", sFormulas.speed.defensiveness_weight);
    const auto& bx = b.value("xp", nlohmann::json::object());
    sFormulas.xp.per_total = bx.value("per_total", sFormulas.xp.per_total);
    const auto& bm = b.value("smell", nlohmann::json::object());
    sFormulas.smell.base_min = bm.value("base_min", sFormulas.smell.base_min);
    sFormulas.smell.base_max = bm.value("base_max", sFormulas.smell.base_max);
    sFormulas.smell.min_per_depth = bm.value("min_per_depth", sFormulas.smell.min_per_depth);
    sFormulas.smell.max_per_depth = bm.value("max_per_depth", sFormulas.smell.max_per_depth);

    const auto& surge = j.value("surge", nlohmann::json::object());
    sSurgeSpeed = surge.value("speed", sSurgeSpeed);
    sSurgeDuration = surge.value("duration", sSurgeDuration);

    for (const auto& hole : holes)
    {
        RunningHole active;
        active.at = hole;
        active.depth = hole.depth;
        active.program =
            loadHoleKind(hole.type, hole.depth == Hole::kThisFloor ? sDepth : hole.depth);
        sHoles.push_back(std::move(active));
    }
    restart();
    sKilled.assign(sHoles.size(), 0);
    for (std::size_t i = 0; i < sHoles.size(); ++i)
        sHoles[i].sealed_shut = i >= opened.size() || !opened[i];
    // What he has already taken out of each hole: the program resumes past it,
    // so a floor revisited is the floor he left rather than the floor he found.
    for (std::size_t i = 0; i < sHoles.size() && i < killed.size(); ++i)
    {
        sKilled[i] = killed[i];
        fastForward(sHoles[i], killed[i]);
    }
    // Spent holes stay spent across visits -- the source does not re-press a
    // hole whose assault it already exhausted.
    for (std::size_t i = 0; i < sHoles.size() && i < cleared.size(); ++i)
        if (cleared[i])
            sHoles[i].done = true;
    poe::log().info("swarm: {} hole(s) at depth {}", sHoles.size(), depth);
    for (std::size_t i = 0; i < sHoles.size(); ++i)
        poe::log().info("swarm:   hole[{}] at ({:.0f},{:.0f}) type '{}' ({} fauna)", i,
                        sHoles[i].at.x, sHoles[i].at.y, sHoles[i].at.type,
                        sHoles[i].program.entries.size());
}

bool holeCleared(const EntityManager& em, int holeIndex)
{
    if (holeIndex < 0 || holeIndex >= static_cast<int>(sHoles.size()))
        return false;
    return sHoles[static_cast<std::size_t>(holeIndex)].done && livingFrom(em, holeIndex) == 0;
}

void countKill(const EntityManager& em, entt::entity dead)
{
    const auto* source = em.registry().try_get<FromHole>(dead);
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

entt::entity spawnOne(EntityManager& em, const std::string& pestPath, float x, float y)
{
    const std::size_t idx = loadPest(pestPath);
    if (idx == static_cast<std::size_t>(-1))
        return entt::null;
    // Belongs to no hole ON THIS FLOOR -- what it costs is the floor below's, and the caller
    // is what says so.
    return emerge(em, x, y, -1, idx, sDepth);
}

// ONE HOLE, MID-WAVE: what it has left to send, sent on its own spacing. Pulled out of the
// tick because a wave emerging and a wave being SCHEDULED are two different things happening on
// two different clocks, and reading them interleaved is what made the tick hard to follow.
void pushOut(EntityManager& em, RunningHole& hole, int index, float dt)
{
    hole.timer -= dt;
    while (hole.timer <= 0.0f && hole.to_emerge > 0)
    {
        if (const std::size_t kind = pickPest(hole, sSpawnCounter);
            kind != static_cast<std::size_t>(-1))
        {
            emerge(em, hole.at.x, hole.at.y, index, kind, hole.depth);
            if (hole.to_emerge == countForWave(hole.program, hole.wave))
                poe::log().info("swarm:   hole[{}] first emergence at ({:.0f},{:.0f})", index,
                                hole.at.x, hole.at.y);
        }
        --hole.to_emerge;
        hole.timer += hole.program.spacing;
    }
    if (hole.to_emerge <= 0)
        hole.emerging = false;
}

void update(EntityManager& em, float dt)
{
    // Every hole on its own clock, but CLEARING gates each: a hole's breath toward its next
    // wave only runs while nothing it scheduled remains standing. Two holes therefore press in
    // staggered rhythm -- whichever yard you clear first starts gathering first.
    for (int i = 0; i < static_cast<int>(sHoles.size()); ++i)
    {
        auto& hole = sHoles[static_cast<std::size_t>(i)];
        if (hole.done || hole.sealed_shut)
            continue;
        if (hole.emerging)
        {
            pushOut(em, hole, i, dt);
            continue;
        }
        // Its output must die before its clock runs.
        if (livingFrom(em, i) > 0)
        {
            hole.timer = hole.program.breath;
            continue;
        }
        // Nothing left to send and nothing left standing: it is spent NOW. Running the breath
        // first would be a hole gathering itself for a wave that does not exist, and the player
        // waiting on a pause with nothing in it.
        if (hole.wave >= hole.program.waves)
        {
            hole.done = true;
            continue;
        }
        hole.timer -= dt;
        if (hole.timer > 0.0f)
            continue;
        ++hole.wave;
        if (hole.wave > hole.program.waves)
        {
            hole.done = true;
            continue;
        }
        hole.to_emerge = hole.owed > 0 ? hole.owed : countForWave(hole.program, hole.wave);
        hole.owed = 0;
        hole.emerging = true;
        hole.timer = 0.0f;
        poe::log().info("swarm: a hole begins wave {} of {} ({} of them)", hole.wave,
                        hole.program.waves, hole.to_emerge);
    }
}

Phase phase()
{
    if (sHoles.empty())
        return Phase::Quiet;
    bool allDone = true;
    bool anyEmerging = false;
    bool anyMidProgram = false;
    for (const auto& hole : sHoles)
    {
        if (!hole.done)
            allDone = false;
        if (hole.emerging)
            anyEmerging = true;
        if (!hole.done && hole.wave > 0)
            anyMidProgram = true;
    }
    if (allDone)
        return Phase::Cleared;
    if (anyEmerging)
        return Phase::Emerging;
    return anyMidProgram ? Phase::Fighting : Phase::Breath;
}

void retarget(int holeIndex, const Hole& to, int killed)
{
    if (holeIndex < 0 || holeIndex >= static_cast<int>(sHoles.size()))
        return;
    RunningHole& hole = sHoles[static_cast<std::size_t>(holeIndex)];
    if (to.type.empty())
    {
        hole.sealed_shut = true; // nothing left below it to carry
        return;
    }
    hole.at = to;
    hole.depth = to.depth;
    hole.program = loadHoleKind(to.type, to.depth == Hole::kThisFloor ? sDepth : to.depth);
    hole.wave = 0;
    hole.to_emerge = 0;
    hole.owed = 0;
    hole.emerging = false;
    hole.sealed_shut = false;
    hole.done = hole.program.entries.empty();
    hole.timer = hole.program.breath;
    fastForward(hole, killed);
    if (static_cast<std::size_t>(holeIndex) < sKilled.size())
        sKilled[static_cast<std::size_t>(holeIndex)] = killed;
}

void wake(int holeIndex)
{
    if (holeIndex < 0 || holeIndex >= static_cast<int>(sHoles.size()))
        return;
    auto& hole = sHoles[static_cast<std::size_t>(holeIndex)];
    if (!hole.sealed_shut)
        return;
    hole.sealed_shut = false;
    // It presses after a breath, not on the same step he opened it on.
    hole.timer = hole.program.breath;
    poe::log().info("swarm: hole[{}] broken open", holeIndex);
}

// Is this hole sending anything, or could it be? A sealed one is neither.
bool holeSealed(int holeIndex)
{
    return holeIndex < 0 || holeIndex >= static_cast<int>(sHoles.size()) ||
           sHoles[static_cast<std::size_t>(holeIndex)].sealed_shut;
}

int holeWave(int holeIndex)
{
    if (holeIndex < 0 || holeIndex >= static_cast<int>(sHoles.size()))
        return 0;
    return sHoles[static_cast<std::size_t>(holeIndex)].wave;
}

int holeWaves(int holeIndex)
{
    if (holeIndex < 0 || holeIndex >= static_cast<int>(sHoles.size()))
        return 0;
    return sHoles[static_cast<std::size_t>(holeIndex)].program.waves;
}

int remaining(const EntityManager& em)
{
    int n = 0;
    for (auto [entity, pest] : em.registry().view<Pest>().each())
        if (!em.registry().all_of<Dying>(entity))
            ++n;
    return n;
}

} // namespace swarm
