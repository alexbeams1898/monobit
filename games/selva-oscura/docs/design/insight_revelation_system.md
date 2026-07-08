# Insight revelation system — design direction

Captured 2026-06-05. **Pillar + architecture LOCKED 2026-06-09.**
Per-node content + unlock channels still WIP per node.

This doc captures the shape of the system and its implementation.
The PILLAR is locked (every player-facing string flows through the
language map; every reveal is gated by a named insight node; one
backend powers every surface). The per-node content (which strings
have which tiers, which triggers fire which nodes, how dense the
authoring becomes) is the ongoing authoring work.

## Implementation status

**v1 SHIPPED 2026-06-09.** Two modules + one extended profile field:

- **`selva::lang`** — [`include/lang/Language.h`](../../include/lang/Language.h). Reads
  `config/lang/*.json` into a tiered string map. `resolve(key)` returns the
  highest-unlocked-tier text; `isUnlocked(node)` is the seam (delegates to
  `hasInsight(node)`). Missing keys return `[lang:KEY]` so authoring bugs
  surface in-game.
- **`selva::insight`** — [`include/insight/Insight.h`](../../include/insight/Insight.h).
  Reads `config/insight/*.json` into a node graph. `tick()` runs per frame,
  evaluates each unlocked node's trigger, fires matching nodes into the
  active profile's `unlocked_insights` set. Event-source hooks
  (`notifyDialogBegan` / `notifyExamined` / `notifyKill`) live in the publisher
  sites (dialog::begin, examine on_interact, fireEnemyDeath).
- **`PlayerProfile.unlocked_insights`** + **`PlayerProfile.kill_counts`** —
  per-character storage; both round-trip through SaveManager. Per-character
  per the locked doctrine; new Vagrant starts fresh, knows nothing, learns
  by play. Reset on character create + cleared via the hardReset path.

**v1 trigger kinds** (in `selva::insight`):

| kind | JSON schema | fires when |
|---|---|---|
| `flag_set` | `{ flag: "..." }` | `hasFlag(p, flag)` is true |
| `dialog_began` | `{ npc_id: "..." }` | player has talked to this NPC at least once |
| `examined` | `{ mesh_debug_name: "..." }` | player has examined this static mesh at least once |
| `kill_count` | `{ archetype: "...", threshold: N }` | `kill_counts[archetype] >= N` |
| `sangue_accumulated` | `{ threshold: N }` | `sangue_lifetime >= N` |

**Wired surfaces today** (all route through `selva::lang::resolve()`):

- NPC dialog speaker name (via `NpcDialog::display_name_key` in `config/npcs/*.json`)
- NPC talk-prompt label (via `EnemyArchetype::display_name_key` in `config/enemies/*.json`)
- Boss HP-bar name (via `EnemyArchetype::boss_name_key`)
- Boss felled-overlay message (via `EnemyArchetype::felled_message_key`)
- Static-mesh examine prompt label (via region.json `examine_label_key`)
- Static-mesh examine prose body (via region.json `examine_text_key`)
- Door interact label (via region.json `doors[].label_key`)

**Interact-prompt verbs are noun-stripped** as of v1: the prompt shows only
"Talk" / "Examine" / "Open" / "Pickup" / "Use" — the noun would name things
the player may not have insight for yet. The label is still computed
(used for telemetry + future) but never rendered.

**Authored nodes today** (3 nodes, 5 lang entries):

| node id | trigger | promotes |
|---|---|---|
| `knows_guide` | `dialog_began:guide` | dialog speaker `???` → `Guide` |
| `knows_lupa` | `flag_set:lupa_felled` | HP-bar `???` → `LUPA`; felled overlay `FELLED` → `LUPA FELLED` |
| `knows_dissolved_souls` | `examined:acheron_pile` | pile label `the pile` → `a heap of the dissolved`; prose body deepens |

**Not yet authored** (the language map supports them; entries just need writing):

- Sangue-related strings (substance has no in-game name per
  [[project_substance_has_no_in_game_name]] — tier-0 sensory only, NEVER
  a tier-2 reveal)
- Region chip labels
- Item descriptions
- Picker / pause-menu / dialog choice labels (these have no tier-2 leaks today
  so literal-fallback works; route through the lang map as authoring grows)
- Grimoire fragments (system itself not yet built)

This doc captures the shape of an idea AND its implementation. Some
content details (per-node text variants, unlock channels for nodes
not yet authored, density of micro-unlocks) are genuinely open. The
ARCHITECTURE is what's defended; per-node mechanics underneath are
negotiable.

## The pillar

**The Vagrant's understanding of the world grows through play, and
EVERY informational surface in the game reflects that growing
understanding.** One revelation system, applied consistently across:

1. **Examine text** on world objects (the original surface)
2. **HUD and meta-UI** elements (resource labels, stat names, region
   chip, etc.)
3. **Item descriptions** (inventory tooltips, on-pickup text, use
   prompts)
4. **Action-driven micro-unlocks** — small revelations that fire
   when the player performs meaningful actions (use an item for
   the first time, kill the first enemy, commit substance via the
   vessel for the first time, examine a specific waypoint)
5. **Dialog tree depth** — NPC speech surfaces and unlocks reflect
   what the player knows
6. **Future surfaces** — anything we add later (map system, journal,
   bestiary, codex) plugs into the same system

The whole game becomes "the longer you play, the more legible the
world becomes" — at every layer. The player learns there is a
system that progressively unveils, and that lesson pays off
everywhere.

## Why this matters cosmologically

- The Vagrant arrives without vocabulary for what he's seeing. UI /
  dialog / items that LABEL things he can't yet name is lying.
  Progressive naming is honest.
- Per [[project_selva_epistemic_doctrine_2026_05_31]] — Selva does
  not tell the player they are in Hell. Selva also should not tell
  the player what their resources are, what their stats mean, or
  what they are accumulating. The world is for them to read.
- Per [[project_contrapasso_law_vs_substance]] — the cosmology is
  invisible to its participants. The Vagrant gains the framework
  for understanding only through play. Every revelation channel
  mirrors that.

## The shared backend (load-bearing architectural commitment)

**One data-driven node graph powers every surface.** Each "thing the
player might come to know" is a node in the graph. Each node has:

- A stable id (`knows_sangue`, `knows_ferryman`, `knows_crucible_use`,
  `first_kill_felt`, etc.)
- Unlock conditions (any-of-list; one path fires the unlock)
- Effects to fire when unlocked (text updates, HUD label changes,
  item description ticks, dialog tree branches, etc.)
- Status (locked / unlocked) persisted to player profile

This means:

- **Examine text** reads its tier-list, checks which nodes are
  unlocked, displays the highest-tier text whose conditions are met
- **HUD label rendering** asks "is `knows_sangue` unlocked? if yes,
  show 'sangue' label; if no, show the icon-only chip"
- **Item descriptions** are tier-lists like examines — same lookup
- **Action-driven micro-unlocks** are nodes whose unlock condition
  is "actor performed action X" — firing them is just setting the
  flag, and the next time any surface checks, the new content shows
- **Dialog handlers** check the same node states to decide which
  topics are unlockable

No surface has its own bespoke revelation logic. They all read from
the same graph. This is the load-bearing architectural commitment —
every new surface added later uses the SAME nodes, the SAME unlock
conditions, the SAME persistence path.

**Why this matters more than the aesthetic:**

If revelation logic lives per-surface, the surfaces drift. The HUD
might say "sangue" while an item description still says "the strange
substance" because two different gating checks got out of sync. With
one node graph, that's structurally impossible. When `knows_sangue`
flips, every surface that depends on it updates atomically. Same
backend = no drift = no [[feedback_dual_source_of_truth_is_the_bug]].

## Multi-channel unlocks

Insight nodes can be unlocked through MULTIPLE channels. ANY of them
firing flips the node; the player never sees the mechanism, they
just experience the revelation. This makes different playstyles
converge:

- **Story beats** — Beat 4 class-pick, R1 Guide-reveal, keeper-fall
  events, scripted dialog moments
- **Accumulation thresholds** — read N tier-1 examine texts, kill
  K shades, accumulate X sangue, visit Y regions
- **Specific items / discoveries** — finding a tutorial-grimoire
  fragment that "teaches" the player symbols
- **Commit events** — first commit (either outcome), first
  successful installation / riversamento
- **Action-driven micro-unlocks** — small "you did this for the
  first time" triggers (first kill, first item use, first death,
  first vessel commit, first conversion-event observed)

The careful explorer, the rusher who blitzes story beats, and the
hoarder who collects every item ALL unlock the same insights —
proportional to engagement, not to a single specific behavior.

**Open question:** OR-gating (any path fires) vs AND-gating (must
hit multiple paths). OR-gating for v1 — lower friction, easier to
author. Deep cosmological revelations (the Beatrice-truth, the
Lucifer-act-shape) may reserve AND-gating.

## Action-driven micro-unlocks (the early-game engine)

The early game has a high authoring density of micro-unlocks. Most
meaningful actions the player takes in the first 2-3 hours produce
a small revelation:

- First item use → that item's description advances
- First kill → sangue-counter mechanic surfaces visibly
- First vessel commit → the installation / riversamento mechanic
  surfaces
- First examine on a cosmologically-loaded object → examine tier 1
  unlocks on that object
- First conversion event observed (larva fresh → aged after the
  feeding loop completes) → related cosmology node unlocks

This is modeled after real-life learning: in any new domain, you
have small understanding-shifts constantly, especially early. The
rate naturally tapers — by mid-game most operational systems are
understood and unlocks become rarer but cosmologically deeper. The
revelation CURVE mirrors how skill acquisition actually feels.

**Design discipline this requires:**

- Not every action is an unlock. Some examines are just flavor;
  some item uses just consume the item. The distribution must be
  unpredictable enough that the player can't algorithmically farm
  unlocks. They have to be playing genuinely.
- The early-game high density means a lot of authoring. Mitigated
  by the shared-backend commitment: adding an unlock is editing one
  data entry, not adding code.
- Each unlock must arrive at a moment that feels earned and
  revelatory. Drip-feeding tiny meaningless unlocks devalues all of
  them.

## Two-layer architecture

1. **Knowledge layer (class-agnostic):** the insight graph. Nodes
   are concepts and event-flags. Edges are unlock conditions. Same
   data for both Vagrants. Same information content.

2. **Presentation layer (class-aware):** HOW an unlock arrives. The
   aesthetic delivery differs by class because the cosmological
   condition of each Vagrant differs:

   - **Class-picker (imprint-bearing):** information arrives by
     INSCRIPTION. The HUD evolves gradually because the Vagrant is
     being shaped by what they absorb. Tier-promotions read as "the
     world is being written into me." HUD elements gain density and
     detail over time; new labels fade in.

   - **Unburdened (substance-passing-through):** information
     arrives by PASSAGE. The Vagrant has no imprint to inscribe.
     Revelations may arrive as ECHOES of Beatrice — fragmentary,
     intrusive, sudden rather than gradual. The HUD might artifact
     briefly during tier-promotions (Beatrice's voice pressing
     through). Over runs the HUD itself thins out (matches the
     unburdened's subtractive evolution Svuotato → Diaphanous)
     even as it gets MORE articulate — knowing more while becoming
     less.

Same content, different cosmological aperture. Both Vagrants end up
with the same information delivered. Neither path is "lesser."

**Open question:** how much per-class divergence is worth the
authoring cost? Steady-state-divergent (every UI element has two
presentations) is the strong version. Promotion-moment-only (only
the unlock animation differs) is the weak version. Probably start
weak and escalate if it reads thin.

## Tier-0 doctrine

All tier-0 surfaces must be **sensory or functional, never
interpretive.** The Vagrant sees shapes, counts, fills, motion.
Cosmological vocabulary is locked behind insight.

Examples:
- Sangue counter: tier-0 = visible accumulating substance (icon
  or fill meter) + an unlabeled roman numeral count. The player
  sees something ticking up. No word for it.
- HP: tier-0 = a fill meter. Not labeled "health" or "vitality."
  Just a state-of-vessel indicator.
- Region chip: tier-0 = a glyph denoting "where am I" without
  naming the region cosmologically.
- Items: tier-0 description is sensory ("a strange vessel that
  hungers") + an action affordance ("Use") that does something
  visibly transformative even if the player doesn't know why.

What lives in tier-0 must be **enough to play the game.** The
legibility floor is real — the player needs to read combat state,
resource state, location at-glance. The thing being gated is
NAMING, FRAMING, COSMOLOGICAL CONTEXT — not underlying state
visibility.

## Pillar: foreign language is load-bearing, never decoration

This is its own pillar, applicable to ALL authoring in Selva, not
just the revelation system. Tier discipline below is one expression
of it; the principle stands on its own.

**Any non-English word that appears in any surface of Selva — UI,
item descriptions, dialog, examine text, lore docs, audio, region
names, proper names — must be USED ACCURATELY AND WITH PURPOSE
GROUNDED IN COSMOLOGY.**

What this means in practice:

- **Accurate.** Spelled correctly, pluralized correctly (vestigium /
  vestigia, sangue is uncountable — never "sangues"), used in
  grammatically correct phrases. If the author isn't sure, look it
  up. If the lookup is uncertain, use English.
- **With purpose.** The word must do work that English can't. If the
  English equivalent carries the same meaning, use English. "Sangue"
  has work to do because it names an alchemical / cosmological
  substance, not ordinary blood. "Spada" doesn't, because "sword"
  works.
- **Grounded in cosmology.** The word ties into the locked lore.
  Italian terms for Hell-substance cosmology (sangue, contrapasso,
  riversamento). Italian terms for the Commedia inheritance (selva
  oscura, vestigia). Italian terms for proper names of cosmologically
  specific things (Lonza, Leone, Lupa as named individuals per
  [[project_selva_wood_lore_locked_2026_05_31]]).

**What this is NOT:**

- NOT a license to sprinkle Italian for flavor. "Buongiorno" in NPC
  dialog without cosmological justification is forbidden.
- NOT a requirement that every cosmological concept get an Italian
  name. Most things stay English. Only the genuinely untranslatable
  cosmological substrates earn foreign names.
- NOT decoration. If the word is doing nothing but signaling "this
  game is Italian-flavored," delete it.

**Failure mode to avoid:**

The "American game scattering Italian for atmosphere" trap. Every
foreign word that doesn't earn its keep advertises that the author
chose decoration over discipline. Selva's defense is to use Italian
ONLY where it's load-bearing, and to gate even the load-bearing terms
behind tier-2 revelation (below) so they arrive as discovery, not
as costume.

**Smell phrases that mean the rule is being violated:**

- "It would sound cooler in Italian"
- "Let's call it sangue instead of blood"
- "This NPC speaks a little Italian for flavor"
- "An Italian word here would feel period-accurate"

If the case for the term is "feel" or "flavor" or "sound," it's
decoration. Use English. If the case is "this concept genuinely
doesn't translate without losing texture, and the cosmological
register depends on it," it's load-bearing. Keep it.

## The substance has no in-game name

**This is its own pillar.** Stronger than the tier discipline below.
Applies specifically to the cosmological substrate (what the lore
calls *sangue*).

**The rule:** In every diegetic surface — HUD, item descriptions,
NPC dialog, vestigium text, examine text, Grimoire fragments,
scripted scene dialog, ambient audio — the substance is referenced
by **function, metaphor, or effect**, NEVER by name. The word
"sangue" does NOT appear in any string the player encounters. Not
at tier 0, not at tier 1, not as a tier-2 reveal. Never.

**Why this is asymmetric to other Italian terms:**

Other locked Italian terms MAY reveal at tier 2 because characters
in the world have access to them:

- **vestigia** — physical waypoints; characters / inscriptions name them
- **contrapasso** — the law of each ring; keepers and lore-bearers might use it
- **riversamento** — a verb the unburdened performs; Beatrice-channel may name it

But **sangue specifically** never reveals because **no one in the
cosmology has the position to name the substance itself**. The
Vagrant arrives without that vocabulary. The Guide doesn't know what
he is. NPCs are damned souls who know they're suffering but not why.
Beatrice doesn't speak. None of them have the cosmological vocabulary.

The substance can be DESCRIBED — what it does, where it goes, what
fills with it — but it cannot be NAMED. Naming it would require a
character with the meta-position to do so, and that character does
not exist in this world.

**What characters CAN say:**

- "Something fills you when you strike."
- "What you carry — pour it out, or let it become you."
- "The wound holds substance. Hell wants it back."
- "There is a thing the dead release."
- "Here the unaccounted is committed."

**What characters CANNOT say:**

- "You carry sangue, Pilgrim."
- Any direct reference by the proper name.
- Any tier-2 "the word for this is X" reveal.

**Authoring discipline:**

When writing any string the player will see, ask: "does this name
the substance?" If yes, rewrite. Use one of:
- A metaphor ("the blood of Hell," "what flows when souls collapse")
- A function ("what fills the wound," "what is held briefly")
- An effect ("what you carry until you commit it," "what Hell takes
  back if you fall")
- A pronoun-like construction ("the substance," "what is carried")

**What the meta-frame (designer-side) uses freely:**

- Lore docs (setting.md, this doc, design files)
- Internal authoring conversations
- Code comments / variable names (`sangue_wallet`, `applySangueOnKill`)
- Commit messages
- Cross-references in design discussions

The word lives in the cosmology's authorial frame. It's load-bearing
where it appears (in the author's vocabulary). The pillar above is
satisfied because "sangue" earns its place in the meta-frame — it
just never crosses into the diegetic.

**What this gets us:**

- Cosmological consistency — the cosmology is invisible to
  participants
- Honest modeling of real learning — operational understanding
  ≠ vocabulary acquisition
- Reward for the dedicated reader without penalty for the casual
  player
- Defends against vocabulary-unlock anticlimax
- Mirrors strong modern game writing (Bloodborne's most central
  entities are often the most opaque)

**Smell phrases that mean the rule is being violated:**

- "The substance you carry is called sangue."
- "Tier 2: 'sangue' label appears on the HUD"
- "At this story beat, the player learns the word"
- "It would feel rewarding to name it eventually"
- Any item / dialog / examine string containing the literal word "sangue"

If any of these appear in authoring, STOP. The rule is uniform and
permanent: the substance has no in-game name.

## Language register doctrine — Italian terms are tier-2 revelations

Selva is set in the Commedia's cosmology and uses period-Italian
register, but the game ships in English. Italian terms are NOT
sprinkled as flavor; they earn their place by being cosmologically
distinct concepts that English doesn't carry cleanly. Across the
revelation tiers, language operates as follows:

- **Tier 0 (no insight):** sensory English only, no proper names
  for cosmological substances. The substance the player is
  accumulating is referred to (if at all) by metaphor or by what
  it visibly does — "what fills the chalice," "the dark substance,"
  "what flows when the dead are struck." Never "sangue" yet, never
  even "blood of Hell" as a settled phrase.

- **Tier 1 (operational understanding):** English gloss for the
  concept. The substance becomes "the blood of Hell" or similar
  named-in-English phrase. The player understands what it is
  functionally. Vocabulary is settled but still naturalistic.

- **Tier 2 (cosmological understanding):** the canon Italian term
  arrives as a revelation. "Sangue" appears — framed as the proper
  name for the substance the player has been carrying the whole
  game. Same for *contrapasso*, *vestigia*, *riversamento*, etc.
  The Italian word anchors the cosmology because the player has
  EARNED the name.

**Why this works:**

- The Vagrant doesn't know the cosmological vocabulary at game
  start. Putting "sangue" on the HUD at hour one would be lying.
  Putting the English gloss at tier 1 teaches without naming.
  Letting the Italian word arrive at tier 2 makes the vocabulary
  itself part of what the player unlocks.
- It defends against the "American game scattering Italian for
  decoration" failure mode. Italian terms earn their tier-2 slot
  by being genuinely untranslatable concepts. English does the
  early-game work.
- It mirrors how readers of the Commedia historically encounter
  the vocabulary — first as concept (in translation), later as the
  specific Italian word that anchors it.

**Discipline rule for authoring:**

Before using any non-English term in any surface, ask: can English
carry this without losing cosmological texture? If yes, use English.
If no, the term belongs at tier 2.

The few non-English terms that legitimately earn tier-2 slots in
Selva's cosmology:
- **sangue** — substance of Hell (cruor / vital essence, alchemical
  register, untranslatable as "blood")
- **contrapasso** — matched-to-sin punishment law (untranslatable
  as a single word)
- **vestigia** — pilgrimage trace / save-shrine ("vestige" decayed
  in English; original carries pilgrimage and footprint connotations)
- **riversamento** — outpouring toward Beatrice (specific
  Catholic-mystical texture "outpouring" doesn't carry)
- **selva oscura** — game title; proper name
- Other proper names of specific cosmological things will be
  documented as they're authored

**What this is NOT:**

- NOT a ban on Italian. It's a *tier discipline.* Italian arrives
  at tier 2.
- NOT a requirement that every cosmological concept get an Italian
  name. Most things stay English forever. Only the genuinely
  untranslatable cosmological substrates earn Italian terms.
- NOT a teaching obligation — the game does not gloss "sangue =
  blood of Hell" anywhere. The player infers it from context across
  hundreds of small encounters between tier 1 and tier 2.

## A worked example: sangue and the level-up vessel

**Game start (tier 0, no insight unlocked):**

- The Vagrant has a strange vessel item in inventory (delivered by
  Beatrice's impulse, possibly via the Guide but possibly already
  on the Vagrant at boot — TBD)
- Item description: "A strange vessel that hungers." Action: "Use"
- HUD shows an unlabeled icon with a roman-numeral count at 0
- Player opens inventory, presses Use on the vessel
- A stat-spend menu opens. Stats are listed but the resource
  counter at the top reads 0 / nothing spendable. The player can
  see the STRUCTURE ("spend X to gain Y") without being told what
  X is
- Player closes menu. They've learned: "this thing wants something."

**First kill (action-driven micro-unlock fires):**

- Player kills first enemy
- The HUD counter ticks visibly from 0 → N
- A small first-time felt-cue plays: visual artifact, sound,
  whatever — the player FEELS something happen
- Node `first_kill_felt` unlocks
- The vessel's description updates: "A strange vessel. Something
  has filled it." Action: "Use"

**First use with resource (action-driven micro-unlock):**

- Player opens inventory, presses Use on the vessel
- Stat-spend menu opens with the resource counter > 0 and at
  least one stat spendable
- Player spends, stat advances, resource counter decrements
- Node `first_vessel_use` unlocks
- Vessel description updates further: cleaner, more confident
  phrasing, but still pre-cosmological ("A vessel that fills as
  you strike, and gives in turn what is paid into it.")

**Mid-game (`knows_sangue` unlocks via story / accumulation / item):**

- HUD icon gains a textual gloss "blood of Hell" or similar
- Vessel description references it directly
- Other items / dialog / examines start cross-referencing
- The system now feels FRAMED to the player even though they don't
  yet have the word "sangue"

**Late game (`knows_sangue_cosmology` unlocks):**

- HUD label resolves to "sangue"
- Vessel description gives the full cosmology
- Tooltips connect sangue to weapon XP, vestigia, the
  Beatrice/riversamento system
- The player now sees the system as the system

## Authoring guide

Practical reference for adding strings + nodes to the shipped v1
system.

### The implied-lesson doctrine (LOCKED 2026-06-09)

This is the load-bearing authoring discipline that governs every
lang map string and every insight node in the game. Read it before
writing any player-facing prose.

**The principle:** an insight unlock is not "the player encountered
a thing." It is **the specific cognitive conclusion a paying-
attention person would draw from the encounter, given everything
else they have already concluded.**

The trigger fires when an event happens. The lang-map text that
promotes is the player's articulation of *what that event taught
them, in their voice, with only the vocabulary they have earned*.

**Three questions for every authored unlock:**

For each new insight node + its lang strings, the author must
answer in writing:

1. **What did the player just do or just see?** (the trigger event)
2. **What is already in their head when they did or saw it?** (the
   prerequisite insight state — what else has fired)
3. **Given #1 and #2, what is the smallest, most operational thing
   a person who had been paying attention would conclude?** (the
   tier-N text that promotes)

If the tier-N text says more than #3 — uses cosmology vocabulary
the player has no business knowing yet, asserts framing the player
could not derive from the encounter alone — it is overpromoted and
must be rewritten.

**Worked example: first kill.**

- *What did the player do?* Struck a thing. The thing died.
  Something moved from it to them. A counter that wasn't there
  before appeared on the HUD.
- *What was already in their head?* Nothing relevant. This is the
  Vagrant's first kill; no prior insights about absorption / sangue
  / vessel exist.
- *What would they conclude?* That killing a thing yields some
  count. They don't know what the count is, where it goes, or what
  it does — only that the act of killing produces it.

Wrong tier-1: "The sangue of the felled accrues in thy vessel."
That uses three terms (sangue, felled, vessel) the player has not
earned.

Right tier-1: "Killing gave me a count." Operational. The player's
voice. No cosmology asserted.

The cosmology terms (sangue, vessel, riversato) arrive only when
other insights have fired that teach them — usually through Guide
naming, examined-thing reveals, or item-text gates per the existing
multi-channel unlock doctrine.

**Worked example: examining the Acheron pile after watching the
feeding cycle.**

- *What did the player do?* Approached the heap of person-shapes
  on the shore and looked at it.
- *What was already in their head?* The player has already watched
  larvae crawl out of the river, has examined a fresh larva, has
  watched aged larvae feed on dead ones. The feeding-cycle insight
  has fired.
- *What would they conclude?* That this is where the things they
  saw earlier accumulate. They saw the feeding; now they see the
  source of the food.

The tier-1 here is not "souls land at Acheron and dissolve" — the
player has no concept of "soul" or "Acheron" yet. It is something
like "this is where they pile up; the feeders take from this." The
player's plain accounting of what they have now seen.

**Why this matters:** without the implied-lesson discipline,
authoring drifts toward exposition — explaining the world TO the
player through tier-1 text rather than letting the player articulate
what they have come to understand. The doctrine forces the author
to write in the player's voice at their current state, not the
designer's voice from omniscience.

**How this composes with the multi-channel + tier system:** the
same insight node may be reachable from multiple triggers. The
implied-lesson differs by which trigger fired and what state the
player was in when it fired. Authors may need to write multiple
tier-1 variants for the same node, one per plausible discovery
path. The lang map can gate variants on additional prerequisite
nodes (`unlock_node_tier_1` + an additional prerequisite list).

### Adding a new player-facing string

1. **Pick a stable key.** Convention: `<domain>.<thing>.<aspect>`. Domain is
   the JSON file the entry lives in (`world`, `interact`, future `ui` /
   `items` / etc.). Examples: `world.acheron_pile.examine_label`,
   `interact.npc.guide.display_name`, `interact.boss.lupa.felled_message`.
2. **Author the entry** in the appropriate `config/lang/<domain>.json`:
   ```json
   "world.my_thing.examine_text": {
     "tier_0": "A sensory description with no cosmological vocabulary.",
     "tier_1": "An operational English gloss when the player understands what it is.",
     "tier_2": "The full canon, possibly with the proper Italian term.",
     "unlock_node_tier_1": "knows_my_thing",
     "unlock_node_tier_2": "knows_my_thing_cosmology"
   }
   ```
   `tier_0` is REQUIRED. `tier_1` / `tier_2` are optional. `unlock_node_tier_1` /
   `unlock_node_tier_2` are the node ids that promote the entry; both come from
   `config/insight/*.json`.
3. **Reference the key from code or schema.** The right field depends on what
   surface this string lives on:
   - **NPC display name (dialog speaker)**: add `display_name_key` to
     `config/npcs/<id>.json` AND `config/enemies/<id>.json` (the dialog system
     reads the former, the talk-prompt the latter; both fields point at the
     same lang key).
   - **Static-mesh examine prompt**: add `examine_label_key` + `examine_text_key`
     to the mesh entry in `assets/regions/<region>/region.json`.
   - **Door label**: add `label_key` to the door entry in the region's `doors[]` array.
   - **Boss HP-bar / felled overlay**: add `boss_name_key` + `felled_message_key`
     to `config/enemies/<archetype>.json`.
   - **New surface**: pass the lang key through to wherever the string is rendered;
     resolve with `selva::lang::resolve(key)` at render time. Prefer resolving
     each draw so tier promotions update live (the boss HP-bar does this; see
     `BossHud.cpp::resolveBossName`).
4. **Test**: launch the game, find the surface, confirm the tier-0 string
   shows. If you see `[lang:KEY]` the key isn't registered (typo or wrong
   JSON file). If you see the wrong tier, the unlock node isn't firing
   (check `selva-oscura.log` for `[insight] node fired:` lines).

### Categories (LOCKED 2026-06-09)

Every insight node declares a category. Categories drive the Mind
sub-page on the pause-menu Vessel tab: fired insights group under
their category heading so the player can see the shape of their
accumulated understanding.

**Three categories (start; add more only when content genuinely
doesn't fit):**

- `world` — what the Vagrant has come to understand about the place
  he's in (the pile, the larvae, the wood, future encounters).
- `self` — what he's come to understand about his own form (kills
  yield count, he holds substance, his body changes when he
  commits, etc.).
- `others` — what he's come to understand about specific beings he
  has met (the Guide, future NPCs, named keepers).

**Schema:** add `"category": "world" | "self" | "others"` as a
top-level field on the node JSON, alongside `trigger` and
`_comment`. Missing or unknown values log loudly and the node will
not appear on the Mind sub-page.

```json
"knows_arrival_queue": {
  "category": "world",
  "trigger": { "kind": "examined", "subject": "foundling" }
}
```

The Mind sub-page itself tier-gates its presentation in the future
(currently flat per-category headers). The category set is closed
for v1; expanding it requires a doctrine update because surfaces
that render categorized insight (Mind, future region overviews,
etc.) hardcode the three values.

### Adding a new insight node

1. **Pick a node id.** Convention: `knows_<thing>` for name-reveals (Guide,
   Lupa, etc.); `<verb>_<thing>` for action-driven micro-unlocks
   (`first_kill_felt`, `first_vessel_use`). Lowercase, underscore-separated.
   Node ids are SAVE SCHEMA fields — renaming breaks existing saves AND every
   `unlock_node_tier_*` reference in the language map. Treat them like flag
   names.
2. **Author the node** in the appropriate `config/insight/<domain>.json`
   (`npcs.json`, `bosses.json`, `world.json`, future `items.json` / etc.):
   ```json
   "knows_my_thing": {
     "_comment": "Fires when X happens.",
     "category": "world",
     "trigger": {
       "kind": "examined",
       "subject": "my_thing"
     }
   }
   ```
   Pick the trigger kind from the v1 table above. Pick the category
   from the three-value set above. Each trigger kind has its own
   required fields; missing fields log loudly and the node is
   skipped on load. Missing category logs but still loads (the node
   just won't appear on the Mind sub-page).
3. **Verify the trigger source is wired.** Most trigger sources are already
   hooked into the event publishers (dialog::begin, examine on_interact,
   fireEnemyDeath). If you're triggering on a new kind of event, add a
   `selva::insight::notify*` hook at the event site OR (simpler) have the
   event set a gameplay flag and use `flag_set` as the trigger kind. Flags
   are the universal lingua franca; any new bookkeeping flag becomes an
   insight trigger for free.
4. **Reference the node from a language entry.** The node exists to gate
   tier promotions; add an `unlock_node_tier_1` (or `_2`) field on the
   language entry that should reveal when this node fires.
5. **Test in-game**: trigger the action, watch for `[insight] node fired:`
   in the log. If the node never fires, check the publisher hook is in
   the right code path; if the language entry doesn't promote after firing,
   check the unlock node id is spelled identically in both files.

### The examine-as-insight doctrine (LOCKED 2026-06-09)

**Every examinable thing in the game fires an insight node on first
examine.** This is a hard rule, not a suggestion:

- Every static mesh with `examine_text` / `examine_text_key` in
  `assets/regions/<region>/region.json` MUST have a corresponding
  insight node in `config/insight/world.json` with an `examined`
  trigger naming the mesh's `debug_name`.
- Every actor archetype with `examine_text` / `examine_text_key` in
  `config/enemies/<id>.json` MUST have a corresponding insight node
  with an `examined` trigger naming the archetype's `id`.

If you add an examinable without an insight node, the examine still
works (the prose shows) but nothing in the world responds to the
player having looked at it. That's a missed authoring opportunity —
the examine becomes a dead end. Per the doctrine, examines ARE the
primary information-loop driver in the early game: every look
unlocks something elsewhere (a Guide topic, a deeper tier on the
examined thing itself, a region-chip reveal, etc.).

### The two-stage reveal pattern

The pattern that drives most cosmological reveals in the world:

1. **Stage 1 (player examines)** — examine fires the node
   `knows_<thing>`. Lang map promotes the examined thing's tier-1
   (the player's solo observation lands).
2. **Stage 2 (Guide names)** — player returns to the Guide; a
   topic gated on `examined:<thing>` becomes eligible. Entering the
   topic fires `guide_named_<thing>` via the topic's
   `unlocks_insight` field. Lang map promotes the examined thing's
   tier-2 (the cosmological frame the player's solo observation
   couldn't reach).

The pattern is the foundation of the Guide's role: he's the
cosmological-naming layer, fed exactly enough by Beatrice's channel
to interpret what the Vagrant has already seen. Every Guide-side
reveal topic should follow this two-stage shape — gated on a
world-side examine, firing a Guide-named node, promoting a
language-map entry.

Schema for the two halves:

```json
// config/insight/world.json -- triggered node
"knows_arrival_queue": {
  "trigger": { "kind": "examined", "subject": "foundling" }
}

// config/npcs/guide.json -- topic that fires the Guide-named node
{
  "id": "guide_reveals_arrival_queue",
  "entry_point": true,
  "show_when": {
    "flags_required": ["seen_topic_post_signing_first_words",
                       "examined:foundling"],
    "flags_forbidden": ["seen_topic_guide_reveals_arrival_queue"]
  },
  "unlocks_insight": "guide_named_arrival_queue",
  "line": "...",
  "choices": [...]
}

// config/lang/world.json -- entry with both tier promotions
"world.foundling.examine_text": {
  "tier_0": "...",
  "tier_1": "...",
  "tier_2": "...",
  "unlock_node_tier_1": "knows_arrival_queue",
  "unlock_node_tier_2": "guide_named_arrival_queue"
}
```

### Manually-set insight nodes

Insight nodes come in two flavors:

- **Triggered nodes** (the common case) live in `config/insight/*.json`
  with a declarative trigger. The graph evaluates them per frame.
- **Manually-set nodes** are fired directly from code via
  `selva::setInsight(node_id)` OR from a dialog topic's
  `unlocks_insight` field. They have NO declaration in the insight
  JSON; their existence is implied by the unlock site.

The `guide_named_*` nodes are the canonical manually-set case —
they're fired from dialog topics, not by a world trigger. Both
flavors land identically in `unlocked_insights` and both are looked
up identically by `selva::lang::resolve()`. Authors don't need to
declare manually-set nodes anywhere; the convention is "if no
config/insight entry exists for a node id, the node is set
manually." Grep for the node id in `config/npcs/` to find where it
fires.

Use the manually-set pattern when:
- The unlock condition is conversational (a topic was reached) rather
  than world-observable.
- The unlock is a downstream consequence of another insight (you only
  fire `guide_named_X` after `knows_X` has fired AND the player has
  re-engaged the Guide).

Use the triggered pattern when:
- The unlock is a direct response to a world event the gameplay code
  already publishes (kills, examines, flag-sets, sangue thresholds).

### What NOT to do

- **Don't hardcode player-facing strings in code.** Every literal "the
  Guide", "LUPA", "the pile", etc. is a future drift bug. Route through
  the lang map; literal-fallback fields exist on the schemas but are the
  exception, not the rule.
- **Don't tier-gate strings that have no insight to reveal.** Some strings
  are tier-0 forever (the literal verbs "Talk" / "Examine"; UI chrome like
  "Save" / "Quit"). Author them with only `tier_0` (no unlock nodes); they
  resolve to that text forever. Don't invent a "knows_save_command" node.
- **Don't author tier-2 sangue strings.** Per [[project_substance_has_no_in_game_name]],
  the substance is referenced by function/metaphor/effect at every tier;
  the proper name never reveals. Treat sangue as the canonical "tier-0 only,
  no reveal" case.
- **Don't add new trigger kinds for one-off cases.** If a node is hard to
  express in the v1 5-kind schema, the answer is usually "have the gameplay
  code set a flag at the right moment, use flag_set as the trigger." Adding
  a new trigger kind is a real engineering commitment (parsing + evaluator
  branch + tests); flags are free.

## What this is NOT

- **NOT different CONTENT per class.** Both Vagrants see the same
  lore, same insight unlocks. Only the aesthetic delivery differs.
- **NOT arbitrary tier-gating** to artificially restrict the player.
  Every tier-0 surface must be enough to play; every promotion must
  arrive at a moment that feels earned.
- **NOT a punishment for slow learners.** The multi-channel unlock
  ensures players who don't pursue any one specific behavior still
  unlock through play.
- **NOT a gimmick.** Consistent pillar applied across every
  informational surface in the game.
- **NOT fully locked.** Unlock channels, tier counts, per-class
  divergence depth are all genuinely open. This doc captures the
  shape; the law is still being written.

## Insight unlocks grow the cognitive stats

Insight node firings drive Mind-section stat growth. The mapping is
not direct ("each insight = +N to Mind pool") but mediated through
the three cognitive stats:

- **New observation firing** -> grows **Perception**.
- **New inference deduced** (any state, warranted or not) -> grows
  **Cognition**. Cognition's value drives the Mind resource pool's
  MAX, so deducing inferences grows the resource pool indirectly.
- **Warranted inference** (player's chosen reading matches the
  evidence they linked) -> grows **Intelligence** in addition to
  the Cognition gain.

Re-firing a node gives nothing (the insight system gates re-firings
via the `examined:<subject>` flag). Reconsidering an inference
(changing its reading) cascades through downstream child inferences
and removes the cognitive-stat gains those children contributed; the
player can rebuild and earn back the growth.

**Mind is the Vagrant's literal cognitive bandwidth** -- not MP, not
a magic-mana pool. Current depletes from cognitive-act spending;
current recovers at rest sites / vestigia. The MAX is the part that
grows from learning -- specifically from Cognition stat growth, per
the cognition system.

See [cognition-system.md](cognition-system.md) for the inference /
reading / warrant mechanics, the cascade rules, and the precise
growth formulas. The insight revelation system (this doc) addresses
language tier-promotion; cognition-system.md addresses the
cognitive-stat system that consumes the same insight-firing events.

This is the mechanical expression of the implied-lesson doctrine.
Paying attention to the world rewards capacity to act on the world.

## Open questions for refinement

1. **Tier count per node.** Two? Three? Per-node variable? Three is
   probably the maximum the player can track. Some nodes may only
   have two tiers (locked → unlocked), some three.
2. **Per-class divergence depth.** Steady-state-divergent (strong,
   high authoring cost) vs promotion-moment-only (weak, low cost)?
   Start weak, escalate if it reads thin.
3. **Unlock channel mix.** What proportion of nodes are story-
   driven vs accumulation-driven vs item-driven vs action-driven?
   Currently leaning: each significant node has 2-3 channels
   OR-gated.
4. **The unburdened HUD-thinning arc.** Strong pillar (HUD becomes
   harder to read over time on the unburdened path) OR fluffy
   aesthetic gimmick? Major design commitment if literal.
5. **Tier-0 visual treatment.** A 1-bit pixel UI has very few
   aesthetic axes. How much can a tier-0-vs-tier-1 difference
   actually convey in this register? Prototype with placeholder
   UI before committing.
6. **Action-unlock density.** How many micro-unlocks per hour
   feels good in the early game? 5? 15? Too many = noise, too few
   = static.
7. **Authoring cost.** Multiplying every information surface by
   2-3 tiers + per-class delivery is a lot of writing. Where do
   we accept lower fidelity to control scope?
8. **What lives in the tutorial / control-hints layer.** Pure
   operational hints ("press E to interact") probably stay
   exempt from tiering because the player needs them to physically
   operate the game. Where exactly is that cutoff?
9. **Node graph storage format.** JSON file per concept domain,
   one big file, or per-node files? Authoring ergonomics question;
   affects how easy it is to iterate.

## Related canon

- [[project_selva_epistemic_doctrine_2026_05_31]] — player-vs-
  designer knowledge split, no exposition
- [[project_contrapasso_law_vs_substance]] — vocabulary discipline
  the insight unlocks
- [[project_imprint_handle_required_for_sangue]] — class-asymmetric
  cosmology that motivates the per-class delivery pattern
- [[project_vagrant_speaks_player_chooses]] — Vagrant speaks; the
  player chooses what he says. The "no labels until earned" rule
  still holds because the Vagrant arrives without the cosmological
  vocabulary, not because he can't speak.
- [[project_selva_leveling_offerings]] — vestigia-mediated level-up
  system, plausible insight unlock site
- [[project_soul_larvae_cosmology]] — the first system where sangue
  appears mechanically; first test of tier-0 sangue UI
- [[feedback_dual_source_of_truth_is_the_bug]] — the shared-backend
  commitment is the structural defense against this bug class
