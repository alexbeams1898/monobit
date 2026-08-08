#include "systems/CombatSystem.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/LogUtils.h"
#include "systems/AimSystem.h"
#include "systems/DamageSystem.h"
#include "systems/PlayerSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

#include <entt/entt.hpp>

namespace tools
{
namespace
{
std::vector<Tool> sTools;
std::vector<float> sCooldowns; // time left before each tool is ready, parallel to sTools
int sSelected = 0;

ChargeTuning sCharge;
entt::entity sStream = entt::null; // the live stream area, while a trigger is held
float sEmit = 0.0f;                // droplet emission accumulator
unsigned sDropSeed = 0;

// Where the wand's nozzle is, in front of him. Short: this places the ORIGIN of the spray, and
// the cone widens from there.
constexpr float kNozzle = 10.0f;
constexpr float kDropletsPerSecond = 90.0f;
// How fast the DAMAGING front sweeps out from the nozzle, as a fraction of the cone radius per
// second. Slightly behind the fastest droplets, so nothing ever dies where no spray has visibly
// arrived -- the kill rides the picture.
constexpr float kSprayFront = 1.6f;

Reach parseReach(const std::string& text)
{
    if (text == "thrown")
        return Reach::Thrown;
    if (text == "stream")
        return Reach::Stream;
    return Reach::Adjacent;
}

} // namespace

// These are the seam where the exterminator's skills will eventually multiply his kit. They
// return the authored number for now; when stats exist, only these change.
float damageOf(const Tool& tool, const Stats& holder)
{
    // The souls blend, one line: base * (1 + per_point * sum(grade * points-above-baseline)).
    // A tool with no grades ignores the sheet entirely.
    const float bonus = tool.scale_chemical * static_cast<float>(holder.chemical - 1) +
                        tool.scale_physical * static_cast<float>(holder.physical - 1) +
                        tool.scale_biological * static_cast<float>(holder.biological - 1);
    return tool.damage * (1.0f + stats::formulas().scaling.per_point * bonus);
}
float staminaOf(const Tool& tool)
{
    return tool.stamina;
}
float cooldownOf(const Tool& tool)
{
    return tool.cooldown;
}
float radiusOf(const Tool& tool)
{
    return tool.radius;
}

const ChargeTuning& chargeTuning()
{
    return sCharge;
}

void creditKill(EntityManager& em)
{
    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p) || !reg.all_of<Charge>(p))
        return;
    auto& c = reg.get<Charge>(p);
    c.current = std::min(c.max_charge, c.current + sCharge.per_kill);
}

bool load(const std::string& path)
{
    sTools.clear();
    sCooldowns.clear();
    std::ifstream in(path);
    if (!in)
    {
        poe::log().error("tools: no config at '{}' -- he is carrying nothing", path);
        return false;
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().error("tools: '{}' is not valid JSON", path);
        return false;
    }

    const auto& ch = j.value("charge", nlohmann::json::object());
    sCharge.max = ch.value("max", 100.0f);
    sCharge.per_kill = ch.value("per_kill", 6.0f);

    for (const auto& t : j.value("tools", nlohmann::json::array()))
    {
        Tool tool;
        tool.name = t.value("name", std::string{"?"});
        tool.reach = parseReach(t.value("reach", std::string{"adjacent"}));
        tool.radius = t.value("radius", 0.0f);
        tool.offset = t.value("offset", 0.0f);
        tool.range = t.value("range", 0.0f);
        tool.speed = t.value("speed", 0.0f);
        tool.damage = t.value("damage", 0.0f);
        tool.cooldown = t.value("cooldown", 0.0f);
        tool.stamina = t.value("stamina", 0.0f);
        tool.charge = t.value("charge", 0.0f);
        tool.arc = t.value("arc", 0.0f);
        tool.linger = t.value("linger", 0.0f);
        const auto& sc = t.value("scaling", nlohmann::json::object());
        tool.scale_chemical = sc.value("chemical", 0.0f);
        tool.scale_physical = sc.value("physical", 0.0f);
        tool.scale_biological = sc.value("biological", 0.0f);
        sTools.push_back(tool);
    }
    sCooldowns.assign(sTools.size(), 0.0f);
    poe::log().info("tools: {} in the kit", sTools.size());
    return !sTools.empty();
}

const std::vector<Tool>& all()
{
    return sTools;
}

int selected()
{
    return sSelected;
}

void select(int index)
{
    if (index >= 0 && index < static_cast<int>(sTools.size()))
        sSelected = index;
}

void next()
{
    if (!sTools.empty())
        sSelected = (sSelected + 1) % static_cast<int>(sTools.size());
}

bool streaming()
{
    return sStream != entt::null;
}

namespace
{

// Put an area into the world. Adjacent areas land beside him and sit still; thrown ones start
// close and travel. Both are the same component -- see HitArea.h.
void spawnArea(EntityManager& em, entt::entity owner, const Tool& tool, float px, float py)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    const float dx = aim::dirX();
    const float dy = aim::dirY();

    Transform t{};
    t.x = px + dx * tool.offset;
    t.y = py + dy * tool.offset;
    reg.emplace<Transform>(e, t);

    HitArea area;
    area.radius = radiusOf(tool);
    area.damage = damageOf(tool, reg.get_or_emplace<Stats>(owner, Stats{}));
    area.owner = owner;
    area.arc = tool.arc;
    area.dir_x = dx;
    area.dir_y = dy;
    // A one-shot area still has to live long enough to be resolved once, so an unauthored linger
    // becomes a single tick rather than an area that expires before it ever touches anything.
    area.remaining = tool.linger > 0.0f ? tool.linger : 0.0001f;
    if (tool.reach == Reach::Thrown)
    {
        area.dir_x = dx;
        area.dir_y = dy;
        area.speed = tool.speed;
        area.range_left = tool.range;
    }
    reg.emplace<PreviousTransform>(e, PreviousTransform{t.x, t.y});

    reg.emplace<HitArea>(e, area);
}

// One droplet of spray. Given a velocity within the cone's spread and left to travel, so what the
// player sees is chemical LEAVING the wand -- a cone drawn as a static shape reads as a stencil
// switching on and off, however correct its geometry is.
//
// They carry no HitArea: damage is the cone's job and these are only the look of it. Keeping the
// two apart means the droplets can be as scruffy and random as they like without any of it
// affecting what actually dies.
void spawnDroplet(EntityManager& em, float x, float y, float dx, float dy, const Tool& tool)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();

    // Cheap deterministic scatter. A real RNG here would be one more thing to seed and thread
    // through for a handful of visual jitter.
    const auto n = static_cast<float>(sDropSeed++);
    const float r1 = std::fmod(n * 0.6180339887f, 1.0f) * 2.0f - 1.0f; // angle within the spread
    const float r2 = std::fmod(n * 0.7548776662f, 1.0f);               // speed and life variance

    // Spread slightly INSIDE the damaging arc, so what you see is never wider than what kills --
    // a droplet drawn past the edge of the cone is a lie about reach.
    const float spread = tool.arc * 0.82f * 3.14159265f / 180.0f;
    const float a = std::atan2(dy, dx) + r1 * spread;
    const float speed = tool.radius * (1.5f + r2 * 0.9f);

    Transform t{};
    t.x = x;
    t.y = y;
    reg.emplace<Transform>(e, t);
    reg.emplace<PreviousTransform>(e, PreviousTransform{t.x, t.y});
    reg.emplace<Velocity>(e, Velocity{std::cos(a) * speed, std::sin(a) * speed});

    Sprite spr{};
    spr.src_w = 2;
    spr.src_h = 2;
    spr.layer = 3;
    reg.emplace<Sprite>(e, spr);
    reg.emplace<SolidColor>(e, SolidColor{0.78f, 0.95f, 0.62f});
    // GROWS as it travels and fades with age (the renderer dims a Particle by how far through its
    // life it is), which is what disperses -- a droplet that stayed the same size would read as a
    // bullet. Lifetime is set so it dies about where the cone stops hurting.
    Particle p;
    p.lifetime = (tool.radius / speed) * (0.85f + r2 * 0.3f);
    p.start_scale = 1.0f;
    p.end_scale = 2.6f;
    reg.emplace<Particle>(e, p);
}

// A held stream is ONE area that lives as long as the trigger does, following the aim. Spawning
// a fresh one every frame would be simpler and wrong: each new area starts with an empty hit list,
// so it would hurt everything in the cone every single frame regardless of the re-hit interval.
void tickStream(EntityManager& em, const Tool& tool, entt::entity owner, float dt)
{
    auto& reg = em.registry();
    auto& charge = reg.get_or_emplace<Charge>(owner, Charge{sCharge.max, sCharge.max});
    auto& sta = reg.get_or_emplace<Stamina>(owner, Stamina{});
    // Both meters gate a held stream, and they say different things: an empty tank means he has
    // not killed enough, an empty body means he has been leaning on the trigger too long.
    const bool wants = aim::firing() && charge.current > 0.0f && sta.current > 0.0f;

    if (!wants)
    {
        if (reg.valid(sStream))
            reg.destroy(sStream);
        sStream = entt::null;
        return;
    }

    charge.current = std::max(0.0f, charge.current - tool.charge * dt);
    sta.current = std::max(0.0f, sta.current - staminaOf(tool) * dt);
    sta.recovery_timer = stats::formulas().stamina.recovery_delay;

    const auto& pt = reg.get<Transform>(owner);
    const float dx = aim::dirX();
    const float dy = aim::dirY();
    if (!reg.valid(sStream))
    {
        sStream = reg.create();
        HitArea area;
        area.owner = owner;
        area.radius = radiusOf(tool);
        area.damage = damageOf(tool, reg.get_or_emplace<Stats>(owner, Stats{}));
        area.arc = tool.arc;
        // Damage lands on a tick rather than continuously, so a number appears at a readable rate
        // instead of once a frame.
        area.rehit = std::max(0.05f, tool.cooldown);
        area.expand = kSprayFront * radiusOf(tool);
        area.remaining = 3600.0f; // held areas are retired by releasing, not by expiring
        reg.emplace<HitArea>(sStream, area);
        reg.emplace<Transform>(sStream, Transform{});
        reg.emplace<PreviousTransform>(sStream, PreviousTransform{});
    }

    // Follow the wand every frame: the cone is anchored ON HIM and swings with the cursor, which
    // is what makes sweeping the crowd the actual skill.
    //
    // The apex sits at the player rather than out along the aim: spray leaves a nozzle and widens
    // as it travels, so a cone starting a body-length away reads as a floating wedge rather than
    // as something he is doing. Offset is the nozzle position only -- a short step, not a gap.
    auto& area = reg.get<HitArea>(sStream);
    area.dir_x = dx;
    area.dir_y = dy;
    auto& t = reg.get<Transform>(sStream);
    t.x = pt.x + dx * kNozzle;
    t.y = pt.y + dy * kNozzle;
    reg.get<PreviousTransform>(sStream) = PreviousTransform{t.x, t.y};

    // Droplets, thrown from the nozzle outward. The cone is the RULE; these are what the player
    // actually sees, and they are what makes it read as spewing rather than as a shape switching
    // on. Emitted on a rate rather than per frame, so the look does not change with framerate.
    sEmit += dt;
    const float interval = 1.0f / kDropletsPerSecond;
    while (sEmit >= interval)
    {
        sEmit -= interval;
        spawnDroplet(em, t.x, t.y, dx, dy, tool);
    }
}

} // namespace

void update(EntityManager& em, float dt)
{
    for (auto& cd : sCooldowns)
        if (cd > 0.0f)
            cd -= dt;

    const entt::entity owner = player::entity();
    auto& reg = em.registry();
    if (sTools.empty() || !reg.valid(owner))
        return;
    const auto index = static_cast<size_t>(sSelected);
    if (index >= sTools.size())
        return;
    const Tool& tool = sTools[index];

    // A stream is held rather than fired, so it runs its own path and stops here.
    if (tool.reach == Reach::Stream)
    {
        tickStream(em, tool, owner, dt);
        return;
    }
    if (reg.valid(sStream))
    {
        reg.destroy(sStream); // switched off a stream tool mid-spray
        sStream = entt::null;
    }

    if (!aim::firing() || sCooldowns[index] > 0.0f)
        return;

    // Stamina gates the shot, and an empty bar simply means not yet -- no penalty, no failed
    // swing. The cost is paid in full or the tool does not fire, so a shot is never half-priced.
    const float cost = staminaOf(tool);
    auto& sta = reg.get_or_emplace<Stamina>(owner, Stamina{});
    auto& charge = reg.get_or_emplace<Charge>(owner, Charge{sCharge.max, sCharge.max});
    if (sta.current < cost || charge.current < tool.charge)
        return;
    sta.current -= cost;
    sta.recovery_timer = stats::formulas().stamina.recovery_delay;
    charge.current -= tool.charge;

    const auto& t = reg.get<Transform>(owner);
    spawnArea(em, owner, tool, t.x, t.y);
    sCooldowns[index] = cooldownOf(tool);
}

void tickParticles(EntityManager& em, float dt)
{
    auto& reg = em.registry();
    std::vector<entt::entity> spent;
    for (auto [e, p, t, v] : reg.view<Particle, Transform, Velocity>().each())
    {
        p.age += dt;
        if (p.age >= p.lifetime)
        {
            spent.push_back(e);
            continue;
        }
        // Slows as it goes, like something sprayed into air rather than fired.
        const float drag = 1.0f - 2.2f * dt;
        v.dx *= drag;
        v.dy *= drag;
        t.x += v.dx * dt;
        t.y += v.dy * dt;
        const float k = p.age / p.lifetime;
        t.scale = p.start_scale + (p.end_scale - p.start_scale) * k;
    }
    for (const auto e : spent)
        reg.destroy(e);
}

void tickStamina(EntityManager& em, float dt)
{
    const entt::entity owner = player::entity();
    auto& reg = em.registry();
    if (!reg.valid(owner) || !reg.all_of<Stamina>(owner))
        return;
    auto& sta = reg.get<Stamina>(owner);

    // The delay resets on every spend, so sustained firing never regenerates -- which is what
    // makes the bar a decision about when to stop rather than a number that refills anyway.
    if (sta.recovery_timer > 0.0f)
    {
        sta.recovery_timer -= dt;
        return;
    }
    sta.current =
        std::min(sta.max_stamina, sta.current + stats::formulas().stamina.recovery_rate * dt);
}

} // namespace tools
