#include "Tools.h"

#include "Aim.h"
#include "Combat.h"
#include "HitArea.h"
#include "Log.h"
#include "Player.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

#include <entt/entt.hpp>

namespace tools
{
namespace
{
std::vector<Tool> sTools;
std::vector<float> sCooldowns; // time left before each tool is ready, parallel to sTools
int sSelected = 0;

StaminaTuning sStamina;

Reach parseReach(const std::string& text)
{
    return text == "thrown" ? Reach::Thrown : Reach::Adjacent;
}

} // namespace

// These are the seam where the exterminator's skills will eventually multiply his kit. They
// return the authored number for now; when stats exist, only these change.
float damageOf(const Tool& tool)
{
    return tool.damage;
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

const StaminaTuning& stamina()
{
    return sStamina;
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

    const auto& st = j.value("stamina", nlohmann::json::object());
    sStamina.max = st.value("max", 100.0f);
    sStamina.recovery_delay = st.value("recovery_delay", 0.8f);
    sStamina.recovery_rate = st.value("recovery_rate", 45.0f);

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
        tool.linger = t.value("linger", 0.0f);
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
    area.damage = damageOf(tool);
    area.owner = owner;
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
    // Visible while there is no art for it. A drawn area is also the only way to judge whether
    // its radius matches what the shot FEELS like it should cover.
    Sprite spr{};
    spr.src_w = static_cast<int>(area.radius * 2.0f);
    spr.src_h = static_cast<int>(area.radius * 2.0f);
    spr.layer = 3;
    spr.alpha = 0.4f;
    reg.emplace<Sprite>(e, spr);
    reg.emplace<SolidColor>(e, SolidColor{0.6f, 0.9f, 1.0f});
    reg.emplace<PreviousTransform>(e, PreviousTransform{t.x, t.y});

    reg.emplace<HitArea>(e, area);
}

} // namespace

void update(EntityManager& em, float dt)
{
    for (auto& cd : sCooldowns)
        if (cd > 0.0f)
            cd -= dt;

    if (sTools.empty() || !aim::firing())
        return;
    const auto index = static_cast<size_t>(sSelected);
    if (index >= sTools.size() || sCooldowns[index] > 0.0f)
        return;

    const entt::entity owner = player::entity();
    auto& reg = em.registry();
    if (!reg.valid(owner))
        return;

    // Stamina gates the shot, and an empty bar simply means not yet -- no penalty, no failed
    // swing. The cost is paid in full or the tool does not fire, so a shot is never half-priced.
    const Tool& tool = sTools[index];
    const float cost = staminaOf(tool);
    auto& sta = reg.get_or_emplace<Stamina>(owner, Stamina{sStamina.max, sStamina.max});
    if (sta.current < cost)
        return;
    sta.current -= cost;
    sta.recovery_timer = sStamina.recovery_delay;

    const auto& t = reg.get<Transform>(owner);
    spawnArea(em, owner, tool, t.x, t.y);
    sCooldowns[index] = cooldownOf(tool);
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
    sta.current = std::min(sta.max_stamina, sta.current + sStamina.recovery_rate * dt);
}

} // namespace tools
