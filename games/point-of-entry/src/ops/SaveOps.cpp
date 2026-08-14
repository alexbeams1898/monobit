#include "ops/SaveOps.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/ItemConfig.h"
#include "ops/LogUtils.h"
#include "ops/RecordOps.h"
#include "systems/CombatSystem.h"
#include "systems/DescentSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/ThermosSystem.h"
#include "systems/TravelSystem.h"

#include <algorithm>

#include <entt/entt.hpp>

namespace save_ops
{
namespace
{

// The one life in the file. A list that holds exactly one today; found rather
// than assumed, so the day it holds two nothing here has to change.
savegame::Data* only(savegame::File& file)
{
    return file.lives.empty() ? nullptr : &file.lives.front();
}

savegame::Man readMan(const EntityManager& em)
{
    savegame::Man man;
    man.thermos_fill = thermos::fillIndex();
    man.thermos_sips = thermos::sipsLeft();
    man.tool = tools::selected();

    const auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return man;
    if (const auto* s = reg.try_get<Stats>(p))
    {
        man.chemical = s->chemical;
        man.physical = s->physical;
        man.biological = s->biological;
        man.endurance = s->endurance;
        man.inspection = s->inspection;
    }
    if (const auto* e = reg.try_get<Earnings>(p))
        man.banked = e->banked;
    if (const auto* h = reg.try_get<Health>(p))
        man.health = h->current;
    if (const auto* c = reg.try_get<Charge>(p))
        man.charge = c->current;
    if (const auto* sat = reg.try_get<Satchel>(p))
        for (const auto& held : sat->items)
            man.satchel.push_back(
                savegame::Item{held.item, static_cast<int>(held.quality), held.count});
    return man;
}

void writeMan(EntityManager& em, const savegame::Man& man)
{
    thermos::restore(man.thermos_fill, man.thermos_sips);
    tools::select(man.tool);

    auto& reg = em.registry();
    const entt::entity p = player::entity();
    if (!reg.valid(p))
        return;
    if (auto* s = reg.try_get<Stats>(p))
    {
        s->chemical = man.chemical;
        s->physical = man.physical;
        s->biological = man.biological;
        s->endurance = man.endurance;
        s->inspection = man.inspection;
        // The sheet decides hp, stamina and defense; putting the points back
        // without re-deriving would leave him with a beginner's body.
        stats::applyDerivations(em, p);
    }
    reg.get_or_emplace<Earnings>(p).banked = man.banked;
    // After the derivations, which set him to a full tank off the sheet: a man
    // who quit hurt comes back hurt, and only the thermos or a rest mends him.
    if (man.health > 0)
        if (auto* h = reg.try_get<Health>(p))
            h->current = std::min(man.health, h->max);
    // The tank likewise: a man who quit on an empty wand comes back to an empty wand. Refilling
    // it overnight would make quitting the cheapest reload in the game.
    if (man.charge >= 0.0f)
        if (auto* c = reg.try_get<Charge>(p))
            c->current = std::min(man.charge, c->max_charge);

    auto& satchel = reg.get_or_emplace<Satchel>(p);
    satchel.items.clear();
    for (const auto& it : man.satchel)
        satchel.items.push_back(ItemInstance{it.id, static_cast<Quality>(it.quality), it.count});
}

} // namespace

void persist(const EntityManager& em)
{
    savegame::File file = savegame::load();
    if (file.lives.empty())
    {
        savegame::Data fresh;
        fresh.id = "job-" + std::to_string(++file.minted);
        file.lives.push_back(std::move(fresh));
    }
    savegame::Data& life = *only(file);
    life.record = record::all();
    life.descent = descent::snapshot();
    life.man = readMan(em);
    life.where.room = descent::standing();
    life.where.area = travel::currentArea();
    if (const entt::entity p = player::entity(); em.registry().valid(p))
    {
        const auto& at = em.registry().get<Transform>(p);
        life.where.x = at.x;
        life.where.y = at.y;
        life.where.stood = true;
    }
    savegame::save(file);
}

bool resume(Engine& engine, EntityManager& em)
{
    savegame::File file = savegame::load();
    const savegame::Data* life = only(file);
    if (life == nullptr)
        return false;

    record::restore(life->record);
    descent::restore(life->descent);

    // Stand him where he stopped. A floor rebuilds from its space and musters
    // what it had not finished; a room that is not a floor is just travel.
    const bool stood = life->where.room >= 0 && descent::stand(engine, em, life->where.room);
    if (!stood && !life->where.area.empty() && !travel::enter(engine, em, life->where.area))
    {
        poe::log().error("save: '{}' is not a place any more", life->where.area);
        return false;
    }
    if (!stood && life->where.area.empty())
        return false; // a written-down job that says nowhere is not one to resume

    // AFTER the world is built: entering rebuilds the player, so a sheet applied
    // before it would be thrown away with everything else. The same is true of
    // the spot he was standing on -- arriving puts him at the way in, and this
    // is what says he was already past it.
    writeMan(em, life->man);
    if (life->where.stood)
        player::standAt(em, life->where.x, life->where.y);
    poe::log().info("save: back at it -- {} floor(s), {} species on the record",
                    life->descent.size(), life->record.size());
    return true;
}

void forget()
{
    savegame::save(savegame::File{});
}

} // namespace save_ops
