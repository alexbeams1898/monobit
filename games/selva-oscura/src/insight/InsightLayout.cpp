#include "insight/InsightLayout.h"

#include "AppStateGlobal.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Deterministic Mind sub-page layout. Inputs: the active profile's
// unlocked insights + the loaded node graph. Output: a per-node
// position in 0..1 normalized canvas space.
//
// Why deterministic and not force-directed: the graph is small
// (~5-30 nodes), the structure is fully known at compute time
// (categories, clusters, requires edges, confirmed_by edges), and
// the player benefits from a stable, predictable arrangement. The
// previous force-directed pass produced unstable, hard-to-tune
// results and had ~6 strength constants competing with each other.
// This pass computes positions in one walk with no iteration.
//
// Layout shape:
//
//   World region (left third)        Self region (upper right)
//     [cluster 1 column]               [cluster column]
//     [cluster 2 column]               [free observations]
//     [free observations]
//                                     Others region (lower right)
//                                       [cluster column]
//                                       [free observations]
//
//   Conclusions: placed ABOVE their requires-observations,
//     centered horizontally at the centroid X of those observations.
//   Confirmed observations: when a conclusion is certain, its
//     confirmed_by observation is placed immediately to the right
//     of the conclusion (overriding its prior position).

namespace selva::insight::layout
{

namespace
{

std::unordered_map<std::string, NodePos>& sCache()
{
    static std::unordered_map<std::string, NodePos> c;
    return c;
}

std::vector<CategorySeparators>& sSeparators()
{
    static std::vector<CategorySeparators> v;
    return v;
}

struct Region
{
    float cx; // center x
    float cy; // center y
    float w;  // width
    float h;  // height
};

Region regionFor(Category c)
{
    switch (c)
    {
    case Category::World:
        return {0.22f, 0.50f, 0.30f, 0.80f};
    case Category::Self:
        return {0.72f, 0.30f, 0.30f, 0.40f};
    case Category::Others:
        return {0.72f, 0.70f, 0.30f, 0.40f};
    default:
        return {0.50f, 0.50f, 0.30f, 0.40f};
    }
}

} // namespace

// Group observations in one category into clusters (>=2-same-source
// columns) + a single 'free' column at the end. Returns the columns
// in display order (cluster columns first, frees last).
std::vector<std::vector<std::string>> buildCategoryColumns(const std::vector<std::string>& ids,
                                                           Category cat)
{
    std::vector<std::string> nodes_in_cat;
    for (const auto& id : ids)
    {
        if (kindOf(id) != NodeKind::Observation || categoryOf(id) != cat)
            continue;
        nodes_in_cat.push_back(id);
    }
    std::unordered_map<std::string, std::vector<std::string>> by_source;
    std::vector<std::string> source_order;
    for (const auto& id : nodes_in_cat)
    {
        const std::string s = sourceOf(id);
        if (by_source.find(s) == by_source.end())
            source_order.push_back(s);
        by_source[s].push_back(id);
    }
    std::vector<std::vector<std::string>> columns;
    std::vector<std::string> frees;
    for (const auto& s : source_order)
    {
        auto& v = by_source[s];
        if (s.empty() || v.size() < 2)
            for (const auto& id : v)
                frees.push_back(id);
        else
            columns.push_back(v);
    }
    if (!frees.empty())
        columns.push_back(frees);
    return columns;
}

// Lay out the columns within a category region. Writes positions to
// `pos`; returns the x-center of each column for separator placement.
std::vector<float> placeCategoryColumns(const std::vector<std::vector<std::string>>& columns,
                                        const Region& r,
                                        std::unordered_map<std::string, NodePos>& pos)
{
    constexpr float kColGap = 0.08f;
    const float left = r.cx - r.w * 0.5f;
    const float top = r.cy - r.h * 0.5f;
    const float total_gap = kColGap * static_cast<float>(columns.size() - 1);
    const float col_w = (r.w - total_gap) / static_cast<float>(columns.size());
    std::vector<float> col_centers;
    col_centers.reserve(columns.size());
    for (std::size_t c = 0; c < columns.size(); ++c)
    {
        const float col_x = left + (col_w + kColGap) * static_cast<float>(c) + col_w * 0.5f;
        col_centers.push_back(col_x);
        const auto& col_nodes = columns[c];
        const std::size_t n = col_nodes.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const float t = (n == 1) ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1);
            const float y = top + r.h * (0.30f + 0.40f * t);
            pos[col_nodes[i]] = {col_x, y};
        }
    }
    return col_centers;
}

// Emit category-internal separators between adjacent columns.
void emitCategorySeparators(Category cat, const Region& r, const std::vector<float>& col_centers)
{
    if (col_centers.size() < 2)
        return;
    const float top = r.cy - r.h * 0.5f;
    CategorySeparators cs;
    cs.category = cat;
    cs.y_top = top + r.h * 0.02f;
    cs.y_bottom = top + r.h * 0.98f;
    for (std::size_t c = 1; c < col_centers.size(); ++c)
    {
        const float boundary = (col_centers[c - 1] + col_centers[c]) * 0.5f;
        cs.xs.push_back(boundary);
    }
    if (!cs.xs.empty())
        sSeparators().push_back(std::move(cs));
}

// Emit the synthetic region-boundary separator between World (left)
// and Self/Others (right).
void emitRegionBoundarySeparator()
{
    const Region rW = regionFor(Category::World);
    const Region rS = regionFor(Category::Self);
    const float boundary = (rW.cx + rW.w * 0.5f + rS.cx - rS.w * 0.5f) * 0.5f;
    CategorySeparators cs;
    cs.category = Category::Unknown;
    cs.y_top = 0.02f;
    cs.y_bottom = 0.98f;
    cs.xs.push_back(boundary);
    sSeparators().push_back(std::move(cs));
}

// Pre-compute per-source counts of currently-visible observations.
std::unordered_map<std::string, int> countVisibleSources(const std::vector<std::string>& ids)
{
    std::unordered_map<std::string, int> out;
    for (const auto& other : ids)
    {
        if (kindOf(other) != NodeKind::Observation)
            continue;
        const std::string s = sourceOf(other);
        if (!s.empty())
            out[s]++;
    }
    return out;
}

// Place a single conclusion above its requires-observations. Biases
// toward a clustered requires when present; falls back to centroid;
// final fallback is the category region center.
void placeOneConclusion(const std::string& id, std::unordered_map<std::string, NodePos>& pos,
                        const std::unordered_map<std::string, int>& visible_source_count)
{
    const auto reqs = requiresOf(id);
    std::string clustered_req;
    float clustered_x = 0.0f;
    float clustered_min_y = 1.0f;
    for (const auto& r : reqs)
    {
        auto it = pos.find(r);
        if (it == pos.end())
            continue;
        const std::string s = sourceOf(r);
        auto sit = visible_source_count.find(s);
        if (!s.empty() && sit != visible_source_count.end() && sit->second >= 2)
        {
            clustered_req = r;
            clustered_x = it->second.x;
            if (it->second.y < clustered_min_y)
                clustered_min_y = it->second.y;
        }
    }
    if (!clustered_req.empty())
    {
        pos[id] = {clustered_x, std::max(0.08f, clustered_min_y - 0.14f)};
        return;
    }
    float sum_x = 0.0f;
    float min_y = 1.0f;
    int count = 0;
    for (const auto& r : reqs)
    {
        auto it = pos.find(r);
        if (it == pos.end())
            continue;
        sum_x += it->second.x;
        if (it->second.y < min_y)
            min_y = it->second.y;
        ++count;
    }
    if (count > 0)
    {
        pos[id] = {sum_x / static_cast<float>(count), std::max(0.08f, min_y - 0.14f)};
        return;
    }
    const Region r = regionFor(categoryOf(id));
    pos[id] = {r.cx, r.cy};
}

// Pull confirming observations adjacent to each certain conclusion.
void pullConfirmersAdjacent(const std::vector<std::string>& ids,
                            std::unordered_map<std::string, NodePos>& pos)
{
    for (const auto& id : ids)
    {
        if (kindOf(id) != NodeKind::Inference || !isCertain(id))
            continue;
        auto cit = pos.find(id);
        if (cit == pos.end())
            continue;
        const auto cbs = confirmedByOf(id);
        const float offset = 0.06f;
        int n = 0;
        for (const auto& cb : cbs)
        {
            if (pos.find(cb) == pos.end())
                continue;
            pos[cb] = {cit->second.x + offset * (n + 1), cit->second.y};
            ++n;
        }
    }
}

// Final pass: clamp all positions into the canvas safe margin.
void clampPositions(std::unordered_map<std::string, NodePos>& pos)
{
    for (auto& [id, p] : pos)
    {
        p.x = std::clamp(p.x, 0.04f, 0.96f);
        p.y = std::clamp(p.y, 0.06f, 0.94f);
    }
}

void recompute()
{
    auto& cache = sCache();
    cache.clear();
    sSeparators().clear();
    const PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return;
    std::vector<std::string> ids;
    for (const auto& id : p->unlocked_insights)
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    if (ids.empty())
        return;
    std::unordered_map<std::string, NodePos> pos;
    const Category cats[] = {Category::World, Category::Self, Category::Others};
    for (Category cat : cats)
    {
        const auto columns = buildCategoryColumns(ids, cat);
        if (columns.empty())
            continue;
        const Region r = regionFor(cat);
        const auto col_centers = placeCategoryColumns(columns, r, pos);
        emitCategorySeparators(cat, r, col_centers);
    }
    emitRegionBoundarySeparator();
    const auto visible_source_count = countVisibleSources(ids);
    for (const auto& id : ids)
        if (kindOf(id) == NodeKind::Inference)
            placeOneConclusion(id, pos, visible_source_count);
    pullConfirmersAdjacent(ids, pos);
    clampPositions(pos);
    cache = std::move(pos);
}

NodePos posOf(const std::string& node_id)
{
    auto& cache = sCache();
    auto it = cache.find(node_id);
    if (it == cache.end())
        return {};
    return it->second;
}

const std::vector<CategorySeparators>& separators()
{
    return sSeparators();
}

void reset()
{
    sCache().clear();
    sSeparators().clear();
}

} // namespace selva::insight::layout
