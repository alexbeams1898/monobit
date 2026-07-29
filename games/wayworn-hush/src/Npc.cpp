#include "Npc.h"

#include "LdtkImport.h" // ldtk::facingVec -- the one authored-facing mapping
#include "WorldClock.h" // parseClockTime/inWindow -- the one reading of "HH:MM"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace npc
{
namespace
{
// One routine step from JSON: the first verb key found decides the kind.
bool parseRoutineStep(const nlohmann::json& j, RoutineStep& s)
{
    if (const auto it = j.find("wander"); it != j.end())
    {
        s.kind = RoutineStep::Kind::Wander;
        s.radius = it->get<float>();
        return s.radius > 0.0f;
    }
    if (const auto it = j.find("move_to"); it != j.end())
    {
        s.kind = RoutineStep::Kind::MoveTo;
        s.target = it->get<std::string>();
        return !s.target.empty();
    }
    if (const auto it = j.find("face"); it != j.end())
    {
        s.kind = RoutineStep::Kind::Face;
        s.target = it->get<std::string>();
        return !s.target.empty();
    }
    if (const auto it = j.find("anim"); it != j.end())
    {
        s.kind = RoutineStep::Kind::Anim;
        s.target = it->get<std::string>();
        return !s.target.empty();
    }
    if (const auto it = j.find("wait"); it != j.end())
    {
        s.kind = RoutineStep::Kind::Wait;
        if (it->is_array() && it->size() == 2)
        {
            s.wait_min = (*it)[0].get<float>();
            s.wait_max = (*it)[1].get<float>();
        }
        else if (it->is_number())
        {
            s.wait_min = s.wait_max = it->get<float>();
        }
        return s.wait_max >= s.wait_min && s.wait_max > 0.0f;
    }
    return false;
}

void parseRoutines(const nlohmann::json& j, const std::string& id, Config& c)
{
    const auto ro = j.find("routines");
    if (ro == j.end() || !ro->is_object())
        return;
    for (const auto& [name, steps] : ro->items())
        for (const auto& js : steps)
        {
            RoutineStep s;
            if (parseRoutineStep(js, s))
                c.routines[name].push_back(std::move(s));
            else
                std::fprintf(stderr, "[npc] '%s' routine '%s': unreadable step %s -- dropped\n",
                             id.c_str(), name.c_str(), js.dump().c_str());
        }
}

void parseSchedule(const nlohmann::json& j, const std::string& id, Config& c)
{
    for (const auto& e : j.value("schedule", nlohmann::json::array()))
    {
        ScheduleEntry entry;
        entry.routine = e.value("routine", std::string{});
        if (const auto w = e.find("when"); w != e.end() && w->is_object())
        {
            if (const auto f = w->find("from"); f != w->end())
                entry.from = worldclock::parseClockTime(f->get<std::string>());
            if (const auto t = w->find("to"); t != w->end())
                entry.to = worldclock::parseClockTime(t->get<std::string>());
            if (const auto u = w->find("unlock_when"); u != w->end())
                entry.when = unlock::parseCondition(*u);
        }
        if (entry.routine.empty() || entry.from < 0.0 || entry.to < 0.0 ||
            c.routines.count(entry.routine) == 0)
        {
            std::fprintf(stderr,
                         "[npc] '%s': schedule entry with a bad time or unknown "
                         "routine '%s' -- dropped\n",
                         id.c_str(), entry.routine.c_str());
            continue;
        }
        c.schedule.push_back(std::move(entry));
    }
}

// The npc's life, from config: named anim states, routines, the schedule.
void parseLife(const nlohmann::json& j, const std::string& id, Config& c)
{
    if (const auto an = j.find("anims"); an != j.end() && an->is_object())
        for (const auto& [name, a] : an->items())
            c.anims[name] =
                AnimState{a.value("row", 1), a.value("frames", 1), a.value("duration", 0.0f)};
    parseRoutines(j, id, c);
    parseSchedule(j, id, c);
}
Config parseConfig(const nlohmann::json& j, const std::string& fallback_id)
{
    Config c;
    c.id = j.value("id", fallback_id);
    c.name = j.value("name", c.id);
    c.texture = j.value("texture", std::string{});
    c.frame_width = j.value("frame_width", c.frame_width);
    c.frame_height = j.value("frame_height", c.frame_height);
    c.direction_count = j.value("direction_count", c.direction_count);
    c.max_frames_per_state = j.value("max_frames_per_state", c.max_frames_per_state);
    if (const auto idle = j.find("idle"); idle != j.end() && idle->is_object())
    {
        c.idle_row = idle->value("row", c.idle_row);
        c.idle_frames = idle->value("frames", c.idle_frames);
        c.idle_duration = idle->value("duration", c.idle_duration);
    }
    if (const auto walk = j.find("walk"); walk != j.end() && walk->is_object())
    {
        c.walk_row = walk->value("row", c.walk_row);
        c.walk_frames = walk->value("frames", c.walk_frames);
        c.walk_duration = walk->value("duration", c.walk_duration);
        c.walk_speed = walk->value("speed", c.walk_speed);
    }
    if (const auto col = j.find("collider"); col != j.end() && col->is_object())
    {
        c.collider_w = col->value("w", c.collider_w);
        c.collider_h = col->value("h", c.collider_h);
    }
    parseLife(j, c.id, c);
    return c;
}
} // namespace

const ScheduleEntry* activeEntry(const Config& cfg, double day_frac,
                                 const std::function<bool(const unlock::Condition&)>& gate)
{
    for (const auto& e : cfg.schedule)
        if (worldclock::inWindow(day_frac, e.from, e.to) && gate(e.when))
            return &e;
    return nullptr;
}

void load(Registry& reg, const std::string& dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        return;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        std::ifstream f(entry.path());
        if (!f)
            continue;
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object())
            continue;
        Config c = parseConfig(j, entry.path().stem().string());
        if (!c.id.empty() && !c.texture.empty())
            reg.npcs[c.id] = std::move(c);
    }
}

entt::entity spawn(EntityManager& em, const Config& cfg, float x, float y,
                   const std::string& facing)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();

    reg.emplace<Transform>(e, Transform{x, y});
    reg.emplace<PreviousTransform>(e, PreviousTransform{x, y});
    // Solid foot box: the player cannot walk through a person, and RenderSystem
    // Y-sorts by the feet (same rule as the player), so front/behind reads by
    // where each of them stands.
    reg.emplace<Collider>(e, Collider{cfg.collider_w, cfg.collider_h, true});

    Sprite spr{};
    spr.texture_path = cfg.texture;
    spr.src_w = cfg.frame_width;
    spr.src_h = cfg.frame_height;
    spr.layer = 2; // characters layer
    reg.emplace<Sprite>(e, spr);

    Animation anim{};
    anim.frame_width = cfg.frame_width;
    anim.frame_height = cfg.frame_height;
    anim.max_frames_per_state = cfg.max_frames_per_state;
    anim.direction_count = cfg.direction_count;
    anim.row_count = 2;
    anim.current_row = cfg.idle_row;
    anim.current_frames = cfg.idle_frames;
    anim.current_duration = cfg.idle_duration;
    reg.emplace<Animation>(e, anim);

    // The authored facing, through the same FacingDirection path the player uses --
    // AnimationSystem snaps the sprite's cardinal from it. Standing still, it never
    // changes until something (a future talk turn-to-face) writes it.
    float dx = 0.0f;
    float dy = 1.0f;
    ldtk::facingVec(facing, dx, dy);
    FacingDirection f{};
    f.dx = dx;
    f.dy = dy;
    f.render_dx = dx;
    f.render_dy = dy;
    reg.emplace<FacingDirection>(e, f);

    return e;
}

} // namespace npc
