#include "systems/CombatSystem.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/FeelConfig.h"
#include "ecs/GameComponents.h"
#include "ops/LogUtils.h"
#include "ops/NavUtils.h"
#include "systems/AimSystem.h"
#include "systems/AudioSystem.h"
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

// THE HELD VOICE. A trigger that is down is ONE sound running, not a sound per frame -- started
// once, kept, and faded on release.
int sFiringVoice = -1;
// WAS THE TRIGGER DOWN LAST FRAME. The pull and the dribble belong to the TRIGGER, not to
// whether the wand can answer it: stamina never regenerates while firing, so holding past empty
// flickers "can fire" on and off every recovery delay, and hanging the clips on that machine-
// guns them -- dozens of one-shots a second, which buries every other sound in the game.
bool sTriggerHeld = false;
// ONE COUGH PER PULL. Running dry is an EVENT -- the moment the tank gave out -- and not a state
// to be announced for as long as he leans on the trigger. Repeating it turns a piece of
// information into a noise, and the wand holds only a few seconds, so a held trigger meant that
// noise forever.
bool sCoughedThisPull = false;
// How much spray -- in seconds of it -- he must have the breath for before the stream will pick
// back up. Enough to be a spray rather than a twitch.

// Silence the trigger, wherever the trigger stopped. Called on release AND on holster, because a
// stream can end without a release at all: a door, a death, switching tools. A looping voice
// that outlives its cause plays forever -- the same fault the holster comment warns about for
// the stream's own state.
void hushFiring()
{
    // Short. The dribble takes over at the level the body left off, so a long fade would lay two
    // near-identical noises over each other and they comb -- which is heard as a wobble, not as
    // a smooth handover. Just enough not to cut on a waveform.
    if (sFiringVoice >= 0)
        AudioSystem::stopSfx(sFiringVoice, 12);
    sFiringVoice = -1;
}

// Holstering ends the pull as well as the sound: a stream cut by a door or a death has had no
// release, and remembering the trigger as down would swallow the next real one.
void forgetTrigger()
{
    sTriggerHeld = false;
    sCoughedThisPull = false;
}

ChargeTuning sCharge;
float sEmit = 0.0f; // droplet emission accumulator
unsigned sDropSeed = 0;

// How the spray LOOKS -- emission, flight, dispersal. All of it presentation: damage rides the
// cone, not these. Loaded from the config's "spray" block; these are the fallbacks.
struct SprayTuning
{
    float droplets_per_second = 90.0f;
    // Droplet flight speed, as fractions of the cone radius per second: base plus per-droplet
    // variance.
    float speed_base = 1.5f;
    float speed_var = 0.9f;
    float spread_inside = 0.82f; // fraction of the damaging arc the droplets fan across
    float lifetime_base = 0.85f;
    float lifetime_var = 0.3f;
    float grow_to = 2.6f; // end scale as a droplet disperses
    float drag = 2.2f;    // per-second velocity decay in flight
};
SprayTuning sSpray;

// How fast the damaging front sweeps outward, DERIVED from the fastest droplet -- the kill
// rides the tip of the visible wave, and one number cannot drift from the other because there
// is only one number.
float sprayFront()
{
    return sSpray.speed_base + sSpray.speed_var;
}

// The live stream area, if any. Found by tag rather than held in a handle: a world swap
// destroys everything but him, and a handle would outlive its entity.
entt::entity streamEntity(entt::registry& reg)
{
    for (const auto e : reg.view<StreamHead>())
        return e;
    return entt::null;
}

Reach parseReach(const std::string& text)
{
    if (text == "thrown")
        return Reach::Thrown;
    if (text == "stream")
        return Reach::Stream;
    return Reach::Adjacent;
}

} // namespace

// The seam where the exterminator's skills multiply his kit. Everything that reads a
// tool's numbers goes through here, so the sheet is applied in exactly one place.
float damageOf(const Tool& tool, const Stats& holder)
{
    // The stat blend, one line: base * (1 + per_point * sum(grade * points-above-baseline)).
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
    const ChargeTuning chargeDefaults{};
    sCharge.max = ch.value("max", chargeDefaults.max);
    sCharge.per_kill = ch.value("per_kill", chargeDefaults.per_kill);

    const auto& sp = j.value("spray", nlohmann::json::object());
    const SprayTuning sprayDefaults{};
    sSpray.droplets_per_second = sp.value("droplets_per_second", sprayDefaults.droplets_per_second);
    sSpray.speed_base = sp.value("speed_base", sprayDefaults.speed_base);
    sSpray.speed_var = sp.value("speed_var", sprayDefaults.speed_var);
    sSpray.spread_inside = sp.value("spread_inside", sprayDefaults.spread_inside);
    sSpray.lifetime_base = sp.value("lifetime_base", sprayDefaults.lifetime_base);
    sSpray.lifetime_var = sp.value("lifetime_var", sprayDefaults.lifetime_var);
    sSpray.grow_to = sp.value("grow_to", sprayDefaults.grow_to);
    sSpray.drag = sp.value("drag", sprayDefaults.drag);

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
        const auto& sfx = t.value("sfx", nlohmann::json::object());
        tool.sfx_start = sfx.value("start", std::string{});
        tool.sfx_loop = sfx.value("loop", std::string{});
        tool.sfx_stop = sfx.value("stop", std::string{});
        tool.sfx_dry = sfx.value("dry", std::string{});
        tool.sfx_volume = sfx.value("volume", tool.sfx_volume);
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

bool streaming(EntityManager& em)
{
    return streamEntity(em.registry()) != entt::null;
}

int absorbWithGuard(EntityManager& em, int damage, bool guarding)
{
    if (!guarding || damage <= 0)
        return damage;
    auto& reg = em.registry();
    auto* sta = reg.try_get<Stamina>(player::entity());
    if (sta == nullptr)
        return damage;
    const float cost = static_cast<float>(damage) * stats::formulas().block.stamina_per_damage;
    sta->recovery_timer = stats::formulas().stamina.recovery_delay;
    if (sta->current >= cost)
    {
        sta->current -= cost;
        return 0;
    }
    // The guard BREAKS but the arm is still up: what stamina remains is
    // spent, and only a fraction of the hit gets through.
    sta->current = 0.0f;
    return std::max(1, static_cast<int>(std::lround(static_cast<float>(damage) *
                                                    stats::formulas().block.broken_factor)));
}

void holster(EntityManager& em)
{
    auto& reg = em.registry();
    for (const auto e : reg.view<StreamHead>())
        reg.destroy(e);
    hushFiring();
    forgetTrigger();
}

namespace
{

// The fields every area shares, whatever its reach. Requires a Stats sheet on the owner --
// every fire path bails before calling this.
HitArea makeArea(entt::registry& reg, const Tool& tool, entt::entity owner)
{
    HitArea area;
    area.owner = owner;
    area.radius = radiusOf(tool);
    area.damage = damageOf(tool, reg.get<Stats>(owner));
    area.arc = tool.arc;
    return area;
}

// Put an area into the world. Adjacent areas land beside him and sit still; thrown ones start
// close and travel. Both are the same component -- see HitArea.h.
void spawnArea(EntityManager& em, entt::entity owner, const Tool& tool, float px, float py)
{
    auto& reg = em.registry();
    if (reg.try_get<Stats>(owner) == nullptr)
    {
        poe::log().error("combat: firing owner has no stats sheet -- shot dropped");
        return;
    }
    const entt::entity e = reg.create();
    const float dx = aim::dirX();
    const float dy = aim::dirY();

    Transform t{};
    t.x = px + dx * tool.offset;
    t.y = py + dy * tool.offset;
    reg.emplace<Transform>(e, t);

    HitArea area = makeArea(reg, tool, owner);
    area.dir_x = dx;
    area.dir_y = dy;
    // A one-shot area still has to live long enough to be resolved once, so an unauthored linger
    // becomes a single tick rather than an area that expires before it ever touches anything.
    area.remaining = tool.linger > 0.0f ? tool.linger : 0.0001f;
    if (tool.reach == Reach::Thrown)
    {
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
    const float spread = geom::degToRad(tool.arc * sSpray.spread_inside);
    const float a = std::atan2(dy, dx) + r1 * spread;
    const float speed = tool.radius * (sSpray.speed_base + r2 * sSpray.speed_var);

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
    // Pale and cold: what he sprays is a clean chemical, and it should never be mistaken for
    // what comes out of the holes.
    reg.emplace<SolidColor>(e, SolidColor{0.80f, 0.93f, 0.98f});
    // GROWS as it travels and fades with age (the renderer dims a Particle by how far through its
    // life it is), which is what disperses -- a droplet that stayed the same size would read as a
    // bullet. Lifetime is set so it dies about where the cone stops hurting.
    Particle p;
    p.lifetime = (tool.radius / speed) * (sSpray.lifetime_base + r2 * sSpray.lifetime_var);
    p.start_scale = 1.0f;
    p.end_scale = sSpray.grow_to;
    reg.emplace<Particle>(e, p);
}

// A held stream is ONE area that lives as long as the trigger does, following the aim. Spawning
// a fresh one every frame would be simpler and wrong: each new area starts with an empty hit list,
// so it would hurt everything in the cone every single frame regardless of the re-hit interval.
// THE TRIGGER CAME UP, or the tank ran dry under it. Release DETACHES the burst rather than
// cutting it: the front finishes its sweep to the rim and the area retires itself. A click is a
// complete fire; chemical does not vanish mid-air on mouse-up.
void releaseStream(entt::registry& reg, entt::entity stream, const Tool& tool, float charge,
                   bool triggerHeld)
{
    if (reg.valid(stream))
    {
        auto& area = reg.get<HitArea>(stream);
        const float total = area.expand > 0.0f ? area.radius / area.expand : 0.0f;
        area.remaining = std::max(0.05f, total - area.age);
        reg.remove<StreamHead>(stream); // a detached burst is no longer the held stream
    }
    // The body stops either way -- nothing is coming out. The DRIBBLE only plays if he actually
    // let go: running dry under a held trigger is not a release, and sounding one would be the
    // wand finishing a spray it never stopped making.
    const bool wasFiring = sFiringVoice >= 0;
    hushFiring();
    if (wasFiring && !triggerHeld && !tool.sfx_stop.empty())
        AudioSystem::playSfx(tool.sfx_stop, tool.sfx_volume);
    // THE COUGH: the tank gave out, or he pulled on one already empty. Said once, then not
    // again until he lets go -- holding the trigger down is not new information.
    if (triggerHeld && charge <= 0.0f && !sCoughedThisPull && !tool.sfx_dry.empty())
    {
        AudioSystem::playSfx(tool.sfx_dry, tool.sfx_volume);
        sCoughedThisPull = true;
    }
    if (!triggerHeld)
        sCoughedThisPull = false;
}

void tickStream(EntityManager& em, const Tool& tool, entt::entity owner, float dt)
{
    auto& reg = em.registry();
    auto* charge = reg.try_get<Charge>(owner);
    auto* sta = reg.try_get<Stamina>(owner);
    if (charge == nullptr || sta == nullptr || reg.try_get<Stats>(owner) == nullptr)
    {
        poe::log().error("combat: stream owner is missing charge, stamina or stats -- not firing");
        return;
    }
    entt::entity stream = streamEntity(reg);
    // Both meters gate a held stream, and they say different things: an empty tank means he has
    // not killed enough, an empty body means he has been leaning on the trigger too long.
    //
    // TWO DIFFERENT FACTS also: whether he is PULLING, and whether the wand can answer. The
    // spray follows the second; the pull and the dribble follow the first.
    const bool triggerHeld = aim::firing() && !aim::guarding();
    // A SLIVER OF STAMINA IS NOT A SPRAY. The cost is per SECOND, so "more than zero" buys a
    // millisecond of it -- and the recovery delay resets on every spend, so a trigger held past
    // empty regenerates one frame's worth, spends it, and does that forever: the spray stutters
    // and the sound stutters with it. STARTING again therefore asks for enough to be worth
    // starting, while continuing only asks for something left. He has to ease off, which is what
    // running out of breath ought to mean.
    const bool bodyWilling =
        sta->current >
        (reg.valid(stream) ? 0.0f : tool.stamina * feel::current().wand.resume_seconds);
    const bool wants = triggerHeld && charge->current > 0.0f && bodyWilling;
    if (!wants)
    {
        releaseStream(reg, stream, tool, charge->current, triggerHeld);
        sTriggerHeld = triggerHeld;
        return;
    }
    const bool freshPull = !sTriggerHeld;
    sTriggerHeld = triggerHeld;
    if (sFiringVoice < 0 && !tool.sfx_loop.empty())
    {
        // The pull first, then the body under it. The body carries no attack of its own -- that
        // is what makes it loop without swelling -- so the start clip is what gives it one.
        // Only a fresh pull gets the attack. Picking the spray back up after the body caught
        // its breath is the same spray continuing, not a new one.
        if (freshPull && !tool.sfx_start.empty())
            AudioSystem::playSfx(tool.sfx_start, tool.sfx_volume);
        sFiringVoice = AudioSystem::playSfxTracked(tool.sfx_loop, tool.sfx_volume, 1.0f,
                                                   /*loop=*/true, /*fade_in_ms=*/0);
    }

    charge->current = std::max(0.0f, charge->current - tool.charge * dt);
    sta->current = std::max(0.0f, sta->current - staminaOf(tool) * dt);
    sta->recovery_timer = stats::formulas().stamina.recovery_delay;

    const auto& pt = reg.get<Transform>(owner);
    const float dx = aim::dirX();
    const float dy = aim::dirY();
    if (!reg.valid(stream))
    {
        stream = reg.create();
        HitArea area = makeArea(reg, tool, owner);
        // Damage lands on a tick rather than continuously, so a number appears at a readable rate
        // instead of once a frame.
        area.rehit = std::max(0.05f, tool.cooldown);
        area.expand = sprayFront() * radiusOf(tool);
        area.remaining = 3600.0f; // held areas are retired by releasing, not by expiring
        reg.emplace<HitArea>(stream, area);
        reg.emplace<Transform>(stream, Transform{});
        reg.emplace<PreviousTransform>(stream, PreviousTransform{});
        reg.emplace<StreamHead>(stream);
    }

    // Follow the wand every frame: the cone is anchored ON HIM and swings with the cursor, which
    // is what makes sweeping the crowd the actual skill.
    //
    // The apex sits at the player rather than out along the aim: spray leaves a nozzle and widens
    // as it travels, so a cone starting a body-length away reads as a floating wedge rather than
    // as something he is doing. Offset is the nozzle position only -- a short step, not a gap.
    auto& area = reg.get<HitArea>(stream);
    area.dir_x = dx;
    area.dir_y = dy;
    auto& t = reg.get<Transform>(stream);
    t.x = pt.x + dx * tool.offset;
    t.y = pt.y + dy * tool.offset;
    reg.get<PreviousTransform>(stream) = PreviousTransform{t.x, t.y};

    // Droplets, thrown from the nozzle outward. The cone is the RULE; these are what the player
    // actually sees, and they are what makes it read as spewing rather than as a shape switching
    // on. Emitted on a rate rather than per frame, so the look does not change with framerate.
    sEmit += dt;
    const float interval = 1.0f / sSpray.droplets_per_second;
    while (sEmit >= interval)
    {
        sEmit -= interval;
        spawnDroplet(em, t.x, t.y, dx, dy, tool);
    }
}

} // namespace

// HIS HANDS. What a man does when the tank runs dry: not a weapon, a way out of the room. Uses
// the same area the tools do, so a punch hits, marks and reads exactly like everything else --
// it is simply weaker, shorter and slower, and costs no chemical because it is not chemistry.
float sFistCooldown = 0.0f;

bool swing(EntityManager& em, entt::entity owner)
{
    auto& reg = em.registry();
    const auto& fists = stats::formulas().fists;
    auto* sta = reg.try_get<Stamina>(owner);
    const auto* sheet = reg.try_get<Stats>(owner);
    if (sta == nullptr || sheet == nullptr || sFistCooldown > 0.0f || sta->current < fists.stamina)
        return false;

    sta->current -= fists.stamina;
    sta->recovery_timer = stats::formulas().stamina.recovery_delay;
    sFistCooldown = fists.cooldown;

    const auto& at = reg.get<Transform>(owner);
    const float dx = aim::dirX();
    const float dy = aim::dirY();
    const entt::entity e = reg.create();
    Transform t{};
    t.x = at.x + dx * fists.reach * 0.5f;
    t.y = at.y + dy * fists.reach * 0.5f;
    reg.emplace<Transform>(e, t);
    // Everything a swept area needs, the same as a tool's: WHERE IT FACES, because the arc is a
    // cone measured off dir and a cone off (0,0) covers nothing; and a previous position,
    // because the damage pass sweeps between frames rather than testing a point.
    reg.emplace<PreviousTransform>(e, PreviousTransform{t.x, t.y});
    HitArea area;
    area.owner = owner;
    area.radius = fists.reach;
    area.damage = fists.base + fists.per_physical * static_cast<float>(sheet->physical);
    area.arc = fists.arc; // DEGREES off dir, like every other area
    area.dir_x = dx;
    area.dir_y = dy;
    area.remaining = 0.0001f; // one swing, not a lingering cloud
    reg.emplace<HitArea>(e, area);
    return true;
}

void update(EntityManager& em, float dt)
{
    for (auto& cd : sCooldowns)
        if (cd > 0.0f)
            cd -= dt;
    // Ticked here rather than inside the swing, so it runs down while he is backing away as
    // well as while he is standing his ground.
    sFistCooldown = std::max(0.0f, sFistCooldown - dt);

    const entt::entity owner = player::entity();
    auto& reg = em.registry();
    if (sTools.empty() || !reg.valid(owner))
        return;
    const auto index = static_cast<size_t>(sSelected);
    if (index >= sTools.size())
        return;
    const Tool& tool = sTools[index];

    // A stream is held rather than fired, so it runs its own path and stops here -- unless the
    // tank is dry, in which case holding the trigger is a man hitting things instead.
    if (tool.reach == Reach::Stream)
    {
        const auto* charge = reg.try_get<Charge>(owner);
        if (charge != nullptr && charge->current <= 0.0f && aim::firing() && !aim::guarding())
        {
            holster(em);
            swing(em, owner);
            return;
        }
        tickStream(em, tool, owner, dt);
        return;
    }
    holster(em); // switched off a stream tool mid-spray

    if (!aim::firing() || aim::guarding() || sCooldowns[index] > 0.0f)
        return;

    // Stamina gates the shot, and an empty bar simply means not yet -- no penalty, no failed
    // swing. The cost is paid in full or the tool does not fire, so a shot is never half-priced.
    const float cost = staminaOf(tool);
    auto* sta = reg.try_get<Stamina>(owner);
    auto* charge = reg.try_get<Charge>(owner);
    if (sta == nullptr || charge == nullptr)
    {
        poe::log().error("combat: firing owner is missing stamina or charge -- shot dropped");
        return;
    }
    if (charge->current < tool.charge)
    {
        // Dry: he still has hands. Not a mode he switches to -- the trigger simply means
        // something else once the tank cannot answer it.
        swing(em, owner);
        return;
    }
    if (sta->current < cost)
        return;
    sta->current -= cost;
    sta->recovery_timer = stats::formulas().stamina.recovery_delay;
    charge->current -= tool.charge;

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
        const float drag = 1.0f - sSpray.drag * dt;
        v.dx *= drag;
        v.dy *= drag;
        t.x += v.dx * dt;
        t.y += v.dy * dt;
        const float k = p.age / p.lifetime;
        t.scale = p.start_scale + (p.end_scale - p.start_scale) * k;
        // Thinning out is what makes a hard-edged particle read as something airborne. Held
        // full for the first stretch so it has presence, then away.
        if (auto* spr = reg.try_get<Sprite>(e))
            spr->alpha = k < 0.35f ? 1.0f : 1.0f - (k - 0.35f) / 0.65f;
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
