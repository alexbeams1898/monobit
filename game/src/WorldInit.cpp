#include "WorldInit.h"

#include "ConfigLoader.h"
#include "Engine.h"
#include "TileMapLoader.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/AppearanceOps.h"
#include "ops/InventoryOps.h"
#include "systems/LevelingSystem.h"
#include "systems/TileMapRenderer.h"
#include "systems/WaveSystem.h"

#include <string>
#include <vector>

namespace WorldInit
{

void createWorld(Engine& engine, EntityManager& em)
{
    // Level 0 = base grid size (wave 1).
    auto [px, py] = TileMapLoader::generate(em, "config/tilemap.json", "config/rooms", 0, 0);
    TileMapRenderer::upload(em.tile_map, em.tile_config, engine.textureManager());

    // Spawn rest spots at 'R' markers, player at 'P' marker (fallback to 'R').
    float spawnX = px;
    float spawnY = py;
    for (const auto& sp : em.tile_map.spawn_points)
    {
        if (sp.type == 'R')
        {
            auto entity = ConfigLoader::loadEntity(em, "config/entities/rest_spot.json");
            if (!em.registry().valid(entity))
                continue;
            auto& t = em.registry().get<Transform>(entity);
            t.x = sp.x;
            t.y = sp.y;
            if (spawnX == px && spawnY == py)
            {
                spawnX = sp.x;
                spawnY = sp.y;
            }
        }
        else if (sp.type == 'P')
        {
            spawnX = sp.x;
            spawnY = sp.y;
        }
    }

    auto player = ConfigLoader::loadEntity(em, "config/entities/player.json");
    if (em.registry().valid(player))
    {
        auto& t = em.registry().get<Transform>(player);
        t.x = spawnX;
        t.y = spawnY;
        em.registry().emplace<PlayerActions>(player);
        auto& wallet = em.registry().emplace<Wallet>(player);
        const auto& saveData = em.registry().ctx().get<SaveData>();
        const auto& charName = em.registry().ctx().get<GameState>().active_character;
        std::unordered_map<std::string, std::string> savedAppearance;
        for (const auto& prof : saveData.characters)
        {
            if (prof.name == charName)
            {
                wallet.money = prof.money;
                savedAppearance = prof.appearance;
                break;
            }
        }
        AppearanceOps::resolveAppearance(em, player, engine.spriteCompositor(), savedAppearance);
        AppearanceOps::applyAppearanceScale(em, player, savedAppearance);
        em.registry().emplace<InteractTarget>(player);
        em.registry().emplace<Camera>(
            player, Camera{.x = spawnX, .y = spawnY, .prev_x = spawnX, .prev_y = spawnY});
    }

    // God mode: seed inventory with every weapon + materials for testing.
    const auto& dbg = em.registry().ctx().get<DebugFlags>();
    if (dbg.god_mode && em.registry().valid(player) && em.registry().all_of<Inventory>(player))
    {
        auto& inv = em.registry().get<Inventory>(player);
        const auto& items = em.registry().ctx().get<ItemRegistry>();

        const std::string weaponPaths[] = {
            "config/items/weapons/shiv.json",        "config/items/weapons/dagger.json",
            "config/items/weapons/short_sword.json", "config/items/weapons/longsword.json",
            "config/items/weapons/bone_club.json",   "config/items/weapons/mace.json",
            "config/items/weapons/warhammer.json",   "config/items/weapons/great_maul.json",
            "config/items/weapons/bow.json",         "config/items/weapons/pistol.json",
            "config/items/weapons/semi_auto.json",
        };
        for (const auto& path : weaponPaths)
        {
            ItemInstance item;
            item.config_path = path;
            item.quality = QualityTier::Common;
            InventoryOps::addItem(inv, item, items);
        }

        // Bone shards for crafting.
        ItemInstance shards;
        shards.config_path = "config/items/materials/bone_shard.json";
        shards.quantity = 100;
        InventoryOps::addItem(inv, shards, items);

        // Ammo stacks.
        ItemInstance arrows;
        arrows.config_path = "config/items/ammo/arrow.json";
        arrows.quantity = 99;
        InventoryOps::addItem(inv, arrows, items);

        ItemInstance bullets;
        bullets.config_path = "config/items/ammo/bullet.json";
        bullets.quantity = 99;
        InventoryOps::addItem(inv, bullets, items);
    }

    LevelingSystem::applyInitialDerivations(em);

    // Reset run stats for the new run.
    em.registry().ctx().get<RunStats>() = RunStats{};

    WaveSystem::startNextWave(em);

    em.registry().ctx().get<GameState>().world_initialized = true;
}

void destroyWorld(EntityManager& em)
{
    auto& reg = em.registry();

    // Destroy all entities (players, enemies, body parts, pickups, rest spots, etc.).
    std::vector<entt::entity> all;
    for (auto e : reg.storage<entt::entity>())
        all.push_back(e);
    for (auto e : all)
        if (reg.valid(e))
            reg.destroy(e);

    // Clear the tile map so it doesn't render behind the main menu.
    em.tile_map = TileMap{};
    TileMapRenderer::clear();

    // Reset wave state for the next run.
    reg.ctx().get<WaveState>() = WaveState{};
    reg.ctx().get<RunStats>() = RunStats{};
    reg.ctx().get<AttackTokenPool>().holders.clear();

    em.registry().ctx().get<GameState>().world_initialized = false;
}

} // namespace WorldInit
