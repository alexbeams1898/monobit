#include "GameLoop.h"
#include "SaveGame.h"

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

namespace
{
namespace fs = std::filesystem;

// A scratch save path, removed on scope exit so tests don't leak files or see
// each other's writes.
struct TempSave
{
    fs::path path;

    explicit TempSave(const char* name)
        : path(fs::temp_directory_path() / "wayworn_save_test" / name)
    {
        fs::remove_all(path.parent_path());
        fs::create_directories(path.parent_path());
    }
    ~TempSave()
    {
        std::error_code ec;
        fs::remove_all(path.parent_path(), ec);
    }
    std::string str() const
    {
        return path.string();
    }
};

// A GameState carrying a bit of every kind of progression.
GameState livedInState()
{
    GameState gs;
    gs.observations.observed_tier["rock"] = 2;
    gs.observations.fired.insert("rock_water_worn");
    gs.observations.flags.insert("rock_moss_cleared");
    gs.observations.taken.insert("rock:clear_moss");

    gs.growth.spirit_exp = 42;
    gs.growth.stat_levels["perception"] = 5;
    gs.growth.stat_levels["survival"] = 2;
    gs.growth.buff_levels["keen_eye"] = 1;

    gs.satchel.items.push_back(inventory::ItemInstance{"wild_thyme", 3, false});
    gs.satchel.items.push_back(inventory::ItemInstance{"river_stone", 1, true});

    gs.notebook.at["rock_water_worn"] = 240.0; // a thought reached at this moment

    gs.crafting_state.known.insert("herbal_draught");
    gs.announced_unlocks.insert("rock:clear_moss");
    gs.clock.seconds = 1234.5;
    gs.gone.insert("p_stone_on_the_path"); // a pickup taken -- the world is changed
    return gs;
}

// One pilgrim holding the lived-in walk above.
savegame::Data walkedPilgrim(const char* name = "Ash")
{
    savegame::File f;
    const std::string id = savegame::add(f, name);
    savegame::capture(livedInState(), 100.0f, 200.0f, *savegame::find(f, id));
    return f.pilgrims.front();
}
} // namespace

// --- identity: the thing that diverges from the studio's prior games ------------

TEST_CASE("a pilgrim is keyed by id, not by name -- namesakes don't collide", "[savegame]")
{
    savegame::File f;
    const std::string a = savegame::add(f, "Ash");
    const std::string b = savegame::add(f, "Ash"); // the SAME name, deliberately

    REQUIRE(a != b);
    REQUIRE(f.pilgrims.size() == 2);
    REQUIRE(savegame::find(f, a) != savegame::find(f, b));
    REQUIRE(savegame::find(f, a)->name == "Ash");
    REQUIRE(savegame::find(f, b)->name == "Ash");
}

TEST_CASE("forgetting a pilgrim leaves their namesake alone", "[savegame]")
{
    savegame::File f;
    const std::string a = savegame::add(f, "Ash");
    const std::string b = savegame::add(f, "Ash");

    savegame::remove(f, a);
    REQUIRE(f.pilgrims.size() == 1); // exactly one went
    REQUIRE(savegame::find(f, a) == nullptr);
    REQUIRE(savegame::find(f, b) != nullptr); // the namesake survives
}

TEST_CASE("renaming a pilgrim keeps their identity (and their walk)", "[savegame]")
{
    savegame::File f;
    const std::string id = savegame::add(f, "Ash");
    savegame::find(f, id)->self.spirit_exp = 7;

    savegame::find(f, id)->name = "Bramble"; // free -- the name is not the key

    REQUIRE(savegame::find(f, id) != nullptr);
    REQUIRE(savegame::find(f, id)->name == "Bramble");
    REQUIRE(savegame::find(f, id)->self.spirit_exp == 7);
}

TEST_CASE("an id is never reused after a pilgrim is forgotten", "[savegame]")
{
    // Reuse would let a new pilgrim inherit a forgotten one's identity.
    savegame::File f;
    const std::string first = savegame::add(f, "Ash");
    savegame::remove(f, first);
    const std::string next = savegame::add(f, "Bramble");
    REQUIRE(next != first);
}

TEST_CASE("an id is never reused ACROSS a save/load either", "[savegame]")
{
    // The ever-minted count has to persist: if it reset on load, forgetting the only
    // pilgrim and starting another would hand the newcomer the dead one's identity.
    const TempSave tmp("ids.json");
    savegame::File f;
    const std::string first = savegame::add(f, "Ash");
    savegame::remove(f, first);
    REQUIRE(savegame::save(f, tmp.str()));

    savegame::File reloaded = savegame::load(tmp.str());
    REQUIRE(reloaded.pilgrims.empty()); // nobody left...
    const std::string next = savegame::add(reloaded, "Bramble");
    REQUIRE(next != first); // ...but the forgotten id is still spent
}

TEST_CASE("find is a plain lookup: unknown/empty id is null", "[savegame]")
{
    savegame::File f;
    savegame::add(f, "Ash");
    REQUIRE(savegame::find(f, "nobody") == nullptr);
    REQUIRE(savegame::find(f, "") == nullptr);
}

// --- capture / apply ----------------------------------------------------------

TEST_CASE("capture writes the walk without touching identity", "[savegame]")
{
    savegame::File f;
    const std::string id = savegame::add(f, "Ash");
    savegame::Data& p = *savegame::find(f, id);

    savegame::capture(livedInState(), 100.0f, 200.0f, p);

    REQUIRE(p.id == id); // identity is the world's business to leave alone
    REQUIRE(p.name == "Ash");
    REQUIRE(p.record.observed_tier.at("rock") == 2);
    REQUIRE(p.record.fired.count("rock_water_worn") == 1);
    REQUIRE(p.self.spirit_exp == 42);
    REQUIRE(p.satchel.size() == 2);
    REQUIRE(p.notebook_at.at("rock_water_worn") == 240.0);
    REQUIRE(p.known_recipes.count("herbal_draught") == 1);
    REQUIRE(p.clock_seconds == 1234.5);
    REQUIRE(p.place.x == 100.0f);
    REQUIRE(p.place.y == 200.0f);
    REQUIRE(p.place.walked); // capturing means they've been somewhere
}

TEST_CASE("what the pilgrim removed from the world survives a save", "[savegame]")
{
    // Without this, a picked-up item is rebuilt from the authored map on the next visit --
    // and taken again, and again. The satchel remembering it is not enough; the WORLD has
    // to remember it's gone.
    const TempSave tmp("world.json");
    savegame::File f;
    const std::string id = savegame::add(f, "Ash");
    savegame::capture(livedInState(), 0.0f, 0.0f, *savegame::find(f, id));
    REQUIRE(savegame::save(f, tmp.str()));

    const savegame::File read = savegame::load(tmp.str());
    REQUIRE(savegame::find(read, id)->world.gone.count("p_stone_on_the_path") == 1);

    // And it comes back onto a fresh world, so the spawners can skip it.
    GameState restored;
    savegame::apply(*savegame::find(read, id), restored);
    REQUIRE(restored.gone.count("p_stone_on_the_path") == 1);
}

TEST_CASE("capturing the same walk twice REPLACES it, it doesn't stack another copy", "[savegame]")
{
    // capture() writes into an existing pilgrim so their identity survives -- which means
    // every field has to replace what's there. The list-shaped ones appended instead, so
    // a walk that autosaved three times read back with three copies of everything.
    savegame::File f;
    const std::string id = savegame::add(f, "Ash");
    savegame::Data& p = *savegame::find(f, id);

    const GameState gs = livedInState(); // 2 items carried, 1 thought written down

    savegame::capture(gs, 0.0f, 0.0f, p);
    REQUIRE(p.satchel.size() == 2);
    REQUIRE(p.notebook_at.size() == 1);

    // Saving again (the autosave does this constantly) must not grow them.
    savegame::capture(gs, 0.0f, 0.0f, p);
    savegame::capture(gs, 0.0f, 0.0f, p);
    REQUIRE(p.satchel.size() == 2);
    REQUIRE(p.notebook_at.size() == 1);
}

TEST_CASE("a re-capture reflects what the walk DROPPED, not just what it gained", "[savegame]")
{
    // The other half of replace-don't-append: losing something has to show up too.
    savegame::File f;
    const std::string id = savegame::add(f, "Ash");
    savegame::Data& p = *savegame::find(f, id);

    savegame::capture(livedInState(), 0.0f, 0.0f, p);
    REQUIRE(p.satchel.size() == 2);

    GameState lighter = livedInState();
    lighter.satchel.items.clear(); // he used everything up
    lighter.notebook.at.clear();
    savegame::capture(lighter, 0.0f, 0.0f, p);
    REQUIRE(p.satchel.empty()); // gone, not lingering from the earlier write
    REQUIRE(p.notebook_at.empty());
}

TEST_CASE("apply gives up the walk's world-record BEFORE a world could be built from it",
          "[savegame]")
{
    // The order enterWorld relies on: applying a walk must populate `gone` immediately, so
    // the spawners that read it are filtering against this pilgrim's history. (Building the
    // world first and applying after left `gone` empty at spawn time -- every taken thing
    // came back, and could be taken again.)
    savegame::Data p;
    p.world.gone.insert("p_stone");

    GameState gs;
    REQUIRE(gs.gone.empty()); // nothing known yet
    savegame::apply(p, gs);
    REQUIRE(gs.gone.count("p_stone") == 1); // known the instant the walk is applied
}

TEST_CASE("a fresh pilgrim has nothing gone -- the world is as authored", "[savegame]")
{
    savegame::File f;
    const std::string id = savegame::add(f, "Ash");
    REQUIRE(savegame::find(f, id)->world.gone.empty());
}

TEST_CASE("a fresh pilgrim has not walked -- distinct from standing at the origin", "[savegame]")
{
    // Without this flag a new pilgrim resumes at world (0,0) instead of the map's spawn.
    savegame::File f;
    const std::string id = savegame::add(f, "Ash");
    const savegame::Data& p = *savegame::find(f, id);

    REQUIRE_FALSE(p.place.walked);
    REQUIRE(p.place.x == 0.0f); // the value is there, it just means nothing yet
}

TEST_CASE("apply puts a pilgrim's walk back onto a GameState", "[savegame]")
{
    const savegame::Data p = walkedPilgrim();

    GameState fresh; // as if authored config were loaded but nothing played yet
    savegame::apply(p, fresh);

    REQUIRE(fresh.observations.observed_tier.at("rock") == 2);
    REQUIRE(fresh.observations.flags.count("rock_moss_cleared") == 1);
    REQUIRE(fresh.growth.spirit_exp == 42);
    REQUIRE(fresh.growth.stat_levels.at("perception") == 5);
    REQUIRE(fresh.satchel.items.size() == 2);
    REQUIRE(fresh.notebook.at.at("rock_water_worn") == 240.0);
    REQUIRE(fresh.crafting_state.known.count("herbal_draught") == 1);
    REQUIRE(fresh.clock.seconds == 1234.5);
}

TEST_CASE("capture -> apply is a faithful round trip through a GameState", "[savegame]")
{
    const GameState original = livedInState();
    savegame::File f;
    const std::string id = savegame::add(f, "Ash");
    savegame::capture(original, 0.0f, 0.0f, *savegame::find(f, id));

    GameState restored;
    savegame::apply(*savegame::find(f, id), restored);

    REQUIRE(restored.observations.observed_tier == original.observations.observed_tier);
    REQUIRE(restored.observations.fired == original.observations.fired);
    REQUIRE(restored.observations.flags == original.observations.flags);
    REQUIRE(restored.observations.taken == original.observations.taken);
    REQUIRE(restored.growth.spirit_exp == original.growth.spirit_exp);
    REQUIRE(restored.growth.stat_levels == original.growth.stat_levels);
    REQUIRE(restored.satchel.items.size() == original.satchel.items.size());
    REQUIRE(restored.crafting_state.known == original.crafting_state.known);
}

TEST_CASE("apply REPLACES authored starting levels with the saved ones", "[savegame]")
{
    savegame::Data p;
    p.self.stat_levels["perception"] = 7;

    GameState gs;
    gs.growth.stat_levels["perception"] = 1; // the authored starting value
    gs.growth.stat_levels["wonder"] = 1;     // not in the save

    savegame::apply(p, gs);
    REQUIRE(gs.growth.stat_levels.at("perception") == 7); // the save wins
    REQUIRE(gs.growth.stat_levels.at("wonder") == 1);     // untouched by the save
}

// --- the file -----------------------------------------------------------------

TEST_CASE("save then load round-trips a roster", "[savegame]")
{
    const TempSave tmp("save.json");

    savegame::File written;
    const std::string ash = savegame::add(written, "Ash");
    const std::string bram = savegame::add(written, "Bramble");
    savegame::capture(livedInState(), 10.0f, 20.0f, *savegame::find(written, ash));
    REQUIRE(savegame::save(written, tmp.str()));

    const savegame::File read = savegame::load(tmp.str());
    REQUIRE(read.schema_version == savegame::kSchemaVersion);
    REQUIRE(read.pilgrims.size() == 2);

    const savegame::Data* a = savegame::find(read, ash);
    REQUIRE(a != nullptr);
    REQUIRE(a->name == "Ash");
    REQUIRE(a->record.observed_tier.at("rock") == 2);
    REQUIRE(a->self.spirit_exp == 42);
    REQUIRE(a->self.stat_levels.at("survival") == 2);
    REQUIRE(a->known_recipes.count("herbal_draught") == 1);
    REQUIRE(a->clock_seconds == 1234.5);
    REQUIRE(a->place.x == 10.0f);
    REQUIRE(a->place.walked);

    // The satchel keeps ids, counts, and the new-find flag, in order.
    REQUIRE(a->satchel.size() == 2);
    REQUIRE(a->satchel[0].id == "wild_thyme");
    REQUIRE(a->satchel[0].quantity == 3);
    REQUIRE_FALSE(a->satchel[0].is_new);
    REQUIRE(a->satchel[1].is_new);

    // The notebook keeps WHEN each thought landed. What it SAYS is authored, so it is
    // read back from config rather than stored here.
    REQUIRE(a->notebook_at.size() == 1);
    REQUIRE(a->notebook_at.at("rock_water_worn") == 240.0);

    // The pilgrim who never set out survives as themselves, un-walked.
    const savegame::Data* b = savegame::find(read, bram);
    REQUIRE(b != nullptr);
    REQUIRE(b->name == "Bramble");
    REQUIRE_FALSE(b->place.walked);
}

TEST_CASE("saving one pilgrim leaves the others' walks alone", "[savegame]")
{
    // The write path reads the roster, updates one, writes back -- a save must never
    // clobber somebody else.
    const TempSave tmp("save.json");
    savegame::File f;
    const std::string ash = savegame::add(f, "Ash");
    const std::string bram = savegame::add(f, "Bramble");
    savegame::find(f, bram)->self.spirit_exp = 99;
    REQUIRE(savegame::save(f, tmp.str()));

    savegame::File reread = savegame::load(tmp.str());
    savegame::capture(livedInState(), 1.0f, 2.0f, *savegame::find(reread, ash));
    REQUIRE(savegame::save(reread, tmp.str()));

    const savegame::File after = savegame::load(tmp.str());
    REQUIRE(after.pilgrims.size() == 2);
    REQUIRE(savegame::find(after, ash)->self.spirit_exp == 42);  // the one that walked
    REQUIRE(savegame::find(after, bram)->self.spirit_exp == 99); // untouched
}

TEST_CASE("load of a missing file is an empty roster, not an error", "[savegame]")
{
    const TempSave tmp("absent.json");
    REQUIRE(savegame::load(tmp.str()).pilgrims.empty());
}

TEST_CASE("load of an unreadable file is an empty roster, not a crash", "[savegame]")
{
    const TempSave tmp("corrupt.json");
    std::ofstream(tmp.str()) << "{ this is not json";
    REQUIRE(savegame::load(tmp.str()).pilgrims.empty());
}

TEST_CASE("a pilgrim missing fields reads as defaults (additive change is free)", "[savegame]")
{
    // Adding a field must need no migration: an absent key takes its default.
    const TempSave tmp("sparse.json");
    std::ofstream(tmp.str())
        << R"({"schema_version":1,"pilgrims":[{"id":"p1","name":"Ash","self":{"spirit_exp":9}}]})";

    const savegame::File read = savegame::load(tmp.str());
    REQUIRE(read.pilgrims.size() == 1);
    const savegame::Data& p = read.pilgrims.front();
    REQUIRE(p.self.spirit_exp == 9);
    REQUIRE(p.self.stat_levels.empty());
    REQUIRE(p.record.fired.empty());
    REQUIRE(p.satchel.empty());
    REQUIRE(p.notebook_at.empty());
    REQUIRE(p.known_recipes.empty());
    REQUIRE(p.clock_seconds == 0.0);
    REQUIRE_FALSE(p.place.walked);
}

TEST_CASE("migrate stamps the version and gives an id-less pilgrim an identity", "[savegame]")
{
    // A document predating stable ids still loads, and every pilgrim comes back with one.
    const TempSave tmp("old.json");
    std::ofstream(tmp.str()) << R"({"pilgrims":[{"name":"Ash","self":{"spirit_exp":3}}]})";

    const savegame::File read = savegame::load(tmp.str());
    REQUIRE(read.schema_version == savegame::kSchemaVersion);
    REQUIRE(read.pilgrims.size() == 1);
    REQUIRE_FALSE(read.pilgrims.front().id.empty()); // minted on the way in
    REQUIRE(read.pilgrims.front().self.spirit_exp == 3);
}

TEST_CASE("settings round-trip beside the roster", "[savegame][settings]")
{
    const TempSave tmp("prefs.json");
    savegame::File f;
    savegame::add(f, "Ash");
    f.prefs.hud.visibility = hud::Visibility::Off;
    f.prefs.hud.show_time = false;
    f.prefs.hud.show_stance = false;
    REQUIRE(savegame::save(f, tmp.str()));

    const savegame::File read = savegame::load(tmp.str());
    REQUIRE(read.prefs.hud.visibility == hud::Visibility::Off);
    REQUIRE_FALSE(read.prefs.hud.show_time);
    REQUIRE_FALSE(read.prefs.hud.show_stance);
}

TEST_CASE("settings are the INSTALLATION's -- forgetting every pilgrim keeps them",
          "[savegame][settings]")
{
    // A preference isn't a walk. Deleting the last pilgrim empties the roster; how the
    // player likes their HUD has nothing to do with that and must survive it.
    const TempSave tmp("prefs_outlive.json");
    savegame::File f;
    const std::string id = savegame::add(f, "Ash");
    f.prefs.hud.show_time = true; // not the default -- a choice the player made
    REQUIRE(savegame::save(f, tmp.str()));

    savegame::File live = savegame::load(tmp.str());
    savegame::remove(live, id);
    REQUIRE(savegame::save(live, tmp.str()));

    const savegame::File read = savegame::load(tmp.str());
    REQUIRE(read.pilgrims.empty());
    REQUIRE(read.prefs.hud.show_time); // the roster is gone; the preference is not
}

TEST_CASE("a save with no settings block keeps the booted defaults", "[savegame][settings]")
{
    // Additive field, no migration: every save written before settings existed simply has
    // no such key, and must read back as whatever the game's own defaults are.
    const TempSave tmp("prefs_absent.json");
    std::ofstream(tmp.str()) << R"({"schema_version":2,"pilgrims":[{"id":"p1","name":"Ash"}]})";

    const savegame::File read = savegame::load(tmp.str());
    const settings::Settings fresh;
    REQUIRE(read.prefs.hud.visibility == fresh.hud.visibility);
    REQUIRE(read.prefs.hud.show_time == fresh.hud.show_time);
}

TEST_CASE("an unknown visibility word doesn't silently become Auto", "[settings]")
{
    // A hand-edited or future-written file: keep what we had rather than guessing, so a
    // typo can't quietly reset a player's choice.
    REQUIRE(settings::visibilityFromName("sideways", hud::Visibility::Off) == hud::Visibility::Off);
    REQUIRE(settings::visibilityFromName(nullptr, hud::Visibility::On) == hud::Visibility::On);
}

TEST_CASE("every visibility survives its name round-trip", "[settings]")
{
    for (const auto v : {hud::Visibility::Auto, hud::Visibility::On, hud::Visibility::Off})
        REQUIRE(settings::visibilityFromName(settings::visibilityName(v), hud::Visibility::Auto) ==
                v);
}
