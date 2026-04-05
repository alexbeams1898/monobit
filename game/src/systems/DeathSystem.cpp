#include "systems/DeathSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/WeaponXPSystem.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <tracy/Tracy.hpp>
#include <vector>

// XP = base * level_scale * level_mult * essence_mult
// level_scale = 1 + log_scale * ln(enemy_level)   (log scaling by enemy level)
// level_mult  = max(min_fraction, 1 - penalty * level_diff)   (player over-level penalty)
// essence_mult = 1 + total_essence * essence_scale   (stronger enemies = more XP)
static int computeXpDrop(int base_xp, int enemy_level, int player_level, int total_essence,
                         float log_scale, float min_fraction, float level_penalty,
                         float essence_scale)
{
    const float scale = 1.0f + log_scale * std::log(static_cast<float>(enemy_level));
    const int diff = std::max(0, player_level - enemy_level);
    const float level_mult =
        std::max(min_fraction, 1.0f - level_penalty * static_cast<float>(diff));
    const float essence_mult = 1.0f + static_cast<float>(total_essence) * essence_scale;
    return static_cast<int>(
        std::floor(static_cast<float>(base_xp) * scale * level_mult * essence_mult));
}

static QualityTier rollQuality(int lck, int total_essence, const FormulaConfig& f,
                               std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, 50);
    const float score = static_cast<float>(dist(rng)) +
                        static_cast<float>(lck) * f.luck.quality_scale +
                        static_cast<float>(total_essence) * f.luck.essence_quality_scale;
    if (score < f.luck.quality_thresholds[0])
        return QualityTier::Crude;
    if (score < f.luck.quality_thresholds[1])
        return QualityTier::Common;
    if (score < f.luck.quality_thresholds[2])
        return QualityTier::Fine;
    if (score < f.luck.quality_thresholds[3])
        return QualityTier::Superior;
    return QualityTier::Masterwork;
}

// Purple-tinted palette: intensity escalates with rarity. VeryCommon has no glow.
static SolidColor rarityColor(Rarity r)
{
    switch (r)
    {
    case Rarity::VeryCommon:
        return {0.55f, 0.55f, 0.55f};
    case Rarity::Uncommon:
        return {0.55f, 0.4f, 0.7f};
    case Rarity::Rare:
        return {0.6f, 0.3f, 0.85f};
    case Rarity::Epic:
        return {0.75f, 0.25f, 0.95f};
    case Rarity::Legendary:
        return {0.9f, 0.8f, 1.0f};
    default:
        return {0.6f, 0.5f, 0.65f};
    }
}

// Quality brightness multiplier: higher quality = brighter pickup.
static float qualityBrightness(QualityTier q)
{
    switch (q)
    {
    case QualityTier::Crude:
        return 0.75f;
    case QualityTier::Common:
        return 1.0f;
    case QualityTier::Fine:
        return 1.15f;
    case QualityTier::Superior:
        return 1.3f;
    case QualityTier::Masterwork:
        return 1.5f;
    }
    return 1.0f;
}

// Shared helper: emplace the visual components for a pickup entity.
static void emplacePickupVisuals(EntityManager& em, entt::entity pickup, const SolidColor& color,
                                 Rarity rarity, QualityTier quality)
{
    const float bright = qualityBrightness(quality);
    em.registry().emplace<SolidColor>(pickup, SolidColor{std::min(1.0f, color.r * bright),
                                                         std::min(1.0f, color.g * bright),
                                                         std::min(1.0f, color.b * bright)});

    Sprite spr;
    spr.src_w = 12;
    spr.src_h = 12;
    spr.layer = 1;
    em.registry().emplace<Sprite>(pickup, spr);

    Collider col;
    col.width = 12.0f;
    col.height = 12.0f;
    col.is_solid = false;
    em.registry().emplace<Collider>(pickup, col);

    // Glow from rarity + quality boost.
    // Base glow from rarity; quality >= Fine adds glow even to VeryCommon items.
    float glowScale = 0.0f;
    float glowAlpha = 0.0f;
    if (rarity > Rarity::VeryCommon)
    {
        glowScale = 2.0f;
        glowAlpha = 0.15f;
    }
    if (rarity >= Rarity::Rare)
    {
        glowScale = 3.0f;
        glowAlpha = 0.25f;
    }
    if (rarity >= Rarity::Legendary)
    {
        glowScale = 3.5f;
        glowAlpha = 0.35f;
    }

    // Quality glow boost: Fine+ adds glow even on VeryCommon items.
    if (quality >= QualityTier::Fine)
    {
        glowScale = std::max(glowScale, 2.0f);
        glowAlpha = std::max(glowAlpha, 0.1f);
    }
    if (quality >= QualityTier::Superior)
    {
        glowScale = std::max(glowScale, 2.5f);
        glowAlpha = std::max(glowAlpha, 0.2f);
    }
    if (quality >= QualityTier::Masterwork)
    {
        glowScale = std::max(glowScale, 3.0f);
        glowAlpha = std::max(glowAlpha, 0.3f);
    }

    if (glowScale > 0.0f)
        em.registry().emplace<Glow>(pickup, Glow{glowScale, glowAlpha});

    em.registry().emplace<Tag>(pickup, Tag{"pickup"});
}

// Nudge a position out of walls. Checks the pickup's bounding box corners
// (not just center) against the tilemap. If any corner is in a non-walkable
// tile, searches neighboring tiles in expanding rings up to 3 tiles out.
static void clampToWalkable(const TileMap& tm, float& x, float& y)
{
    if (!tm.valid())
        return;

    // Check all 4 corners of the pickup's 12x12 bounding box.
    constexpr float HALF = 6.0f;
    bool allWalkable = true;
    for (const float oy : {-HALF, HALF})
    {
        for (const float ox : {-HALF, HALF})
        {
            const int c = static_cast<int>(x + ox) / TileMap::TILE_SIZE;
            const int r = static_cast<int>(y + oy) / TileMap::TILE_SIZE;
            if (!tm.in_bounds(c, r) || !tm.at(c, r).walkable)
            {
                allWalkable = false;
                break;
            }
        }
        if (!allWalkable)
            break;
    }
    if (allWalkable)
        return;

    const int col = static_cast<int>(x) / TileMap::TILE_SIZE;
    const int row = static_cast<int>(y) / TileMap::TILE_SIZE;

    constexpr int MAX_RING = 3;
    for (int ring = 1; ring <= MAX_RING; ++ring)
    {
        for (int dy = -ring; dy <= ring; ++dy)
        {
            for (int dx = -ring; dx <= ring; ++dx)
            {
                if (std::abs(dx) != ring && std::abs(dy) != ring)
                    continue; // skip inner cells already checked
                const int nc = col + dx;
                const int nr = row + dy;
                if (tm.in_bounds(nc, nr) && tm.at(nc, nr).walkable)
                {
                    x = static_cast<float>(nc * TileMap::TILE_SIZE) +
                        static_cast<float>(TileMap::TILE_SIZE) * 0.5f;
                    y = static_cast<float>(nr * TileMap::TILE_SIZE) +
                        static_cast<float>(TileMap::TILE_SIZE) * 0.5f;
                    return;
                }
            }
        }
    }
}

// Roll each drop entry and spawn Pickup entities at the death position.
static void spawnDrops(EntityManager& em, const Loot& loot, float deathX, float deathY, int lck,
                       int total_essence, const FormulaConfig& f)
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> offsetDist(-16.0f, 16.0f);

    const auto& itemReg = em.registry().ctx().get<ItemRegistry>();

    for (const auto& drop : loot.drops)
    {
        const float effective = std::min(
            1.0f, drop.base_chance * (1.0f + static_cast<float>(lck) * f.luck.drop_scale / 100.0f));

        if (chanceDist(rng) > effective)
            continue;

        std::uniform_int_distribution<int> qtyDist(drop.min_qty, drop.max_qty);
        const int qty = qtyDist(rng);
        if (qty <= 0)
            continue;

        const QualityTier quality = rollQuality(lck, total_essence, f, rng);

        auto pickup = em.create();
        Transform t;
        t.x = deathX + offsetDist(rng);
        t.y = deathY + offsetDist(rng);
        clampToWalkable(em.tile_map, t.x, t.y);
        em.registry().emplace<Transform>(pickup, t);

        Pickup p;
        p.item.config_path = drop.config_path;
        p.item.quantity = qty;
        p.item.quality = quality;
        em.registry().emplace<Pickup>(pickup, std::move(p));

        const ItemDef* def = itemReg.find(drop.config_path);
        const Rarity rarity = (def != nullptr) ? def->rarity : Rarity::Common;
        const bool isMoney = (def != nullptr && def->category == ItemCategory::Money);
        if (isMoney)
        {
            // Money: flat green, no glow, no rarity/quality visuals.
            emplacePickupVisuals(em, pickup, SolidColor{0.2f, 0.75f, 0.3f}, Rarity::VeryCommon,
                                 QualityTier::Common);
        }
        else
        {
            emplacePickupVisuals(em, pickup, rarityColor(rarity), rarity, quality);
        }

        TracyMessageL("ItemDropped");
    }
}

// Grant XP directly to the player, cascade-destroy body-part children, then
// destroy entity.
static void processEnemyDeath(EntityManager& em, entt::entity entity, const Loot& loot,
                              const std::string& name)
{
    auto& reg = em.registry();
    const FormulaConfig& f = reg.ctx().get<FormulaConfig>();

    int playerLevel = 1;
    int playerLck = 1;
    for (auto pe : reg.view<PlayerActions>())
    {
        if (reg.all_of<Experience>(pe))
            playerLevel = reg.get<Experience>(pe).level;
        if (reg.all_of<Stats>(pe))
            playerLck = reg.get<Stats>(pe).lck;
        break;
    }

    int totalEss = 0;
    if (reg.all_of<Essence>(entity))
    {
        const auto& ess = reg.get<Essence>(entity);
        totalEss = ess.str + ess.dex + ess.end + ess.lck;
    }
    const int xpValue =
        computeXpDrop(loot.xp_drop, loot.level, playerLevel, totalEss, f.xp_drop.log_scale,
                      f.xp_drop.min_fraction, f.xp_drop.level_penalty, f.xp_drop.essence_scale);

    // Grant XP directly to the player.
    for (auto pe : reg.view<PlayerActions>())
    {
        if (reg.all_of<Experience>(pe))
        {
            reg.get<Experience>(pe).current_xp += xpValue;
            break;
        }
    }

    // Grant weapon XP on kill.
    {
        int enemyHp = 0;
        float enemyDmg = 0.0f;
        int enemyStats = 0;
        if (reg.all_of<Health>(entity))
            enemyHp = reg.get<Health>(entity).max;
        if (reg.all_of<Weapon>(entity))
            enemyDmg = reg.get<Weapon>(entity).base_damage;
        if (reg.all_of<Stats>(entity))
        {
            const auto& s = reg.get<Stats>(entity);
            enemyStats = s.str + s.dex + s.end + s.lck;
        }
        const float power =
            WeaponXPSystem::computeEnemyPower(loot.level, enemyHp, enemyDmg, enemyStats, f);
        WeaponXPSystem::grantXP(em, power, f.weapon_xp.kill_multiplier);
    }

    // Accumulate run stats.
    auto& runStats = reg.ctx().get<RunStats>();
    runStats.kills++;
    runStats.xp_earned += xpValue;

    // Spawn drops before destroying the entity.
    if (reg.all_of<Transform>(entity) && !loot.drops.empty())
    {
        const auto& t = reg.get<Transform>(entity);
        int totalEssence = 0;
        if (reg.all_of<Essence>(entity))
        {
            const auto& e = reg.get<Essence>(entity);
            totalEssence = e.str + e.dex + e.end + e.lck;
        }
        spawnDrops(em, loot, t.x, t.y, playerLck, totalEssence, f);
    }

    std::vector<entt::entity> children;
    for (auto [child, bp] : reg.view<BodyPart>().each())
        if (bp.parent == entity)
            children.push_back(child);
    for (auto child : children)
        em.destroy(child);

    em.destroy(entity);
}

void DeathSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("DeathSystem");
    auto& reg = em.registry();
    auto& waveState = reg.ctx().get<WaveState>();

    // Tick death timers first -- entities waiting for death animation to finish.
    for (auto [entity, dead] : reg.view<Dead>().each())
    {
        if (dead.timer > 0.0f)
            dead.timer -= static_cast<float>(dt);
    }

    // Collect all dead entities whose timer has expired before destroying any
    // (entt iterator safety).
    struct DeadEntry
    {
        entt::entity entity;
        bool is_player;
        Loot loot;
        std::string name;
    };

    std::vector<DeadEntry> dead;
    for (auto [entity, deadComp] : reg.view<Dead>().each())
    {
        // Wait for death animation to finish.
        if (deadComp.timer > 0.0f)
            continue;

        const bool is_player = reg.all_of<PlayerActions>(entity);
        Loot loot;
        if (reg.all_of<Loot>(entity))
            loot = reg.get<Loot>(entity);

        std::string name = "???";
        if (reg.all_of<Tag>(entity))
            name = reg.get<Tag>(entity).name;

        dead.push_back({entity, is_player, loot, std::move(name)});
    }

    for (const auto& entry : dead)
    {
        if (entry.is_player)
        {
            // Destroy body-part children so the player sprite disappears.
            std::vector<entt::entity> children;
            for (auto [child, bp] : reg.view<BodyPart>().each())
                if (bp.parent == entry.entity)
                    children.push_back(child);
            for (auto child : children)
                em.destroy(child);

            reg.remove<Dead>(entry.entity);
            if (reg.all_of<Health>(entry.entity))
                reg.get<Health>(entry.entity).current = 0;
            waveState.phase = WaveState::Phase::GameOver;
        }
        else if (reg.all_of<Loot>(entry.entity))
        {
            processEnemyDeath(em, entry.entity, entry.loot, entry.name);
        }
        else
        {
            em.destroy(entry.entity);
        }
    }
}
