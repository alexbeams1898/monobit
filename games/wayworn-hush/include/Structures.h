#pragma once

#include <array>
#include <string>
#include <unordered_map>

// Structures: placed, walk-on constructions that are NOT native terrain -- bridges,
// docks, wooden platforms, stone floors. Each is authored as ONE resizable LDtk entity
// (a rectangle) whose identifier names a structure type. The importer tiles the type's
// 9-slice art across the rect, marks the covered cells walkable, and tags them with the
// structure's surface -- so collision, footstep sound, and visuals all fall out of the
// single placed box (no per-tile painting). A linear bridge is the thin sub-case of the
// general 9-slice. See config/structures.json.
namespace structures
{

// The 9-slice tile map for one structure type: an atlas cell [col,row] (16px source,
// the importer x2's it) per position, plus the surface these cells get. Indexed as a
// 3x3 grid cell[row][col]: row 0=top,1=middle,2=bottom; col 0=left,1=center,2=right.
struct Layout
{
    std::string surface; // LDtk Surface enum value tagged onto the structure's WALKABLE cells
    std::array<std::array<std::array<int, 2>, 3>, 3> cell{}; // cell[row][col]={uv_col,uv_row}
    // Per-slice walkability: a slice's art always renders, but only walkable slices become
    // footing (walkable + surface). A false slice is cosmetic overhang -- a bridge's side
    // rails hang over water, rendered but not walked on. Defaults to all-true.
    std::array<std::array<bool, 3>, 3> walk{
        {{true, true, true}, {true, true, true}, {true, true, true}}};

    // The 9-slice grid index (cx,cy in 0..2) for a position inside a w x h rect. Edges/
    // corners map to the matching slice; the interior to center. When a dimension is 1,
    // that axis has NO distinct edges -- the single cell spans the full extent, so it uses
    // CENTER on that axis (a 1-wide vertical bridge is the full-deck n/c/s column, not the
    // left rail).
    static std::array<int, 2> sliceIndex(int lc, int lr, int w, int h)
    {
        const int cx = (w == 1) ? 1 : (lc == 0 ? 0 : (lc == w - 1 ? 2 : 1));
        const int cy = (h == 1) ? 1 : (lr == 0 ? 0 : (lr == h - 1 ? 2 : 1));
        return {cx, cy};
    }

    // The atlas cell for a position inside a w x h structure rect (local col/row, 0-based).
    std::array<int, 2> at(int lc, int lr, int w, int h) const
    {
        const auto s = sliceIndex(lc, lr, w, h);
        return cell[static_cast<std::size_t>(s[1])][static_cast<std::size_t>(s[0])];
    }

    // Whether the position is walkable footing (true) or cosmetic overhang (false).
    bool walkableAt(int lc, int lr, int w, int h) const
    {
        const auto s = sliceIndex(lc, lr, w, h);
        return walk[static_cast<std::size_t>(s[1])][static_cast<std::size_t>(s[0])];
    }
};

// Loaded structure types keyed by the LDtk entity identifier ("Bridge", "Dock", ...).
struct Config
{
    std::unordered_map<std::string, Layout> layouts;
};

// Load structure layouts from config/structures.json (silent no-op -> empty if missing,
// so the importer stays permissive: no layout for an entity id -> it isn't a structure).
void load(Config& cfg, const std::string& path);

} // namespace structures
