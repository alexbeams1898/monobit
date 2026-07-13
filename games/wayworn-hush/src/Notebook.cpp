#include "Notebook.h"

#include <algorithm>

namespace notebook
{

void record(Record& rec, observations::LineKind kind, const std::string& text,
            const std::string& faculty, int difficulty, int day)
{
    rec.entries.push_back(Entry{kind, text, faculty, difficulty, day});
}

std::vector<DayGroup> groupByDay(const Record& rec)
{
    std::vector<DayGroup> groups;
    // First pass in write order: land each entry in its day's group (creating one
    // on first sight), so within a day entries keep the order they were written.
    for (const auto& e : rec.entries)
    {
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&](const DayGroup& g) { return g.day == e.day; });
        if (it == groups.end())
        {
            groups.push_back(DayGroup{e.day, {}});
            it = std::prev(groups.end());
        }
        it->entries.push_back(e);
    }
    // Days ascending; the undated group (day 0) sorts LAST -- "when unknown" reads
    // after the dated days rather than before day 1.
    std::sort(groups.begin(), groups.end(),
              [](const DayGroup& a, const DayGroup& b)
              {
                  if (a.day == 0)
                      return false;
                  if (b.day == 0)
                      return true;
                  return a.day < b.day;
              });
    return groups;
}

} // namespace notebook
