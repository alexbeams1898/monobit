#include "systems/TintSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ops/LogUtils.h"
#include "systems/PlayerSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace tint
{
namespace
{

struct Tuning
{
    // How long a body stays lit after being struck. A kill holds longer and stays on screen for
    // it, so the killing blow is not the one hit that never flashes.
    float flash = 0.12f;
    float death_flash = 0.22f;
    // Where the wound starts to show, as a fraction of full health. Above this he looks fine --
    // a bar that reddens from the first scratch says nothing, because it is always saying it.
    float wound_from = 0.6f;
    // How far the green and blue are pulled out at the bottom of the ladder. 1 is fully gone.
    float wound_depth = 0.85f;
    // The red is pushed ABOVE full, so a wounded body reads as lit rather than merely darkened.
    float wound_red = 1.2f;
};

Tuning sT;

} // namespace

bool load(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        poe::log().warn("tint: no config at '{}' -- using built-in feedback", path);
        return false;
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().error("tint: '{}' is not valid JSON -- using built-in feedback", path);
        return false;
    }
    const auto& f = j.value("feedback", nlohmann::json::object());
    sT.flash = f.value("flash", sT.flash);
    sT.death_flash = f.value("death_flash", sT.death_flash);
    sT.wound_from = f.value("wound_from", sT.wound_from);
    sT.wound_depth = f.value("wound_depth", sT.wound_depth);
    sT.wound_red = f.value("wound_red", sT.wound_red);
    return true;
}

float flashSeconds(bool fatal)
{
    return fatal ? sT.death_flash : sT.flash;
}

void forget(EntityManager& em)
{
    em.registry().clear<TintOverride>();
}

void update(EntityManager& em)
{
    auto& reg = em.registry();
    // THE POOL IS EMPTIED FIRST. Everything below re-states what is true this frame, so nothing
    // has to remember to take its own tint away.
    reg.clear<TintOverride>();

    // BEING STRUCK, the loudest thing that can be true of a body.
    for (auto [entity, flash] : reg.view<HitFlash>().each())
    {
        if (reg.all_of<Dying>(entity))
        {
            // Blown out toward white and fading with the corpse, so a kill reads as a thing
            // going out rather than a thing being struck.
            const float t = sT.death_flash > 0.0f ? flash.remaining / sT.death_flash : 0.0f;
            reg.emplace<TintOverride>(entity, TintOverride{6.0f, 5.0f * t + 1.0f, 3.0f * t + 1.0f});
        }
        else
            reg.emplace<TintOverride>(entity, TintOverride{2.5f, 2.5f, 2.5f});
    }

    // WOUNDED. Continuous, and read off health rather than off any record of damage -- healing
    // takes the colour back out by itself, with nothing anywhere having to undo it.
    const entt::entity him = player::entity();
    if (!reg.valid(him) || reg.all_of<TintOverride>(him))
        return;
    const auto* hp = reg.try_get<Health>(him);
    if (hp == nullptr || hp->max <= 0 || sT.wound_from <= 0.0f)
        return;
    const float left = static_cast<float>(hp->current) / static_cast<float>(hp->max);
    if (left >= sT.wound_from)
        return;
    // Clamped because health can sit below zero for the frame between the killing blow and the
    // death beat taking over, and a negative channel is a colour nothing can draw.
    const float hurt = std::min(1.0f, 1.0f - left / sT.wound_from);
    const float rest = std::max(0.0f, 1.0f - hurt * sT.wound_depth);
    reg.emplace<TintOverride>(him, TintOverride{sT.wound_red, rest, rest});
}

} // namespace tint
