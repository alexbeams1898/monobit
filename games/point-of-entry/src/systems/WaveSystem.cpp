#include "systems/WaveSystem.h"

#include "SpriteDefLoader.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/LogUtils.h"
#include "ops/NavUtils.h"
#include "systems/PlayerSystem.h"
#include "systems/SpriteAnimSystem.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>

#include <entt/entt.hpp>

namespace swarm
{
namespace
{

Assault sAssault;
// The creature itself, from config: balance numbers, not code.
int sAntHealth = 7;
float sAntContact = 4.0f;
int sAntXp = 2;
float sAntScale = 1.0f;
float sSurgeSpeed = 150.0f;
float sSurgeDuration = 0.35f;
// The ant's drawing, loaded once per assault rather than once per ant -- a wave of seventy
// should not read the same json seventy times. ok=false means no art yet: boxes stand in.
sprite_def::Def sAntDef;
std::vector<Seep> sSeeps;
Phase sPhase = Phase::Quiet;
int sWave = 0;
int sToEmerge = 0;   // still to come up in the current wave
float sTimer = 0.0f; // until the next arrival, or until the next wave
unsigned sSpawnCounter = 0;

// How many come up in a given wave. Each is worse than the last, which is the whole shape of an
// assault -- a flat wave count reads as the floor running out of things rather than getting
// angrier.
int countForWave(const Assault& a, int wave)
{
    float n = static_cast<float>(a.per_wave);
    for (int i = 1; i < wave; ++i)
        n *= a.growth;
    return static_cast<int>(n);
}

// One ant. Deliberately almost nothing: a few pixels, some health, and the will to walk at you.
// The bestiary gets its variety from BEHAVIOUR and numbers, not from complicated individuals --
// a swarm is only affordable if each member is cheap.
void emerge(EntityManager& em, const Seep& seep)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();

    // Scatter INSIDE the pit. The hole's drawing is ~24px across, so the scatter stays within
    // its interior -- a creature surfacing on the blank floor beside the hole breaks the one
    // fiction a spawner has. The crowd spreads out on its own once it starts walking.
    const auto n = static_cast<float>(sSpawnCounter++);
    const float angle = n * 2.39996f; // golden angle: successive spawns never line up
    const float spread = 2.0f + std::fmod(n, 5.0f) * 1.5f;
    Transform t{};
    t.x = seep.x + std::cos(angle) * spread;
    t.y = seep.y + std::sin(angle) * spread;
    // Nothing materialises inside a wall. The scatter can reach past the seep's tile, and a
    // creature born on solid ground would stand in it unreachable forever -- a wave that cannot
    // be cleared. The seep itself is floor by generation's guarantee, so it is the safe fallback.
    if (!world::boxFree(em, t.x, t.y, 3.0f, 3.0f))
    {
        t.x = seep.x;
        t.y = seep.y;
    }
    t.scale = sAntScale;
    reg.emplace<Transform>(e, t);
    reg.emplace<PreviousTransform>(e, PreviousTransform{t.x, t.y});

    // A FEW PIXELS, drawn WIDER than tall: a crawling insect is a horizontal body with legs
    // under it, and a vertical bar of the same area reads as a paddle. Real art comes from the
    // pipeline like every other creature -- one drawing, facing right, mirrored; the flat box
    // only stands in while no drawing exists.
    Sprite spr{};
    spr.layer = 2;
    if (sAntDef.ok)
    {
        spr.texture_path = sAntDef.sheet;
        spr.src_w = sAntDef.frame_w;
        spr.src_h = sAntDef.frame_h;
        reg.emplace<Sprite>(e, spr);
    }
    else
    {
        spr.src_w = 5;
        spr.src_h = 3;
        reg.emplace<Sprite>(e, spr);
        reg.emplace<SolidColor>(e, SolidColor{0.14f, 0.11f, 0.10f});
    }
    // Faces the way it travels, through the same mirroring the player uses.
    reg.emplace<FacingDirection>(e, FacingDirection{});
    // Legs, if the art has them. A body's gait is code (WalkBob), but scattering legs cannot be
    // expressed by moving one drawing -- that is what timeline tags are for, and the mirrored
    // facing still applies to the animated frames.
    sprite_anim::attach(em, e, sAntDef);

    reg.emplace<Health>(e, Health{sAntHealth, sAntHealth});
    reg.emplace<Vermin>(e, Vermin{sAntContact});
    reg.emplace<Worth>(e, Worth{sAntXp});
    // The eruption: outward along this creature's own golden-angle bearing, with enough spread
    // in the timing that a wave reads as boiling out rather than detonating as one ring.
    reg.emplace<Surge>(e, Surge{std::cos(angle), std::sin(angle), sSurgeSpeed,
                                sSurgeDuration * (0.7f + std::fmod(n, 7.0f) * 0.09f)});
    reg.emplace<Velocity>(e, Velocity{});
}

} // namespace

void begin(const Assault& assault, const std::vector<Seep>& seeps)
{
    sAntDef = sprite_def::load("assets/sprites/ant.json");
    sAssault = assault;
    sSeeps = seeps;
    sWave = 0;
    sPhase = seeps.empty() ? Phase::Cleared : Phase::Breath;
    // The first wave gets a breath too: the space should be quiet for a moment after it opens,
    // long enough for the player to see where he is before it starts coming out of the walls.
    sTimer = assault.breath;
    sToEmerge = 0;
    if (seeps.empty())
        poe::log().warn("swarm: nothing seeps here -- the chamber is inert");
}

void restart()
{
    sWave = 0;
    sToEmerge = 0;
    sPhase = sSeeps.empty() ? Phase::Cleared : Phase::Breath;
    sTimer = sAssault.breath;
}

void update(EntityManager& em, float dt)
{
    if (sPhase == Phase::Quiet || sPhase == Phase::Cleared)
        return;

    sTimer -= dt;

    if (sPhase == Phase::Breath)
    {
        if (sTimer > 0.0f)
            return;
        ++sWave;
        if (sWave > sAssault.waves)
        {
            sPhase = Phase::Cleared;
            poe::log().info("swarm: the space is clear");
            return;
        }
        sToEmerge = countForWave(sAssault, sWave);
        sPhase = Phase::Emerging;
        sTimer = 0.0f;
        poe::log().info("swarm: wave {} of {}, {} of them", sWave, sAssault.waves, sToEmerge);
        return;
    }

    if (sPhase == Phase::Emerging)
    {
        // They come up a few at a time rather than appearing as a block -- a wave that arrives
        // all at once is a wall, where one that seeps is a rising tide.
        while (sTimer <= 0.0f && sToEmerge > 0)
        {
            emerge(em, sSeeps[static_cast<size_t>(sSpawnCounter) % sSeeps.size()]);
            --sToEmerge;
            sTimer += sAssault.spacing;
        }
        if (sToEmerge <= 0)
            sPhase = Phase::Fighting;
        return;
    }

    // Fighting: the wave is over when the last of it is dead. Counting the living rather than
    // counting kills means anything that removes one -- a hazard, a fall, a thing yet to exist --
    // is counted correctly without telling this system about it.
    if (remaining(em) == 0)
    {
        // The LAST wave ends the assault outright. Entering a breath first would have the state
        // machine gathering a wave that does not exist -- and announcing it.
        if (sWave >= sAssault.waves)
        {
            sPhase = Phase::Cleared;
            poe::log().info("swarm: the space is clear");
            return;
        }
        sPhase = Phase::Breath;
        sTimer = sAssault.breath;
    }
}

Phase phase()
{
    return sPhase;
}

int waveNumber()
{
    return sWave;
}

int totalWaves()
{
    return sAssault.waves;
}

int remaining(const EntityManager& em)
{
    int n = 0;
    for (auto [entity, vermin] : em.registry().view<Vermin>().each())
        if (!em.registry().all_of<Dying>(entity))
            ++n;
    return n;
}

Assault assaultForDepth(const std::string& configPath, int depth)
{
    Assault a;
    std::ifstream in(configPath);
    if (!in)
    {
        poe::log().warn("swarm: no config at '{}' -- using defaults", configPath);
        return a;
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().error("swarm: '{}' is not valid JSON", configPath);
        return a;
    }

    // One set of curves rather than a table per depth: a dig twenty chambers down should be
    // worse than the nineteenth without anyone having authored the nineteenth.
    const auto& base = j.value("base", nlohmann::json::object());
    const auto& per = j.value("per_depth", nlohmann::json::object());
    const auto& ant = j.value("ant", nlohmann::json::object());
    sAntHealth = ant.value("health", sAntHealth);
    sAntContact = ant.value("contact_damage", sAntContact);
    sAntXp = ant.value("xp", sAntXp);
    // Scale is a stopgap: fractional scaling makes pixels uneven, and the clean answer to "the
    // ant is too big" is a smaller drawing. The knob exists so size can be JUDGED before the
    // art is redrawn at whatever wins.
    sAntScale = ant.value("scale", sAntScale);
    const auto& surge = j.value("surge", nlohmann::json::object());
    sSurgeSpeed = surge.value("speed", sSurgeSpeed);
    sSurgeDuration = surge.value("duration", sSurgeDuration);
    const auto d = static_cast<float>(depth);
    a.waves = base.value("waves", 3) + static_cast<int>(per.value("waves", 0.34f) * d);
    a.per_wave = base.value("per_wave", 6) + static_cast<int>(per.value("per_wave", 1.5f) * d);
    a.growth = base.value("growth", 1.4f);
    a.spacing = std::max(0.05f, base.value("spacing", 0.25f) - per.value("spacing", 0.01f) * d);
    a.breath = std::max(1.0f, base.value("breath", 3.0f) - per.value("breath", 0.1f) * d);
    return a;
}

} // namespace swarm
