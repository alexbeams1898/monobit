#pragma once

// Mind sub-page graph layout.
//
// Computes a stable spatial position for every insight node, used by
// the Mind sub-page canvas. Deterministic force-directed
// layout. Inputs are the loaded graph (node ids, observation/conclusion
// kinds, requires edges) and the player's unlocked set. Output is a
// {node_id -> (x, y)} map in 0..1 normalized canvas space.
//
// Determinism: same input set -> same output positions. The layout is
// seeded by the sorted node-id list (no Date.now / no random source),
// so two players with the same fired-node set see the same graph.
//
// Optional JSON `pos` override: any node whose JSON includes a pos is
// PINNED at that value during the layout. The force pass relaxes the
// other nodes around it. Authoring scope: rarely needed; only when a
// specific node looks bad after auto-layout.
//
// When this runs: at graph load, and after any deduction that fires a
// new conclusion. The pause menu reads the cached positions; layout
// does not re-run while the menu is open.

#include "insight/Insight.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace selva::insight::layout
{

// Run the layout pass over the currently loaded graph + the player's
// unlocked set. Idempotent; safe to call repeatedly.
void recompute();

// Return the cached canvas position for a node. Returns {0,0} for
// unknown ids. The UI consumes this every frame; cheap O(1) lookup.
NodePos posOf(const std::string& node_id);

// Vertical separator x-coordinates for column boundaries WITHIN
// each category region. Used by the canvas renderer to draw thin
// vertical lines that visually divide cluster columns from each
// other and from the free-observations column. Coordinates are in
// 0..1 normalized canvas space. Y-range comes from the category
// region; the renderer fetches it from regionFor() equivalents.
struct CategorySeparators
{
    Category category;
    float y_top;           // 0..1 normalized
    float y_bottom;        // 0..1 normalized
    std::vector<float> xs; // boundary x positions
};

const std::vector<CategorySeparators>& separators();

// Reset the cache (test seam).
void reset();

} // namespace selva::insight::layout
