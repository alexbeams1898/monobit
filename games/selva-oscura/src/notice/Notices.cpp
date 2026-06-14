#include "notice/Notices.h"

#include "AppStateGlobal.h"

namespace selva::notice
{

namespace
{

// Compose the storage key. "<domain>:<id>". domain comes from the
// kDomain* constants in the header (or future ones); id is the
// per-domain stable string the caller already uses (item
// config_path, insight node id, etc.).
std::string key(const std::string& domain, const std::string& id)
{
    std::string k;
    k.reserve(domain.size() + id.size() + 1);
    k.append(domain);
    k.push_back(':');
    k.append(id);
    return k;
}

// Empty static returned by all() when no active profile, so the
// caller can iterate unconditionally.
const std::unordered_set<std::string>& emptyView()
{
    static const std::unordered_set<std::string> s_empty;
    return s_empty;
}

} // namespace

void mark(const std::string& domain, const std::string& id)
{
    PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr || domain.empty() || id.empty())
        return;
    p->unread_notices.insert(key(domain, id));
}

bool isUnread(const std::string& domain, const std::string& id)
{
    const PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr || domain.empty() || id.empty())
        return false;
    return p->unread_notices.count(key(domain, id)) > 0;
}

void acknowledge(const std::string& domain, const std::string& id)
{
    PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr || domain.empty() || id.empty())
        return;
    p->unread_notices.erase(key(domain, id));
}

const std::unordered_set<std::string>& all()
{
    const PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return emptyView();
    return p->unread_notices;
}

} // namespace selva::notice
