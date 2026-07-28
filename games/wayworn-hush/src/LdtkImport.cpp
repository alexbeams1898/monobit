#include "LdtkImport.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <nlohmann/json.hpp>
#include <stb_image.h> // declarations only; implementation lives in TextureManager.cpp

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace ldtk
{
// Defined below with the entity helpers; the internal prop builder reads authored
// fields through it.
std::string entityField(const nlohmann::json& e, const char* identifier);

namespace
{
using nlohmann::json;

// Forward decls -- helpers below reference these before their definitions appear.
int cellId(int uv_col, int uv_row, int atlas_cols);
const json* findLayer(const json& level, const char* identifier);

// The loaded render atlas (RGBA). Owns the stb pixel buffer so both scans below --
// fully-transparent cells and per-prop opaque bounds -- share ONE decode of the PNG.
struct Atlas
{
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    explicit Atlas(const std::string& path)
    {
        int ch = 0;
        px = stbi_load(path.c_str(), &w, &h, &ch, 4); // force RGBA
    }
    ~Atlas()
    {
        if (px)
            stbi_image_free(px);
    }
    Atlas(const Atlas&) = delete;
    Atlas& operator=(const Atlas&) = delete;
    bool ok() const
    {
        return px != nullptr;
    }
    int alphaAt(int x, int y) const
    {
        return px[(y * w + x) * 4 + 3];
    }
};

// Atlas cell ids that are FULLY TRANSPARENT (no opaque pixel) in the render atlas.
// A transparent tile carries no visual and only causes bugs when it's accidentally
// painted (e.g. a stray full-fill from the tileset's empty corner displacing real
// stacked decoration). The importer skips these so such mistakes never render.
std::unordered_set<int> transparentCells(const Atlas& atlas, int atlas_tile, int atlas_cols)
{
    std::unordered_set<int> out;
    if (!atlas.ok())
        return out; // atlas unavailable -> skip nothing (importer stays permissive)
    const int cols = atlas.w / atlas_tile;
    const int rows = atlas.h / atlas_tile;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            bool anyOpaque = false;
            for (int y = 0; y < atlas_tile && !anyOpaque; ++y)
                for (int x = 0; x < atlas_tile; ++x)
                    if (atlas.alphaAt(c * atlas_tile + x, r * atlas_tile + y) != 0)
                    {
                        anyOpaque = true;
                        break;
                    }
            if (!anyOpaque)
                out.insert(r * atlas_cols + c);
        }
    return out;
}

// A pixel rectangle (origin + size). Used for atlas source rects and derived bounds.
struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
};

// Tight bounding box of the OPAQUE pixels inside the BOTTOM BAND of a source rect --
// the band [h-band, h) at the sprite's base. This derives the FOOTPRINT (a tree's
// trunk, a rock's whole body), not the full opaque bounds: a tree's canopy is opaque
// but walk-through, so scanning only the base band gives a collider that hugs the
// trunk and leaves the leaves passable. `band` is the footprint height in the same
// pixel scale as `src` (== world px). Returned bounds are RELATIVE to src's top-left.
// Returns {} (w==0) if the band is fully transparent or the rect is out of the atlas.
Rect footprintBounds(const Atlas& atlas, const Rect& src, int band, bool at_top = false)
{
    if (!atlas.ok() || src.w <= 0 || src.h <= 0)
        return {};
    // The scanned band: the sprite's BASE for a standing prop's footprint, its TOP
    // for a cover prop's raised back (the headboard you cannot walk through).
    const int y0 = at_top ? 0 : std::max(0, src.h - std::max(1, band));
    const int y1 = at_top ? std::min(src.h, std::max(1, band)) : src.h;
    int minx = src.w, maxx = -1, maxy = -1;
    for (int y = y0; y < y1; ++y)
    {
        const int ay = src.y + y;
        if (ay < 0 || ay >= atlas.h)
            continue;
        for (int x = 0; x < src.w; ++x)
        {
            const int ax = src.x + x;
            if (ax < 0 || ax >= atlas.w)
                continue;
            if (atlas.alphaAt(ax, ay) != 0)
            {
                minx = std::min(minx, x);
                maxx = std::max(maxx, x);
                maxy = std::max(maxy, y);
            }
        }
    }
    if (maxx < 0)
        return {}; // no opaque pixel in the band
    return Rect{minx, y0, maxx - minx + 1, maxy - y0 + 1};
}

void deriveCollider(const Atlas& atlas, const json& e, Prop& p);

// Build a visual Prop from a tile-carrying LDtk entity `e` whose pivot is at world
// (wx,wy). Resolves the sprite's atlas rect + center (pivot -> center for the CENTERED
// draw), the base Y-sort key, and the derived collider. `atlas` is the decoded
// render atlas for the alpha scan.
Prop buildProp(const Atlas& atlas, const json& e, const json& tile, float wx, float wy)
{
    // __tile {x,y,w,h} is in the 16px source; x2 for the 32px render atlas. The sprite's
    // world size = the tile region x2.
    Prop p;
    p.sx = tile.value("x", 0) * 2;
    p.sy = tile.value("y", 0) * 2;
    p.sw = tile.value("w", 0) * 2;
    p.sh = tile.value("h", 0) * 2;

    // LDtk px is the entity's PIVOT point; the pivot fractions say where in the entity
    // box that is. RenderSystem draws a sprite CENTERED on its Transform, so convert
    // pivot -> center. World size = entity box x2 (matches the tile region).
    const int ww = e.value("width", 0) * 2;
    const int hh = e.value("height", 0) * 2;
    float pvx = 0.5f, pvy = 1.0f; // default base-center pivot
    if (const auto pv = e.find("__pivot"); pv != e.end() && pv->is_array() && pv->size() == 2)
    {
        pvx = (*pv)[0].get<float>();
        pvy = (*pv)[1].get<float>();
    }
    p.wx = wx + static_cast<float>(ww) * (0.5f - pvx);
    p.wy = wy + static_cast<float>(hh) * (0.5f - pvy);
    p.sort_wy = wy; // px IS the pivot point -> its world Y is the base (feet if pivotY=1)

    // Which plane the thing occupies (see Prop::Plane). Absent = Standing -- the
    // common case authors nothing. Case-insensitive so a String field ("cover")
    // and an LDtk enum field ("Cover") both read.
    std::string plane = entityField(e, "plane");
    for (char& c : plane)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    p.plane = plane == "floor"   ? Prop::Plane::Floor
              : plane == "cover" ? Prop::Plane::Cover
                                 : Prop::Plane::Standing;
    deriveCollider(atlas, e, p);
    return p;
}

// Derive a prop's collider from its art. Floor props take none (you walk on
// them); cover props collide at their TOP band (the raised back); standing props
// at their base FOOTPRINT -- opaque pixels only, so collision hugs the art. Band
// height is authored per-entity as `collider_height` (source px; 0 = deliberately
// no collider); absent -> a quarter of the sprite. Atlas px == world px.
void deriveCollider(const Atlas& atlas, const json& e, Prop& p)
{
    if (p.plane == Prop::Plane::Floor)
        return;
    int band = p.sh / 4;
    if (const auto fis = e.find("fieldInstances"); fis != e.end() && fis->is_array())
        for (const auto& fi : *fis)
            if (fi.value("__identifier", std::string{}) == "collider_height")
                if (const auto v = fi.find("__value"); v != fi.end() && v->is_number())
                    band = v->get<int>() * 2; // source px -> world px
    if (band <= 0)
        return;

    const Rect fp = footprintBounds(atlas, Rect{p.sx, p.sy, p.sw, p.sh}, band,
                                    /*at_top=*/p.plane == Prop::Plane::Cover);
    if (fp.w > 0)
    {
        const float spriteLeft = p.wx - static_cast<float>(p.sw) * 0.5f;
        const float spriteTop = p.wy - static_cast<float>(p.sh) * 0.5f;
        p.col_solid = true;
        p.col_w = static_cast<float>(fp.w);
        p.col_h = static_cast<float>(fp.h);
        p.col_cx = spriteLeft + static_cast<float>(fp.x) + p.col_w * 0.5f;
        p.col_cy = spriteTop + static_cast<float>(fp.y) + p.col_h * 0.5f;
    }
}

// A tileset source pixel (src:[sx,sy]) at the authoring grid size maps to an atlas
// CELL index; that index is identical in the x2 render atlas (x2 cancels). So the
// engine tile id is a stable cell index, and uv_col/uv_row = the same cell.
int cellId(int uv_col, int uv_row, int atlas_cols)
{
    return uv_row * atlas_cols + uv_col;
}

// Find a layer instance by its __IDENTIFIER (the layer's name: "Ground",
// "Overhang", "Entities"). NOT __type -- Ground and Overhang are both __type
// "Tiles", so only the identifier distinguishes them.
const json* findLayer(const json& level, const char* identifier)
{
    const auto li = level.find("layerInstances");
    if (li == level.end() || !li->is_array())
        return nullptr;
    for (const auto& layer : *li)
        if (layer.value("__identifier", std::string{}) == identifier)
            return &layer;
    return nullptr;
}

// The tileset def a LEVEL's ground layer was painted with, by the layer's
// __tilesetDefUid. Tile ids are per-tileset, so everything keyed by them (surfaces,
// atlas columns, the render atlas itself) must come from THIS def, not a global one.
// uid < 0 (a layer without the field) falls back to the legacy Overworld match.
const json* tilesetDef(const json& j, int uid)
{
    const auto defs = j.find("defs");
    if (defs == j.end())
        return nullptr;
    const auto sets = defs->find("tilesets");
    if (sets == defs->end() || !sets->is_array())
        return nullptr;
    for (const auto& ts : *sets)
    {
        if (uid >= 0 ? ts.value("uid", -1) == uid
                     : ts.value("relPath", std::string{}).find("Overworld") != std::string::npos)
            return &ts;
    }
    return nullptr;
}

// The 32px render atlas for a tileset, by convention: authoring source `Inner.png` ->
// `assets/tilesets/inner.png` (the x2 nearest-neighbor twin -- see MAP-PIPELINE.md).
// Falls back to `fallback` (the configured default atlas) when the def carries no
// relPath or the conventional file doesn't exist -- loudly, because a level painted
// with a tileset whose atlas is missing would otherwise render from the wrong sheet.
std::string atlasPathFor(const json* def, const std::string& fallback)
{
    if (def == nullptr)
        return fallback;
    const std::string rel = def->value("relPath", std::string{});
    if (rel.empty())
        return fallback;
    std::string stem = std::filesystem::path(rel).stem().string();
    for (char& c : stem)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string path = "assets/tilesets/" + stem + ".png";
    if (!std::filesystem::exists(path))
    {
        std::fprintf(stderr, "[ldtk] no render atlas '%s' for tileset '%s' -- falling back to %s\n",
                     path.c_str(), rel.c_str(), fallback.c_str());
        return fallback;
    }
    return path;
}

// Map atlas cell id -> surface name from a tileset's enumTags. LDtk tags tiles in
// the tileset editor: each enumTags entry is {enumValueId, tileIds:[cell ids]}, where
// enumValueId is the surface (e.g. "Water") and tileIds are the cells wearing it (the
// SAME cell index space as cellId()). Untagged cells simply aren't in the map -> they
// resolve to the default surface.
std::unordered_map<int, std::string> surfaceByCell(const json& j, int tileset_uid)
{
    std::unordered_map<int, std::string> out;
    const json* ts = tilesetDef(j, tileset_uid);
    if (ts == nullptr)
        return out;
    if (const auto tags = ts->find("enumTags"); tags != ts->end() && tags->is_array())
        for (const auto& tag : *tags)
        {
            const std::string surface = tag.value("enumValueId", std::string{});
            if (const auto ids = tag.find("tileIds"); ids != tag.end() && ids->is_array())
                for (const auto& id : *ids)
                    out[id.get<int>()] = surface;
        }
    return out;
}
} // namespace

// The stable identity of a placed entity, as the map file records it. `iid` is the
// authoring format's own persistent per-entity UUID: it survives moving, resizing, and
// re-authoring, which is exactly what an identity the SAVE refers to has to do (a
// position or an index would not).
//
// This is the only place that name appears. Everything downstream calls it a
// placement_id and never learns where it came from -- so the identity outlives the tool
// that mints it.
std::string placementIdOf(const json& e)
{
    return e.value("iid", std::string{});
}

// A string-valued entity field instance by identifier ("encounter", "trigger"), or
// empty if absent/not a string. LDtk stores custom fields in fieldInstances as
// {__identifier, __value}.
std::string entityField(const json& e, const char* identifier)
{
    if (const auto fis = e.find("fieldInstances"); fis != e.end() && fis->is_array())
        for (const auto& fi : *fis)
            if (fi.value("__identifier", std::string{}) == identifier)
            {
                const auto v = fi.find("__value");
                if (v != fi.end() && v->is_string())
                    return v->get<std::string>();
            }
    return {};
}

// An integer-valued entity field by identifier, or `fallback` if absent/not a number.
int entityFieldInt(const json& e, const char* identifier, int fallback)
{
    if (const auto fis = e.find("fieldInstances"); fis != e.end() && fis->is_array())
        for (const auto& fi : *fis)
            if (fi.value("__identifier", std::string{}) == identifier)
            {
                const auto v = fi.find("__value");
                if (v != fi.end() && v->is_number())
                    return v->get<int>();
            }
    return fallback;
}

// The CENTER of a box entity in world px. LDtk's `px` is the entity's PIVOT point --
// NOT necessarily the top-left: a bottom-center pivot (0.5,1) puts `px` at the box's
// bottom edge. Treating px as top-left silently shifts the box by up to a full size
// in each axis, so where the author SEES the box and where the game puts it disagree.
// center = pivot point + size * (0.5 - pivot), the same math the prop path uses.
// False (a malformed entity with no position) means skip the entity, not fail the load.
bool boxCenter(const json& e, float& cx, float& cy, float& w, float& h)
{
    const auto px = e.find("px");
    if (px == e.end() || !px->is_array() || px->size() != 2)
        return false;
    const float wx = static_cast<float>((*px)[0].get<int>() * 2);
    const float wy = static_cast<float>((*px)[1].get<int>() * 2);
    w = static_cast<float>(e.value("width", 16) * 2);
    h = static_cast<float>(e.value("height", 16) * 2);
    float pvx = 0.0f, pvy = 0.0f; // LDtk's default pivot is top-left
    if (const auto pv = e.find("__pivot"); pv != e.end() && pv->is_array() && pv->size() == 2)
    {
        pvx = (*pv)[0].get<float>();
        pvy = (*pv)[1].get<float>();
    }
    cx = wx + w * (0.5f - pvx);
    cy = wy + h * (0.5f - pvy);
    return true;
}

// If entity `e` carries a non-empty `encounter` field, append its placement: the box
// AABB (center + size, x2 to world px) marks WHERE the encounter is -- you interact when
// within interact_reach of it. Also reads the optional `trigger` mode. Only the Encounter
// box carries this field; physical entities stay field-free.
void collectEncounter(const json& e, Region& r)
{
    const std::string id = entityField(e, "encounter");
    if (id.empty())
        return;
    const std::string placementId = placementIdOf(e);
    if (placementId.empty())
        return; // no identity -> the game could never record what happened to it
    EncounterPlacement p;
    p.placement_id = placementId;
    p.id = id;
    if (!boxCenter(e, p.x, p.y, p.w, p.h))
        return;
    p.trigger = entityField(e, "trigger");
    r.encounters.push_back(std::move(p));
}

// Scan an entity-carrying layer for encounter placements (entities with an `observable`
// field). Used for both the Entities layer (props that opt in) and the dedicated
// Encounters layer (standalone area/point triggers).
void collectEncountersInLayer(const json& level, const char* layerName, Region& r)
{
    const json* lay = findLayer(level, layerName);
    if (!lay)
        return;
    if (const auto ei = lay->find("entityInstances"); ei != lay->end() && ei->is_array())
        for (const auto& e : *ei)
            collectEncounter(e, r);
}

// Append a world pickup for entity `e`: a static drop if it carries an `item` field, a
// gather node if it carries a `loot` field (a table id). px is the entity's top-left
// (authoring px); a point entity, so its center is px + half its authored cell (x2 to world
// px). `target` binds to the item or loot registry at load.
void collectPickup(const json& e, Region& r)
{
    PickupPlacement p;
    p.placement_id = placementIdOf(e);
    if (p.placement_id.empty())
        return; // no identity -> taking it could never be remembered
    if (const std::string item = entityField(e, "item"); !item.empty())
    {
        p.kind = PickupPlacement::Kind::Item;
        p.target = item;
    }
    else if (const std::string table = entityField(e, "loot"); !table.empty())
    {
        p.kind = PickupPlacement::Kind::Loot;
        p.target = table;
    }
    else
        return; // neither field -> not a pickup/gather entity
    const auto px = e.find("px");
    if (px == e.end() || !px->is_array())
        return;
    const float halfW = static_cast<float>(e.value("width", 16));  // authoring px, pre-x2
    const float halfH = static_cast<float>(e.value("height", 16)); // (center offset = half)
    p.cx = static_cast<float>((*px)[0].get<int>() * 2) + halfW; // px*2 + (cell*2)/2 = px*2 + cell
    p.cy = static_cast<float>((*px)[1].get<int>() * 2) + halfH;
    r.pickups.push_back(std::move(p));
}

// Scan the Pickups layer for pickups + gather nodes (entities carrying `item` or `loot`).
void collectPickupsInLayer(const json& level, const char* layerName, Region& r)
{
    const json* lay = findLayer(level, layerName);
    if (!lay)
        return;
    if (const auto ei = lay->find("entityInstances"); ei != lay->end() && ei->is_array())
        for (const auto& e : *ei)
            collectPickup(e, r);
}

// Entities layer -> props (tile-carrying) + objects (typed, e.g. PlayerSpawn). LDtk px
// is authoring-grid px, x2 to the 32px world. `atlas` decodes prop footprint colliders.
// Structure-type entities (bridges, docks) are handled by parseStructures (stamped into
// the map), so they're skipped here rather than becoming stray objects.
// Split a COVER prop at `t` world px from its top into its two natures: above the
// line is the part you lie ON (pillow, headboard -- drawn under a body), below it
// the part that ENCLOSES (the blanket -- drawn over). One authored entity, one
// authored number (`cover_height`, the enclosing part's height from the bottom),
// two sprites at import. The derived collider (the raised back) rides the upper
// piece.
void splitCoverProp(const Prop& p, int t, Region& r)
{
    const float top = p.wy - static_cast<float>(p.sh) * 0.5f;
    Prop upper = p;
    upper.plane = Prop::Plane::Floor;
    upper.sh = t;
    upper.wy = top + static_cast<float>(t) * 0.5f;
    Prop lower = p;
    lower.sy = p.sy + t;
    lower.sh = p.sh - t;
    lower.wy = top + static_cast<float>(t) + static_cast<float>(lower.sh) * 0.5f;
    lower.col_solid = false; // the collider (if any) belongs to the upper piece
    r.props.push_back(upper);
    r.props.push_back(lower);
}

// A tile-carrying entity -> prop(s): one sprite normally; a cover prop with an
// authored `cover_height` (the enclosing part's height in source px, from the
// sprite's bottom) splits in two.
void collectProp(const Atlas& atlas, const json& e, const json& tile, float wx, float wy, Region& r)
{
    const Prop p = buildProp(atlas, e, tile, wx, wy);
    const int coverH = entityFieldInt(e, "cover_height", 0) * 2; // source px -> world px
    if (p.plane == Prop::Plane::Cover && coverH > 0 && coverH < p.sh)
        splitCoverProp(p, p.sh - coverH, r);
    else
        r.props.push_back(p);
}

// An Npc entity -> a character placement (who, where, facing), plus its talk
// encounter when an `encounter` field is present -- the same placement anchors both
// the body and the box (talking is observing).
void collectNpc(const json& e, float wx, float wy, Region& r)
{
    NpcPlacement n;
    n.npc = entityField(e, "npc");
    n.facing = entityField(e, "facing");
    n.wx = wx;
    n.wy = wy;
    if (!n.npc.empty())
        r.npcs.push_back(std::move(n));
    else
        std::fprintf(stderr, "[ldtk] an Npc in '%s' names no npc id -- dropped\n",
                     r.level_id.c_str());
    collectEncounter(e, r);
}

// A Warp entity -> a threshold placement. Dropped loudly when it has no position or
// no destination -- a half-authored door should say so, not silently not exist.
void collectWarp(const json& e, Region& r)
{
    WarpPlacement w;
    w.id = entityField(e, "id");
    w.target_level = entityField(e, "target_level");
    w.target = entityField(e, "target");
    if (w.target.empty())
        w.target = entityField(e, "target_spawn"); // legacy field name
    w.facing = entityField(e, "facing");
    if (boxCenter(e, w.x, w.y, w.w, w.h) && !w.target_level.empty())
        r.warps.push_back(std::move(w));
    else
        std::fprintf(stderr,
                     "[ldtk] a Warp in '%s' has no position or no target_level -- dropped\n",
                     r.level_id.c_str());
}

// An invisible solid box, placed and sized by hand -- collision authored exactly
// where the author means it (a bed's footboard, a gap too narrow to squeeze
// through) instead of derived from art.
void collectBlocker(const json& e, Region& r)
{
    Prop b;
    if (boxCenter(e, b.col_cx, b.col_cy, b.col_w, b.col_h))
    {
        b.col_solid = true;
        r.props.push_back(b); // no atlas rect -> nothing drawn, only the collider
    }
}

// PlayerSpawn and Marker are both NAMED POINTS in the same lookup space:
// PlayerSpawn is where the player arrives, Marker is a spot choreography
// (scenes) steers by. Same shape, different word in the map so authoring reads
// honestly.
void collectPoint(const json& e, float wx, float wy, Region& r)
{
    SpawnPoint s;
    s.id = entityField(e, "id");
    s.facing = entityField(e, "facing");
    s.wx = wx;
    s.wy = wy;
    r.spawns.push_back(std::move(s));
}

void parseEntities(const Atlas& atlas, const json& level, const structures::Config& structureCfg,
                   Region& r)
{
    const json* ents = findLayer(level, "Entities");
    if (!ents)
        return;
    const auto ei = ents->find("entityInstances");
    if (ei == ents->end() || !ei->is_array())
        return;
    for (const auto& e : *ei)
    {
        const std::string id = e.value("__identifier", std::string{});
        if (structureCfg.layouts.count(id))
            continue; // a structure -> stamped by parseStructures, not a prop/object
        const auto px = e.find("px");
        if (px == e.end() || !px->is_array())
            continue;
        const float wx = static_cast<float>((*px)[0].get<int>() * 2);
        const float wy = static_cast<float>((*px)[1].get<int>() * 2);
        const auto tile = e.find("__tile");
        // Npc BEFORE the tile branch: an Npc def may carry an editor icon tile, and
        // that must not reroute a character into the props path.
        if (id == "Npc")
        {
            collectNpc(e, wx, wy, r);
        }
        else if (id == "Blocker")
        {
            collectBlocker(e, r);
        }
        else if (tile != e.end() && tile->is_object())
        {
            collectProp(atlas, e, *tile, wx, wy, r);
        }
        else if (id == "PlayerSpawn" || id == "Marker")
        {
            collectPoint(e, wx, wy, r);
        }
        else if (id == "Warp")
        {
            collectWarp(e, r);
        }
        else
        {
            Object o;
            o.type = id;
            o.wx = wx;
            o.wy = wy;
            r.objects.push_back(o);
        }
    }
}

// Per-level properties (LDtk level fields): the level's ambient track + whether it is
// an interior. Parsed here, consumed as the audio/light systems land.
void parseLevelFields(const json& level, Region& r)
{
    if (const auto fis = level.find("fieldInstances"); fis != level.end() && fis->is_array())
        for (const auto& fi : *fis)
        {
            const std::string ident = fi.value("__identifier", std::string{});
            const auto v = fi.find("__value");
            if (v == fi.end())
                continue;
            if (ident == "music" && v->is_string())
                r.music = v->get<std::string>();
            else if (ident == "interior" && v->is_boolean())
                r.interior = v->get<bool>();
        }
}

// Dimensions + lookups shared by ground parsing (all resolved once in load()).
struct GridInfo
{
    int grid_size = 16; // authoring cell px (16)
    int atlas_cols = 40;
    int cw = 0, ch = 0;    // grid columns/rows
    int fill_id = 0;       // border-fill cell id (meaningful only when has_fill)
    bool has_fill = false; // did the level author a fill_tile at all?
};

// The tile id meaning "nothing here": no visual is ever registered for it, so the
// renderer draws empty space (the clear color shows through), and it is unwalkable.
// Unpainted cells in a level WITHOUT an authored fill_tile get this -- blank means
// blank; a level that wants grass-to-the-horizon authors its fill_tile.
constexpr int kEmptyTileId = -1;

// Stamp resizable structure entities (bridges, docks) into the map: for each entity
// whose identifier is a known structure type, tile the 9-slice art across its rect, mark
// those cells walkable, and tag them with the structure's surface (so collision +
// footsteps come from the one placed box). Runs AFTER ground so a structure draws over
// whatever terrain it spans. Registers each stamped tile's uv into `uvById` and its
// surface into r.tile_surface.
void parseStructures(const json& level, const structures::Config& structureCfg, const GridInfo& g,
                     std::unordered_map<int, std::pair<int, int>>& uvById, Region& r)
{
    const json* ents = findLayer(level, "Entities");
    if (!ents)
        return;
    const auto ei = ents->find("entityInstances");
    if (ei == ents->end() || !ei->is_array())
        return;
    for (const auto& e : *ei)
    {
        const auto lay = structureCfg.layouts.find(e.value("__identifier", std::string{}));
        if (lay == structureCfg.layouts.end())
            continue; // not a structure type
        const auto px = e.find("px");
        if (px == e.end() || !px->is_array())
            continue;
        // Entity rect in CELLS (px + width/height are authoring px; pivot is top-left).
        const int col0 = (*px)[0].get<int>() / g.grid_size;
        const int row0 = (*px)[1].get<int>() / g.grid_size;
        const int w = std::max(1, e.value("width", g.grid_size) / g.grid_size);
        const int h = std::max(1, e.value("height", g.grid_size) / g.grid_size);
        for (int lr = 0; lr < h; ++lr)
            for (int lc = 0; lc < w; ++lc)
            {
                const int col = col0 + lc;
                const int row = row0 + lr;
                if (col < 0 || row < 0 || col >= g.cw || row >= g.ch)
                    continue;
                const std::array<int, 2> uv = lay->second.at(lc, lr, w, h);
                const int id = cellId(uv[0], uv[1], g.atlas_cols);
                uvById[id] = {uv[0], uv[1]};
                const std::size_t idx = r.map.cellIndex(col, row);
                // The structure art ALWAYS goes on the DECORATION layer, drawn over the
                // terrain but under the player -- so the ground base underneath still
                // renders where the structure tile is transparent (a bridge-end cap and
                // the side rails taper to terrain; only the middle deck is fully opaque).
                // Never replace the base tile, or that transparency shows the clear color
                // instead of the ground.
                r.map.decoration.push_back({idx, TileMap::Tile{id, true}});
                // Walkable slices are footing: mark the cell walkable and give it a
                // per-CELL surface override (the base tile stays terrain for rendering,
                // but you sound like wood walking the deck). Overhang slices (rails)
                // leave the terrain's own walkability -- you don't walk or step there.
                if (lay->second.walkableAt(lc, lr, w, h))
                {
                    r.map.tiles[idx].walkable = true;
                    r.cell_surface[idx] = lay->second.surface;
                }
            }
    }
}

// Resolve the region's grid header: find the Ground layer, read its dimensions into
// r.map, parse the level's fill_tile into r.fill_uv_*, and fill the GridInfo. Returns
// the ground layer, or nullptr if there's no usable ground (caller bails to ok=false).
const json* parseHeader(const json& level, int atlas_cols, int tileset_uid, GridInfo& g, Region& r)
{
    const json* ground = findLayer(level, "Ground");
    if (!ground)
        ground = findLayer(level, "Tiles"); // legacy single-layer name
    if (!ground)
        return nullptr;
    g.grid_size = ground->value("__gridSize", 16);
    g.atlas_cols = atlas_cols;
    g.cw = ground->value("__cWid", 0);
    g.ch = ground->value("__cHei", 0);
    if (g.cw <= 0 || g.ch <= 0)
        return nullptr;

    r.map.tile_size = g.grid_size * 2; // 16 -> 32 world grid
    r.map.width = g.cw;
    r.map.height = g.ch;

    // fill_tile (level field) -> the default/fill cell for unpainted cells (grass to
    // the horizon outdoors). ABSENT = unpainted cells are genuinely empty (see
    // kEmptyTileId) -- an interior's void is the clear color, not a surprise tile.
    // A fill referencing a DIFFERENT tileset than the level is painted with is
    // rejected the same way: tile ids only mean anything within their own sheet, so
    // honouring it would fill the level with whatever sits at those coords in the
    // level's actual atlas (an interior checkered with the sheet's palette swatch).
    if (const auto fis = level.find("fieldInstances"); fis != level.end() && fis->is_array())
        for (const auto& fi : *fis)
            if (fi.value("__identifier", std::string{}) == "fill_tile")
                if (const auto v = fi.find("__value"); v != fi.end() && v->is_object())
                {
                    const int fillTs = v->value("tilesetUid", -1);
                    if (fillTs >= 0 && tileset_uid >= 0 && fillTs != tileset_uid)
                    {
                        std::fprintf(stderr,
                                     "[ldtk] level '%s': fill_tile references tileset %d but "
                                     "the ground is painted with %d -- ignoring the fill\n",
                                     r.level_id.c_str(), fillTs, tileset_uid);
                        continue;
                    }
                    r.fill_uv_col = v->value("x", 0) / g.grid_size;
                    r.fill_uv_row = v->value("y", 0) / g.grid_size;
                    g.has_fill = true;
                }
    g.fill_id = g.has_fill ? cellId(r.fill_uv_col, r.fill_uv_row, atlas_cols) : kEmptyTileId;
    return ground;
}

// Decode one gridTiles entry into its grid cell + atlas uv (authoring px -> cells).
// False if the entry is malformed or lands outside the grid.
bool decodeGridTile(const json& t, const GridInfo& g, int& col, int& row, int& uv_col, int& uv_row)
{
    const auto px = t.find("px");
    const auto src = t.find("src");
    if (px == t.end() || src == t.end() || !px->is_array() || !src->is_array())
        return false;
    col = (*px)[0].get<int>() / g.grid_size;
    row = (*px)[1].get<int>() / g.grid_size;
    if (col < 0 || row < 0 || col >= g.cw || row >= g.ch)
        return false;
    uv_col = (*src)[0].get<int>() / g.grid_size;
    uv_row = (*src)[1].get<int>() / g.grid_size;
    return true;
}

// Parse the Ground layer into r.map: the base tile per cell (walkability = its surface)
// plus a decoration layer for stacked tiles (flowers over grass, drawn under the
// player). Skips fully-transparent atlas cells. Returns the id->uv map for the tile
// config. LDtk stacks tiles per cell in paint order; the FIRST at a cell is the base,
// later ones are decoration.
std::unordered_map<int, std::pair<int, int>>
parseGround(const json& ground, const Atlas& atlas, const surfaces::Config& surfaceCfg,
            const GridInfo& g, const std::unordered_map<int, std::string>& cellSurface, Region& r)
{
    const std::size_t total = static_cast<std::size_t>(g.cw) * static_cast<std::size_t>(g.ch);
    const std::unordered_set<int> transparent =
        transparentCells(atlas, r.map.tile_size, g.atlas_cols);
    const auto walkableFor = [&](int id)
    {
        const auto it = cellSurface.find(id);
        return surfaceCfg.walkable(it == cellSurface.end() ? std::string{} : it->second);
    };

    std::unordered_map<int, std::pair<int, int>> uvById;
    if (g.has_fill)
        uvById[g.fill_id] = {r.fill_uv_col, r.fill_uv_row};
    r.map.tiles.assign(total,
                       TileMap::Tile{g.fill_id, g.has_fill ? walkableFor(g.fill_id) : false});
    r.map.decoration.clear();

    std::unordered_map<std::size_t, bool> seen; // cell -> already placed a base?
    const auto gt = ground.find("gridTiles");
    if (gt == ground.end() || !gt->is_array())
        return uvById;
    for (const auto& t : *gt)
    {
        int col = 0, row = 0, uv_col = 0, uv_row = 0;
        if (!decodeGridTile(t, g, col, row, uv_col, uv_row))
            continue;
        const int id = cellId(uv_col, uv_row, g.atlas_cols);
        if (transparent.count(id))
            continue; // blank atlas cell (e.g. a stray fill) -> no visual, skip
        uvById[id] = {uv_col, uv_row};
        const std::size_t idx = r.map.cellIndex(col, row);
        if (!seen[idx])
        {
            seen[idx] = true;
            r.map.tiles[idx] = TileMap::Tile{id, walkableFor(id)}; // base
        }
        else
        {
            // Stacked tiles keep PAINT order (gridTiles order), so the renderer
            // composites the stack exactly as the editor shows it -- every layer,
            // not just the topmost.
            r.map.decoration.push_back({idx, TileMap::Tile{id, true}});
        }
    }
    return uvById;
}

// Fill the tile config (atlas path + per-id uv) from the id->uv map collected while
// placing ground tiles.
void fillTileConfig(const std::unordered_map<int, std::pair<int, int>>& uvById,
                    const std::string& tileset_path, Region& r)
{
    r.config = TileConfig{};
    r.config.tileset_path = tileset_path;
    r.config.atlas_tile_size = r.map.tile_size; // 32
    for (const auto& [id, uv] : uvById)
    {
        r.config.tiles[id] = {tileset_path, true};
        TileConfig::TileVisual vis;
        vis.uv_col = uv.first;
        vis.uv_row = uv.second;
        r.config.tile_visuals[id] = vis;
    }
}

// Select the level to import: by identifier when named, the project's first otherwise.
// A named level that's absent is fatal (nullptr) -- loading the wrong place silently
// would be worse.
const json* chooseLevel(const json& j, const std::string& level_id, const std::string& ldtk_path)
{
    const auto levels = j.find("levels");
    if (levels == j.end() || !levels->is_array() || levels->empty())
        return nullptr;
    if (level_id.empty())
        return &(*levels)[0];
    for (const auto& lv : *levels)
        if (lv.value("identifier", std::string{}) == level_id)
            return &lv;
    std::fprintf(stderr, "[ldtk] FATAL: no level '%s' in %s\n", level_id.c_str(),
                 ldtk_path.c_str());
    return nullptr;
}

Region loadImpl(const std::string& ldtk_path, const std::string& tileset_path,
                const surfaces::Config& surfaceCfg, const structures::Config& structureCfg,
                const std::string& level_id)
{
    Region r;
    std::ifstream f(ldtk_path);
    if (!f)
        return r;
    const json j = json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return r;

    const json* chosen = chooseLevel(j, level_id, ldtk_path);
    if (!chosen)
        return r;
    const json& level = *chosen;
    r.level_id = level.value("identifier", std::string{});

    // The level's OWN tileset drives everything keyed by tile id: atlas columns,
    // surface tags, and the render atlas itself. An interior painted with Inner.png
    // renders from inner's atlas; the overworld from overworld's. The ground layer
    // names its tileset; a level without the field falls back to the configured
    // default (`tileset_path`).
    const json* groundLayer = findLayer(level, "Ground");
    if (!groundLayer)
        groundLayer = findLayer(level, "Tiles");
    const int tilesetUid = groundLayer ? groundLayer->value("__tilesetDefUid", -1) : -1;
    const json* tsDef = tilesetDef(j, tilesetUid);
    const std::string atlas_path = atlasPathFor(tsDef, tileset_path);
    // Authoring grid size (16) -> world grid is x2 (32). Atlas columns come from
    // the tileset def so a src pixel resolves to a cell index (40 = Overworld default).
    const int atlas_cols = tsDef ? tsDef->value("__cWid", 40) : 40;

    GridInfo g;
    const json* ground = parseHeader(level, atlas_cols, tilesetUid, g, r);
    if (!ground)
        return r; // no usable ground layer

    // Decode the render atlas ONCE: drives both the transparent-cell skip and each
    // prop's derived collider (opaque bounds).
    const Atlas atlas(atlas_path);

    // The atlas column count is derived TWO ways -- from the PNG width (atlas.w /
    // tile_size, used by transparentCells) and from the LDtk tileset def (__cWid, used by
    // cellId). They MUST agree, or tile ids computed one way index the skip-set the other
    // way and the wrong tiles silently render/vanish. Validate at load; a mismatch means
    // the PNG and the .ldtk disagree about the tileset -- fatal, not silently wrong.
    if (atlas.ok())
    {
        if (atlas.w % r.map.tile_size != 0 || atlas.h % r.map.tile_size != 0)
        {
            std::fprintf(stderr,
                         "[ldtk] FATAL: atlas %s is %dx%d, not a multiple of tile size %d.\n",
                         atlas_path.c_str(), atlas.w, atlas.h, r.map.tile_size);
            return r; // ok stays false
        }
        if (atlas.w / r.map.tile_size != atlas_cols)
        {
            std::fprintf(stderr,
                         "[ldtk] FATAL: atlas %s has %d columns but the .ldtk tileset def says "
                         "%d -- the PNG and .ldtk disagree.\n",
                         atlas_path.c_str(), atlas.w / r.map.tile_size, atlas_cols);
            return r; // ok stays false
        }
    }

    // Per-cell surface tags (from THIS level's tileset enumTags -- tile ids are
    // per-tileset). Terrain walkability comes from a tile's surface (water blocks,
    // grass walks) -- no hand-painted collision. The runtime also maps a tile id ->
    // surface for per-surface footsteps.
    const std::unordered_map<int, std::string> cellSurface = surfaceByCell(j, tilesetUid);
    r.tile_surface = cellSurface;

    auto uvById = parseGround(*ground, atlas, surfaceCfg, g, cellSurface, r);
    // Structures stamp over the ground (a deck spans whatever terrain), adding their
    // tiles to the config + surface map, so do them before fillTileConfig.
    parseStructures(level, structureCfg, g, uvById, r);
    fillTileConfig(uvById, atlas_path, r);
    parseEntities(atlas, level, structureCfg, r);
    // Observation PLACEMENTS come ONLY from the dedicated Encounters layer. Observability
    // is its own concern -- a resizable Encounter box placed anywhere (over a bridge
    // piece, an area, an object). Physical entities (Rock, Tree, Bridge) stay purely
    // physical; they carry no observation fields. The content lives in psyche.json;
    // the box is just where + how it fires. See docs/design/PSYCHE.md.
    collectEncountersInLayer(level, "Encounters", r);
    // Pickups: items lying in the world, authored on their own Pickups layer (a Pickup entity
    // carrying an `item` id, or a Gather entity carrying a `loot` table id). Bound to
    // inventory/loot at load, spawned as floor sprites (world_items::spawn). See
    // docs/design/GAME-SYSTEMS.md.
    collectPickupsInLayer(level, "Pickups", r);
    parseLevelFields(level, r);

    r.ok = true;
    return r;
}

Region load(const std::string& ldtk_path, const std::string& tileset_path,
            const surfaces::Config& surfaceCfg, const structures::Config& structureCfg,
            const std::string& level)
{
    // The many .get<int/float/string>() calls in loadImpl throw json::type_error if a
    // hand-authored .ldtk has a field of the wrong type (allow_exceptions=false covers
    // only parse(), not the typed getters). Catch here so a bad field names the FILE
    // instead of surfacing as an opaque std::terminate. A throw -> Region{ok=false},
    // which the caller treats as a fatal load failure.
    try
    {
        return loadImpl(ldtk_path, tileset_path, surfaceCfg, structureCfg, level);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[ldtk] FATAL: malformed field in %s: %s\n", ldtk_path.c_str(),
                     e.what());
        return Region{};
    }
}

std::string findStartLevel(const std::string& ldtk_path)
{
    std::ifstream f(ldtk_path);
    if (!f)
        return {};
    const json j = json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return {};
    const auto levels = j.find("levels");
    if (levels == j.end() || !levels->is_array())
        return {};
    for (const auto& lvl : *levels)
    {
        const json* ents = findLayer(lvl, "Entities");
        if (!ents)
            continue;
        const auto ei = ents->find("entityInstances");
        if (ei == ents->end() || !ei->is_array())
            continue;
        for (const auto& e : *ei)
            if (e.value("__identifier", std::string{}) == "PlayerSpawn" &&
                entityField(e, "id").empty())
                return lvl.value("identifier", std::string{});
    }
    return {};
}

void spawnProps(EntityManager& em, const Region& region)
{
    auto& reg = em.registry();
    for (const auto& p : region.props)
    {
        // A prop with no atlas rect (a Blocker) is pure collision -- skip the sprite.
        if (p.sw <= 0 || p.sh <= 0)
        {
            if (p.col_solid)
            {
                const entt::entity ce = reg.create();
                reg.emplace<Transform>(ce, Transform{p.col_cx, p.col_cy});
                reg.emplace<Collider>(ce, Collider{p.col_w, p.col_h, true});
            }
            continue;
        }
        const entt::entity e = reg.create();
        // Transform is the sprite's top-left; RenderSystem draws the src rect there.
        reg.emplace<Transform>(e, Transform{p.wx, p.wy});
        Sprite spr{};
        // The region's RESOLVED atlas (per-level tileset), not a global one -- an
        // interior's furniture draws from the interior sheet.
        spr.texture_path = region.config.tileset_path;
        spr.src_x = p.sx;
        spr.src_y = p.sy;
        spr.src_w = p.sw;
        spr.src_h = p.sh;
        spr.layer = 2; // character layer -- Y-sorted against the player
        // ONE sprite for the whole prop. A STANDING prop sorts by its BASE (feet)
        // world-Y, exactly like the player. A FLOOR prop is always underfoot and a
        // COVER prop always over whoever is inside it -- expressed as sort keys
        // beyond any real world-Y, so no character can ever out-sort them.
        constexpr float kUnderEveryone = -1.0e6f;
        constexpr float kOverEveryone = 1.0e6f;
        spr.use_sort_anchor = true;
        spr.sort_anchor = p.plane == Prop::Plane::Floor   ? kUnderEveryone
                          : p.plane == Prop::Plane::Cover ? kOverEveryone
                                                          : p.sort_wy;
        reg.emplace<Sprite>(e, spr);

        // Derived collider: a separate static entity at the trunk's center carrying a
        // solid Collider sized to the footprint. Separate from the sprite because the
        // engine Collider is centered on its Transform, and the trunk is offset from
        // (and smaller than) the sprite. No Velocity -> PlayerMovement reads it as a
        // static wall. Skipped for fully-transparent props (nothing to collide with).
        if (p.col_solid)
        {
            const entt::entity ce = reg.create();
            reg.emplace<Transform>(ce, Transform{p.col_cx, p.col_cy});
            Collider c{};
            c.width = p.col_w;
            c.height = p.col_h;
            c.is_solid = true;
            reg.emplace<Collider>(ce, c);
        }
    }
}

} // namespace ldtk
