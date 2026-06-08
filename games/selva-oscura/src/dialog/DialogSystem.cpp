#include "dialog/DialogSystem.h"

#include "AppStateGlobal.h"
#include "dialog/Encounter.h"
#include "dialog/Handlers.h"
#include "dialog/TopicRegistry.h"
#include "insight/Insight.h"
#include "lang/Language.h"
#include "text/TextPresentation.h"

#include <cstdio>

// Dialog system: a producer for the shared text-presentation layer.
// Owns conversation state (topic registry, show_when machinery,
// per-NPC encounter history); pushes content into selva::text when
// dialogs open, listens for input events via SessionHandlers, ends
// the text session when the conversation ends.
//
// The PANEL rendering lives in selva::ui::DialogScreen and reads
// from selva::text -- not from this file. Per the diamond-foundation
// rule, the dialog-specific machinery is decoupled from the
// rendering primitive so other producers (examine, future
// narration) feed the same panel.

namespace selva::dialog
{

namespace
{

struct RuntimeState
{
    bool is_active = false;
    std::string npc_id;
    std::string topic_id;
    ActiveTopicView view;
    // Pending intent queue. The text-layer handlers (invoked from
    // input) just SET these; tick() runs the actual transition. This
    // decouples UI render (read-only) from runtime state mutation,
    // so UI references into the view never get invalidated mid-render.
    // The just-began frame-guard is owned by selva::text, not here.
    int pending_choice = -1;      // index into view.choices; -1 = no click
    bool pending_advance = false; // confirmAdvance was requested
};

RuntimeState& state()
{
    static RuntimeState s;
    return s;
}

// "seen_topic_X" is a synthesized flag namespace. It looks up the
// active NPC's topics_seen set rather than profile.flags. Convention
// only: JSON authors write `flags_required: ["seen_topic_first_meeting"]`
// without knowing which storage backs it.
bool isSeenTopicFlag(const std::string& flag)
{
    return flag.compare(0, 11, "seen_topic_") == 0;
}

std::string seenTopicSuffix(const std::string& flag)
{
    return flag.substr(11);
}

bool evalFlag(const std::string& npc_id, const std::string& flag)
{
    if (isSeenTopicFlag(flag))
    {
        const std::string topic = seenTopicSuffix(flag);
        return selva::hasSeenTopic(npc_id, topic);
    }
    return selva::hasFlag(flag);
}

bool evalShowWhen(const std::string& npc_id, const ShowWhen& sw)
{
    for (const auto& f : sw.flags_required)
        if (!evalFlag(npc_id, f))
            return false;
    for (const auto& f : sw.flags_forbidden)
        if (evalFlag(npc_id, f))
            return false;
    if (!sw.custom.empty())
    {
        const auto* fn = getConditionHandler(sw.custom);
        if (fn == nullptr)
        {
            std::fprintf(stderr,
                         "[dialog] '%s': unknown condition handler '%s' (treating as fail)\n",
                         npc_id.c_str(), sw.custom.c_str());
            std::fflush(stderr);
            return false;
        }
        if (!(*fn)())
            return false;
    }
    return true;
}

// Resolve the speaker display name. NPC id -> registered NpcDialog's
// language-map key -> tier-gated text. Falls back to literal
// display_name if no key was authored. Empty / sentinel "narration" /
// "player" / "vagrant" -> empty string (UI renders unattributed).
std::string resolveSpeakerName(const std::string& speaker)
{
    if (speaker.empty() || speaker == "narration" || speaker == "player" || speaker == "vagrant")
        return std::string{};
    const NpcDialog* npc = topicRegistry().get(speaker);
    if (npc == nullptr)
        return speaker;
    if (!npc->display_name_key.empty())
        return selva::lang::resolve(npc->display_name_key);
    return npc->display_name;
}

// Build a text::ActiveTextView from a dialog topic. Used to push
// content into the text layer on topic enter / topic transition.
selva::text::ActiveTextView buildTextView(const ActiveTopicView& v)
{
    selva::text::ActiveTextView tv;
    tv.speaker = v.speaker_display_name;
    tv.line = v.line;
    for (const auto& c : v.choices)
    {
        selva::text::ActiveTextView::ChoiceView cv;
        cv.id = c.id;
        cv.label = c.label;
        cv.enabled = c.enabled;
        cv.disabled_reason = c.disabled_reason;
        tv.choices.push_back(std::move(cv));
    }
    return tv;
}

void rebuildView(const NpcDialog& npc, const Topic& topic)
{
    auto& rs = state();
    rs.view.npc_id = npc.npc_id;
    rs.view.topic_id = topic.id;
    rs.view.speaker_display_name = resolveSpeakerName(topic.speaker);
    rs.view.line = topic.line;
    rs.view.choices.clear();
    for (const auto& c : topic.choices)
    {
        ActiveTopicView::ChoiceView cv;
        cv.id = c.id;
        cv.label = c.label;
        if (evalShowWhen(npc.npc_id, c.choice_show_when))
        {
            cv.enabled = true;
        }
        else
        {
            cv.enabled = false;
            cv.disabled_reason = "Not yet available.";
        }
        rs.view.choices.push_back(std::move(cv));
    }
    // Push to text layer so the panel UI re-renders the new content.
    if (rs.is_active)
        selva::text::updateView(buildTextView(rs.view));
}

void markTopicSeen(const std::string& npc_id, const std::string& topic_id)
{
    PlayerProfile* profile = selva::activePlayerProfile();
    auto& enc = selva::npcEncounter(profile, npc_id);
    enc.topics_seen.insert(topic_id);
}

void fireAction(const std::string& key, const std::string& npc_id, const std::string& topic_id)
{
    if (key.empty())
        return;
    const auto* fn = getActionHandler(key);
    if (fn == nullptr)
    {
        std::fprintf(stderr, "[dialog] '%s' topic '%s': unknown action handler '%s' (no-op)\n",
                     npc_id.c_str(), topic_id.c_str(), key.c_str());
        std::fflush(stderr);
        return;
    }
    (*fn)(npc_id, topic_id);
}

// Enter a topic by id within the active NPC. Fires on_enter, marks
// topic seen, rebuilds view, fires the topic's insight unlock (if
// any). Caller must have validated topic_id exists.
void enterTopic(const NpcDialog& npc, const Topic& topic)
{
    auto& rs = state();
    rs.topic_id = topic.id;
    markTopicSeen(npc.npc_id, topic.id);
    rebuildView(npc, topic);
    fireAction(topic.on_enter, npc.npc_id, topic.id);
    // Topic-level insight unlock. Per the locked doctrine: entering
    // the topic at all (any path through it) is the reveal moment;
    // the node fires here, not at on_exit / on_select. setInsight is
    // idempotent so a repeat visit to the topic is harmless.
    if (!topic.unlocks_insight.empty())
        selva::setInsight(topic.unlocks_insight);
}

// Internal: actually apply a choice. Called from tick() with no UI
// caller holding view references.
void applyChoice(int choice_index);

} // namespace

void begin(const std::string& npc_id)
{
    auto& rs = state();
    if (rs.is_active)
    {
        std::fprintf(stderr, "[dialog] begin('%s') ignored: '%s' already active\n", npc_id.c_str(),
                     rs.npc_id.c_str());
        std::fflush(stderr);
        return;
    }
    const NpcDialog* npc = topicRegistry().get(npc_id);
    if (npc == nullptr)
    {
        std::fprintf(stderr, "[dialog] begin('%s'): NPC not in registry\n", npc_id.c_str());
        std::fflush(stderr);
        return;
    }
    const Topic* entry = nullptr;
    for (const auto& t : npc->topics)
    {
        if (!t.entry_point)
            continue;
        if (evalShowWhen(npc_id, t.show_when))
        {
            entry = &t;
            break;
        }
    }
    if (entry == nullptr)
    {
        std::fprintf(stderr, "[dialog] begin('%s'): no topic passes show_when; aborting\n",
                     npc_id.c_str());
        std::fflush(stderr);
        return;
    }
    rs.is_active = true;
    rs.npc_id = npc_id;
    PlayerProfile* profile = selva::activePlayerProfile();
    auto& enc = selva::npcEncounter(profile, npc_id);
    enc.times_talked += 1;
    // Insight: dialog opened with this NPC. tick() will fire any
    // node whose dialog_began trigger names this npc_id.
    selva::insight::notifyDialogBegan(npc_id);
    std::fprintf(stderr, "[dialog] begin '%s' -> entry topic '%s' (times_talked=%d)\n",
                 npc_id.c_str(), entry->id.c_str(), enc.times_talked);
    std::fflush(stderr);
    // Open the text-layer session. Handlers queue intents; tick()
    // consumes them. on_end fires when text::end() is called (either
    // from us, or from another producer's begin() displacing us).
    selva::text::SessionHandlers handlers;
    handlers.on_select_choice = [](int idx) { state().pending_choice = idx; };
    handlers.on_confirm_advance = []() { state().pending_advance = true; };
    handlers.on_end = []()
    {
        // Snapshot state, clear, then clear.
        auto& rs = state();
        if (!rs.is_active)
            return;
        rs.is_active = false;
        rs.npc_id.clear();
        rs.topic_id.clear();
        rs.view = ActiveTopicView{};
        rs.pending_choice = -1;
        rs.pending_advance = false;
    };
    // Enter topic FIRST so rs.view is populated; the topic-enter
    // rebuildView would call updateView(), but we want begin() to
    // be the first text-layer call. So we suppress updateView in
    // rebuildView via the is_active gate (we set is_active above,
    // so updateView WILL fire -- which is fine because text::begin
    // hasn't been called yet, and text::updateView is a no-op when
    // text::active() is false).
    enterTopic(*npc, *entry);
    selva::text::begin(buildTextView(rs.view), handlers);
}

void end()
{
    auto& rs = state();
    if (!rs.is_active)
        return;
    const NpcDialog* npc = topicRegistry().get(rs.npc_id);
    if (npc != nullptr)
    {
        for (const auto& t : npc->topics)
        {
            if (t.id == rs.topic_id)
            {
                fireAction(t.on_exit, rs.npc_id, rs.topic_id);
                break;
            }
        }
    }
    std::fprintf(stderr, "[dialog] end '%s' (was on topic '%s')\n", rs.npc_id.c_str(),
                 rs.topic_id.c_str());
    std::fflush(stderr);
    // Tearing down via text::end() invokes our on_end handler which
    // clears the rest of rs.
    selva::text::end();
}

bool active()
{
    return state().is_active;
}

const ActiveTopicView* currentView()
{
    const auto& rs = state();
    return rs.is_active ? &rs.view : nullptr;
}

void selectChoice(int choice_index)
{
    auto& rs = state();
    if (!rs.is_active)
        return;
    if (choice_index < 0 || static_cast<std::size_t>(choice_index) >= rs.view.choices.size())
        return;
    if (!rs.view.choices[static_cast<std::size_t>(choice_index)].enabled)
        return;
    rs.pending_choice = choice_index;
}

void confirmAdvance()
{
    auto& rs = state();
    if (!rs.is_active)
        return;
    if (!rs.view.choices.empty())
        return;
    rs.pending_advance = true;
}

namespace
{
void applyChoice(int choice_index)
{
    auto& rs = state();
    if (!rs.is_active)
        return;
    if (choice_index < 0 || static_cast<std::size_t>(choice_index) >= rs.view.choices.size())
        return;
    const auto cv_id = rs.view.choices[static_cast<std::size_t>(choice_index)].id;
    const NpcDialog* npc = topicRegistry().get(rs.npc_id);
    if (npc == nullptr)
    {
        end();
        return;
    }
    const Topic* cur_topic = nullptr;
    const Choice* chosen = nullptr;
    for (const auto& t : npc->topics)
    {
        if (t.id != rs.topic_id)
            continue;
        cur_topic = &t;
        for (const auto& c : t.choices)
        {
            if (c.id == cv_id)
            {
                chosen = &c;
                break;
            }
        }
        break;
    }
    if (cur_topic == nullptr || chosen == nullptr)
    {
        std::fprintf(stderr, "[dialog] applyChoice: lost reference to topic '%s' choice '%s'\n",
                     rs.topic_id.c_str(), cv_id.c_str());
        std::fflush(stderr);
        end();
        return;
    }
    fireAction(chosen->on_select, rs.npc_id, rs.topic_id);
    fireAction(cur_topic->on_exit, rs.npc_id, rs.topic_id);
    const std::string next = chosen->next_topic;
    if (next == "__end__")
    {
        std::fprintf(stderr, "[dialog] '%s' topic '%s' choice '%s' -> __end__\n", rs.npc_id.c_str(),
                     rs.topic_id.c_str(), chosen->id.c_str());
        std::fflush(stderr);
        selva::text::end(); // triggers our on_end which clears rs
        return;
    }
    if (next == "__same__")
    {
        rebuildView(*npc, *cur_topic);
        return;
    }
    for (const auto& t : npc->topics)
    {
        if (t.id == next)
        {
            std::fprintf(stderr, "[dialog] '%s' '%s' -[%s]-> '%s'\n", rs.npc_id.c_str(),
                         rs.topic_id.c_str(), chosen->id.c_str(), next.c_str());
            std::fflush(stderr);
            enterTopic(*npc, t);
            return;
        }
    }
    std::fprintf(stderr,
                 "[dialog] '%s' next_topic '%s' not found (validated at load -- this "
                 "should not happen)\n",
                 rs.npc_id.c_str(), next.c_str());
    std::fflush(stderr);
    end();
}
} // namespace

void tick()
{
    auto& rs = state();
    if (rs.pending_choice >= 0)
    {
        const int idx = rs.pending_choice;
        rs.pending_choice = -1;
        applyChoice(idx);
    }
    if (rs.pending_advance)
    {
        rs.pending_advance = false;
        if (rs.is_active && rs.view.choices.empty())
            end();
    }
}

void hardReset()
{
    auto& rs = state();
    rs.is_active = false;
    rs.npc_id.clear();
    rs.topic_id.clear();
    rs.view = ActiveTopicView{};
    rs.pending_choice = -1;
    rs.pending_advance = false;
    // text::hardReset is called by the caller (hardResetWorldForCharacter
    // tears down the text layer in its own pass).
}

} // namespace selva::dialog
