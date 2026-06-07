# Insight revelation system — design direction

Captured 2026-06-05. **WIP, not locked.** Refine as Selva develops.

This doc captures the shape of an idea, not the final law. Some
details (node count, unlock conditions, surface-specific behavior)
are genuinely open. The PILLAR is what we're trying to defend; the
mechanics underneath are negotiable.

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

- The Vagrant is mute and hollow. They don't have vocabulary for
  what they're seeing. UI / dialog / items that LABEL things they
  can't yet name is lying. Progressive naming is honest.
- Per [[project_selva_epistemic_doctrine_2026_05_31]] — Selva does
  not tell the player they are in Hell. Selva also should not tell
  the player what their resources are, what their stats mean, or
  what they are accumulating. The world is for them to read.
- Per [[project_contrapasso_law_vs_substance]] — the cosmology is
  invisible to its participants. The Vagrant gains the framework
  for understanding only through play. Every revelation channel
  mirrors that.
- Per [[project_vagrant_silent_vessel_doctrine]] — the Vagrant
  doesn't speak; the world reveals itself through what the player
  DOES, not what NPCs explain.

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
- **Vessel commitments** — first Crucible / Censer use, first
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
Vagrant is mute and hollow. The Guide doesn't know what he is.
NPCs are damned souls who know they're suffering but not why.
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
- [[project_vagrant_silent_vessel_doctrine]] — Vagrant is mute,
  reinforces the "no labels until earned" rule
- [[project_selva_leveling_offerings]] — vestigia-mediated level-up
  system, plausible insight unlock site
- [[project_soul_larvae_cosmology]] — the first system where sangue
  appears mechanically; first test of tier-0 sangue UI
- [[feedback_dual_source_of_truth_is_the_bug]] — the shared-backend
  commitment is the structural defense against this bug class
