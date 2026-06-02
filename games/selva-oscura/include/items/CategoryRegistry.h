#pragma once

#include <string>
#include <vector>

namespace selva::items
{

// One inventory category (tab in the inventory UI). Loaded from
// config/inventory_categories.json; ordered by `order` field. Adding
// / removing / renaming a category is a JSON-only edit.
struct CategoryDef
{
    std::string id;
    std::string display_name;
    int order = 0;
};

class CategoryRegistry
{
  public:
    void loadFromFile(const std::string& path);

    const std::vector<CategoryDef>& all() const { return ordered; }

    // Lookup by id. Returns nullptr if no category with that id is loaded.
    const CategoryDef* get(const std::string& id) const;

  private:
    std::vector<CategoryDef> ordered;
};

// Process-wide registry. Loaded once at boot.
CategoryRegistry& categoryRegistry();

} // namespace selva::items
