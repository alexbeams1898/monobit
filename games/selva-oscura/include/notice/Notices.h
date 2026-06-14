#pragma once

#include <string>
#include <unordered_set>

// Cross-domain "this is unread until you look at it" registry.
//
// One canonical place to ask "should I render a NEW badge on this
// thing?" -- whatever "this thing" is. Items use it for the gold
// dot on inventory rows. Insights use it for a future Mind sub-page
// node-badge. NPC topics, Grimoire entries, crafting recipes,
// region discoveries -- everything that has a "you discovered
// something" UX falls under the same shape so we don't grow
// parallel per-domain fields on PlayerProfile.
//
// Storage lives on PlayerProfile.unread_notices as an
// unordered_set<string> keyed "<domain>:<id>". Per-character; save-
// persisted; cleared on acknowledge. The notice module never
// reads/writes the set directly through any path other than these
// functions -- one site to audit if the storage shape ever changes.
//
// Discovery toasts (selva::ui::pushNotification) are a SEPARATE
// concern. mark() handles the persistent badge; the toast push at
// discovery time is the caller's responsibility. Same trigger,
// different storage.

namespace selva::notice
{

// Standard domain strings. Callers should use these constants
// rather than raw strings so domain-key collisions surface as
// compile errors. Adding a new domain = adding a constant here +
// using it in the new system; no other plumbing required.
inline constexpr const char* kDomainItem = "item";
inline constexpr const char* kDomainInsight = "insight";
// Future domains land alongside as they ship:
//   kDomainTopic = "topic";   (NPC dialog topics)
//   kDomainRecipe = "recipe"; (crafting)
//   kDomainBestiary = "bestiary";
//   kDomainRegion = "region";

// Mark a discovery as unread. Idempotent -- inserting an
// already-present key is a no-op. No-op if no active profile.
void mark(const std::string& domain, const std::string& id);

// True if this notice is in the unread set. The UI calls this to
// decide whether to render a NEW badge. False if no active profile.
bool isUnread(const std::string& domain, const std::string& id);

// Player has now seen this. Called on UI row click / node open /
// topic select / whatever the per-domain "the player looked at it"
// signal is. No-op if not currently in the unread set.
void acknowledge(const std::string& domain, const std::string& id);

// Read-only view of every unread notice for the active profile.
// Used by aggregate surfaces ("you have 3 unread observations")
// when those land. Returns an empty set when no active profile.
const std::unordered_set<std::string>& all();

} // namespace selva::notice
