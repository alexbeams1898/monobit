#include "Scene.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace scene
{
namespace
{
// The step parser, split by what the verb steers. Each helper returns true when
// its verb key was PRESENT (claiming the step); `ok` says whether it was valid.

// Body verbs -- an npc enters, leaves, moves, faces.
bool parseBodyStep(const nlohmann::json& j, Step& s, bool& ok)
{
    if (const auto it = j.find("enter"); it != j.end())
    {
        s.kind = Step::Kind::Enter;
        s.who = it->get<std::string>();
        s.target = j.value("at", std::string{});
        s.facing = j.value("facing", std::string{});
        ok = !s.who.empty() && !s.target.empty();
        return true;
    }
    if (const auto it = j.find("leave"); it != j.end())
    {
        s.kind = Step::Kind::Leave;
        s.who = it->get<std::string>();
        ok = !s.who.empty();
        return true;
    }
    if (const auto it = j.find("move"); it != j.end())
    {
        s.kind = Step::Kind::Move;
        s.who = it->get<std::string>();
        s.target = j.value("to", std::string{});
        ok = !s.who.empty() && !s.target.empty();
        return true;
    }
    if (const auto it = j.find("face"); it != j.end())
    {
        s.kind = Step::Kind::Face;
        s.who = it->get<std::string>();
        s.facing = j.value("dir", std::string{});
        ok = !s.who.empty() && !s.facing.empty();
        return true;
    }
    return false;
}

// Box verbs -- encounter content through the thought box, with pacing fields.
bool parseBoxStep(const nlohmann::json& j, Step& s, bool& ok)
{
    if (const auto it = j.find("observe"); it != j.end())
    {
        s.kind = Step::Kind::Observe;
        s.target = it->get<std::string>();
        s.blocking = j.value("blocking", true);
        ok = !s.target.empty();
        return true;
    }
    if (const auto it = j.find("remark"); it != j.end())
    {
        s.kind = Step::Kind::Remark;
        s.target = it->get<std::string>();
        s.blocking = j.value("blocking", true);
        ok = !s.target.empty();
        return true;
    }
    if (const auto it = j.find("menu"); it != j.end())
    {
        s.kind = Step::Kind::Menu;
        s.target = it->get<std::string>();
        s.blocking = j.value("blocking", true);
        s.must_choose = j.value("must_choose", false);
        // A must-choose menu IS the scene's pace: it repeats until a deed consumes
        // the spot, so "advance on first choice" would walk the scene away from a
        // decision the player is still locked inside. Coerce, loudly.
        if (s.must_choose && !s.blocking)
        {
            std::fprintf(stderr, "[scene] menu '%s': must_choose forces blocking\n",
                         s.target.c_str());
            s.blocking = true;
        }
        ok = !s.target.empty();
        return true;
    }
    return false;
}

// World verbs -- time, flags, sound, the screen.
bool parseWorldStep(const nlohmann::json& j, Step& s, bool& ok)
{
    if (const auto it = j.find("wait"); it != j.end())
    {
        s.kind = Step::Kind::Wait;
        s.seconds = it->get<float>();
        ok = s.seconds > 0.0f;
        return true;
    }
    if (const auto it = j.find("fade_in"); it != j.end())
    {
        s.kind = Step::Kind::FadeIn;
        s.seconds = it->get<float>();
        ok = s.seconds > 0.0f;
        return true;
    }
    // Both members ARE read, immediately below, through the structured binding in the loop.
    // Static analysis does not follow a binding back to the members it names and reads them as
    // dead; they are the table this function is built on.
    const struct
    {
        // cppcheck-suppress unusedStructMember
        const char* key;
        // cppcheck-suppress unusedStructMember
        Step::Kind kind;
    } named[] = {{"set_flag", Step::Kind::SetFlag},
                 {"sound", Step::Kind::Sound},
                 {"stop_sound", Step::Kind::StopSound}};
    for (const auto& [key, kind] : named)
        if (const auto it = j.find(key); it != j.end())
        {
            s.kind = kind;
            s.target = it->get<std::string>();
            ok = !s.target.empty();
            return true;
        }
    return false;
}

// One step from its JSON object: the first verb key found decides the kind.
bool parseStep(const nlohmann::json& j, Step& s)
{
    bool ok = false;
    if (parseBodyStep(j, s, ok) || parseBoxStep(j, s, ok) || parseWorldStep(j, s, ok))
        return ok;
    return false;
}
// One scene from its parsed file. False (with a log) when it cannot run: no
// level, no steps, or no completion flag -- the flag is REQUIRED because it is
// the once-guard, and how later scenes sequence after this one.
bool parseScene(const nlohmann::json& j, const std::string& fallback_id, Def& d)
{
    d.id = j.value("id", fallback_id);
    d.level = j.value("level", std::string{});
    d.set_flag = j.value("set_flag", std::string{});
    if (const auto it = j.find("start_when"); it != j.end())
        d.start_when = unlock::parseCondition(*it);
    for (const auto& js : j.value("steps", nlohmann::json::array()))
    {
        Step s;
        if (parseStep(js, s))
            d.steps.push_back(std::move(s));
        else
            std::fprintf(stderr, "[scene] '%s': unreadable step %s -- dropped\n", d.id.c_str(),
                         js.dump().c_str());
    }
    if (d.level.empty() || d.set_flag.empty() || d.steps.empty())
    {
        std::fprintf(stderr, "[scene] '%s' needs a level, a set_flag and steps -- dropped\n",
                     d.id.c_str());
        return false;
    }
    return true;
}
} // namespace

void load(Registry& reg, const std::string& dir)
{
    namespace fs = std::filesystem;
    reg.scenes.clear();
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
        Def d;
        if (parseScene(j, entry.path().stem().string(), d))
            reg.scenes.push_back(std::move(d));
    }
}

} // namespace scene
