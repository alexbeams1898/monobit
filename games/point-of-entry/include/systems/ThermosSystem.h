#pragma once

#include <string>
#include <vector>

class EntityManager;

// The thermos: the field's only heal, and a man taking his break.
//
// Sips are charges; the STAGING AREA refills them and is where the fill is chosen. Fills are
// files (config/fills/) with their own effects -- a build choice dressed as lunch planning.
// Nothing heals passively anywhere: rationing sips IS the health game between rests.
namespace thermos
{

// What a fill does per sip, as its file declares.
struct Fill
{
    std::string path;
    std::string name;
    int heal = 25;
    float stamina = 0.0f;
};

// Load sips-per-refill and the fills on hand (config/stats.json "thermos").
void load(const std::string& statsPath);

const std::vector<Fill>& fills();
int fillIndex();
// Choose what gets brewed. Takes effect on the next refill; choosing AT the staging area
// refills immediately -- you are standing right there.
void setFill(EntityManager& em, int index);

int sipsLeft();
int sipsMax();

// Drink one, if any remain and it would do anything. Returns true if he drank.
bool sip(EntityManager& em);

// The staging-area act: full heal, thermos refilled with the chosen fill.
void rest(EntityManager& em);

} // namespace thermos
