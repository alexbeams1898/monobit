#include "gameplay/AppearanceVariance.h"

#include <array>
#include <random>
#include <string_view>
#include <utility>

namespace selva::gameplay
{

namespace
{

// Bipolar morph axis: an incr/decr pair where a positive roll picks
// the incr side and negative picks the decr, with only ONE side
// active at a time. Range value = the MAX weight the picked side can
// receive (roll is uniform 0..max, sign chosen 50/50).
//
// Deliberately narrow ranges (0.15-0.30). Anything higher pushes into
// caricature. Anything much lower is imperceptible at soulslike camera
// distance. Adjust per-morph if a specific axis reads too strong / too
// weak once the variance ships.
struct BipolarAxis
{
    std::string_view incr;
    std::string_view decr;
    float max_weight;
};

// One-sided morph axis: a single morph_name that gets a positive roll
// in [0, max_weight]. For translation-style morphs where "incr" and
// "decr" already encode DIRECTION and there's no natural neutral to
// mirror across (e.g. mouth-trans-up vs mouth-trans-down are already
// a bipolar pair, so treat them as ONE axis where positive->up and
// negative->down).
struct SignedAxis
{
    std::string_view pos_side;
    std::string_view neg_side;
    float max_weight;
};

// SYMMETRIC bipolar axes -- applied identically to both sides where
// applicable (e.g. cheek bones use L+R, both roll to the same value
// to avoid crooked-face asymmetry across the whole face). For pure
// centreline morphs (nose, chin, mouth) only one entry is needed.
const std::array<BipolarAxis, 5> kCentreBipolar = {{
    // Nose shape (small ranges -- nose is very identity-defining)
    {"nose-hump-incr", "nose-hump-decr", 0.30f},
    {"nose-volume-incr", "nose-volume-decr", 0.20f},
    {"nose-width2-incr", "nose-width2-decr", 0.20f},
    // Chin (mild to avoid uncanny valley)
    {"chin-prominent-incr", "chin-prominent-decr", 0.25f},
    {"chin-width-incr", "chin-width-decr", 0.20f},
}};

// SYMMETRIC bipolar axes with L + R variants that get the SAME value
// (e.g. left cheek and right cheek always roll together so faces don't
// go crookedly asymmetric).
struct SymmetricPair
{
    std::string_view l_incr;
    std::string_view l_decr;
    std::string_view r_incr;
    std::string_view r_decr;
    float max_weight;
};
const std::array<SymmetricPair, 1> kSymmetricLR = {{
    {"l-cheek-bones-incr", "l-cheek-bones-decr", "r-cheek-bones-incr", "r-cheek-bones-decr", 0.25f},
}};

// ASYMMETRIC per-side eye-position axes. These get DIFFERENT rolls on
// each side to introduce mild eye-position asymmetry (nobody's eyes
// are perfectly aligned in real life; small differences make the face
// read as "a person" rather than "a mannequin"). Range kept tight so
// nobody looks visibly crooked.
const std::array<SignedAxis, 6> kAsymmetricEyes = {{
    // Left eye position
    {"l-eye-trans-in", "l-eye-trans-out", 0.15f},
    {"l-eye-trans-up", "l-eye-trans-down", 0.15f},
    // Right eye position (independent roll -- asymmetric on purpose)
    {"r-eye-trans-in", "r-eye-trans-out", 0.15f},
    {"r-eye-trans-up", "r-eye-trans-down", 0.15f},
    // Mouth position (bipolar; already an "up vs down" pair)
    {"mouth-trans-up", "mouth-trans-down", 0.20f},
    // Cheek bones side asymmetry is skipped -- we already do L+R
    // together via kSymmetricLR. Adding per-side asymmetry here on
    // top of that would double-write those slots.
}};

// Lip volume (upper + lower independently). Kept modest -- lip volume
// changes read as "different mouth" quickly.
const std::array<BipolarAxis, 2> kLips = {{
    {"mouth-upperlip-volume-incr", "mouth-upperlip-volume-decr", 0.25f},
    {"mouth-lowerlip-volume-incr", "mouth-lowerlip-volume-decr", 0.25f},
}};

// Roll a bipolar axis: 50/50 pick incr or decr side, roll uniform
// [0, max]. The unpicked side is left at 0 to guarantee the two
// counterpair morphs never fight each other.
void rollBipolar(const BipolarAxis& axis, std::mt19937& rng,
                 std::unordered_map<std::string, float>& out)
{
    std::uniform_real_distribution<float> mag(0.0f, axis.max_weight);
    const float value = mag(rng);
    if (std::uniform_int_distribution<int>(0, 1)(rng) == 0)
    {
        out[std::string(axis.incr)] = value;
        out[std::string(axis.decr)] = 0.0f;
    }
    else
    {
        out[std::string(axis.incr)] = 0.0f;
        out[std::string(axis.decr)] = value;
    }
}

void rollSymmetricLR(const SymmetricPair& sp, std::mt19937& rng,
                     std::unordered_map<std::string, float>& out)
{
    std::uniform_real_distribution<float> mag(0.0f, sp.max_weight);
    const float value = mag(rng);
    if (std::uniform_int_distribution<int>(0, 1)(rng) == 0)
    {
        out[std::string(sp.l_incr)] = value;
        out[std::string(sp.l_decr)] = 0.0f;
        out[std::string(sp.r_incr)] = value;
        out[std::string(sp.r_decr)] = 0.0f;
    }
    else
    {
        out[std::string(sp.l_incr)] = 0.0f;
        out[std::string(sp.l_decr)] = value;
        out[std::string(sp.r_incr)] = 0.0f;
        out[std::string(sp.r_decr)] = value;
    }
}

void rollSigned(const SignedAxis& axis, std::mt19937& rng,
                std::unordered_map<std::string, float>& out)
{
    std::uniform_real_distribution<float> mag(0.0f, axis.max_weight);
    const float value = mag(rng);
    if (std::uniform_int_distribution<int>(0, 1)(rng) == 0)
    {
        out[std::string(axis.pos_side)] = value;
        out[std::string(axis.neg_side)] = 0.0f;
    }
    else
    {
        out[std::string(axis.pos_side)] = 0.0f;
        out[std::string(axis.neg_side)] = value;
    }
}

// Thread-local RNG so we don't pay lock contention on the shared
// engine and each thread rolls independently. Seeded once on first
// use with a stable but non-deterministic source (std::random_device),
// which gives us "fully random every spawn" per the design goal.
std::mt19937& rng()
{
    static thread_local std::mt19937 g(
        []
        {
            std::random_device rd;
            return std::mt19937(rd());
        }());
    return g;
}

} // namespace

void rollRandomFaceMorphs(std::unordered_map<std::string, float>& out)
{
    auto& r = rng();
    for (const auto& axis : kCentreBipolar)
        rollBipolar(axis, r, out);
    for (const auto& sp : kSymmetricLR)
        rollSymmetricLR(sp, r, out);
    for (const auto& axis : kAsymmetricEyes)
        rollSigned(axis, r, out);
    for (const auto& axis : kLips)
        rollBipolar(axis, r, out);
}

} // namespace selva::gameplay
