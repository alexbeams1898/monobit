# Bestiary — *figura umana*

**Status:** drafting / WIP

## The rule

> Every damned soul in Hell wears the human form, however deformed by
> its sin.

The bestiary of Selva Oscura is built on one shared human skeleton.
Damned souls — the overwhelming majority of enemies the player fights
— are variations of that skeleton, deformed by **contrapasso** (the
sin reshaping the body). Three canonical exemption classes get their
own skeletons:

1. **The three legends of the Wood** — **Lonza**, **Leone**, **Lupa**
   — the three named pre-failure apex-fauna of the *selva oscura*.
   At game-start, only **Lupa** survives (the others died during
   Hell's stagnation, per [[selva-wood-lore-locked-2026-05-31]]).
   Encountered before descent.
2. **Selva-organisms** — the new ecology that emerges as the Wood
   heals (per [creatures.md](creatures.md)). Sangue-infused
   descendant forms of the three legends and their combinations.
   English-coined fantasy names. Distinct from the legends and
   distinct from any Hell-creature.
3. **Classical guardians** — Cerberus, Minotaur, Geryon, Lucifer
   himself, guarding circles inside Hell. Drawn directly from Dante.

All three exemptions are detailed below.

We call the rule the *figura umana* — "the human figure" — internally.

## Why

1. **It is Dantean.** Souls in the *Inferno* retain recognizable human
   form (Dante speaks to most by name) and are deformed by their sins
   per *contrapasso* (named explicitly in Canto XXVIII, line 142:
   *"Così s'osserva in me lo contrappasso"*). Bodies twisting under
   the weight of sin is in the source text.
2. **It carries moral weight.** Every enemy was once a person.
   Every kill is the recognition of a sin. The bestiary becomes a
   moral catalogue, not a monster list. The player learns to read
   sins in silhouettes.
3. **It is genuinely unsettling.** The body reads as human, but a
   feature is wrong — too long, too short, a snout where a face
   should be, hooves where hands were. The brain refuses to file
   it as "animal" or "human." This is the *Silent Hill 2 /
   Bloodborne / Goya* register, not the *D&D monster manual*
   register.
4. **It is enormously cheaper to ship.** One rig. One animation
   pipeline. Every clip authored works for every enemy. Variety
   comes from bone scaling, mesh swaps, surface treatment, posture,
   and movement quirks — all cheap relative to authoring distinct
   creatures from scratch.

## What is and isn't covered

**Damned souls (rule applies):** the bulk of the bestiary. Gluttons,
lustful, wrathful, slothful, avaricious, heretics, treacherous,
schismatics, thieves, and so on — every sinner Dante meets is some
distorted human, and so is every enemy in this game that was once a
mortal.

**The three legends of the Wood (rule does not apply):** the pre-failure
*selva oscura* hosted three named apex-fauna on the colle's south slope:
**Lonza**, **Leone**, and **Lupa**. **Selva-canon: Lonza and Leone died
during Hell's stagnation, before the Vagrant arrived. Only Lupa
survives at game-start** as the lone surviving legend and the Beat 2
opening boss (see [story.md](story.md) Beat 2 and
[[selva-wood-lore-locked-2026-05-31]]).

The three are **proper names of specific individual creatures**, used
like Cerberus or Margit — capitalized, no article ("Lupa stalks the
slope," NOT "the lupa stalks the slope"). There is exactly one Lonza,
one Leone, one Lupa. They are individuals, not species. Italian here
serves the **Italian-as-legend register doctrine** (see
[[selva-epistemic-doctrine-2026-05-31]]): Italian is reserved for
legends and Named Things; descendant species that emerge from the
healing Wood get English-coined fantasy names.

They are **alive on the slope** (real biological animals + allegorical
forces given physical form); not post-mortem Hell-creatures, not
derived from any damned soul, not Selva-organisms (those are the
leak-evolved new ecology). Cosmologically distinct from classical
guardians: classical guardians are *Hell-made monsters guarding
circles inside Hell*; the legends are *living pre-failure inhabitants
of the threshold-Wood, encountered before descent*. Different
exemption, same outcome (non-humanoid skeletons).

Canonical Canto I beasts (source-text references):

- **Lonza** (*Inferno* I.31–33) — leopard / lynx, spotted, light-footed
- **Leone** (*Inferno* I.45–48) — lion, head high, raging
- **Lupa** (*Inferno* I.49–60) — she-wolf, gaunt, insatiable — the worst

Allegorical readings: incontinence / violence / fraud (per the
traditional gloss). Whether the game leans on the allegory is open
(WIP). The Lupa encounter ships as a tragic-register boss; the
absence of Lonza and Leone is part of the encounter's emotional
weight (see [[selva-wood-lore-locked-2026-05-31]]
*Tonal register* for the Lupa fight).

After Lupa falls, the slope stays empty in subsequent cycles. Lupa
is `permanent_on_death: true`, fought once per save, same pattern as
keepers. The empty slope itself becomes the monument to what was.

Engine architecture for non-humanoid actors lives in
[animals_and_multi_skeleton.md](animals_and_multi_skeleton.md).
Asset status (2026-05-31): **Lupa shipped** as Quaternius CC0 wolf
placeholder. **Lonza and Leone are not pending assets** — they are
dead in canon and have no in-game representation as legends. Their
silhouettes survive only via the descendant ecology (per
[creatures.md](creatures.md)) — sangue-infused new forms that echo
the legends' shapes.

**Selva-organisms (rule does not apply):** the leak-evolved new ecology
that emerges as the Wood heals (see [creatures.md](creatures.md)).
**More-Hellish descendant forms** of the three legends and their
combinations, sangue-infused. Recognizable silhouettes (Lupa-shape,
Leone-shape, Lonza-shape) but with English-coined fantasy names —
they are kinds-of-things, not Named Things. They are NOT damned souls,
NOT classical guardians, NOT legends. They are *new life that
emerged because the Wood healed and sangue tagged along with the
healing.* See [creatures.md](creatures.md) for the full framework.

**Classical guardians (rule does not apply):** drawn directly from
canonical Dante. These are infernal *creatures*, not damned humans.
Used sparingly — minibosses, bosses, setpiece encounters. The
exceptions teach the rule: when the player sees a non-human shape,
it *means* something.

Canonical exceptions Dante writes:

- **Cerberus** (*Inferno* VI) — three-headed dog, guards the gluttons
- **Minotaur** (*Inferno* XII) — bull-headed, guards the violent
- **Centaurs** (*Inferno* XII) — Chiron, Nessus, Pholus; patrol the river of blood
- **Harpies** (*Inferno* XIII) — bird-bodied with human faces; in the forest of suicides
- **Geryon** (*Inferno* XVII) — human face, lion paws, serpent tail, scorpion sting
- **The Malebranche** (*Inferno* XXI–XXIII) — devils with wings/claws/hooks
- **Lucifer** (*Inferno* XXXIV) — three-faced giant with bat wings, at the pit's center

Not every canonical exception will ship — some are placeholders for
future scope. But any non-human enemy in this game **must** trace back
to a Dantean source. We do not invent new monsters.

## Soul-form vs animal-form (combat doctrine in the Wood)

> **LOCKED 2026-06-01.** Foundational rule that explains why the
> Lupa fight is permitted in the Wood AND why the Wood is otherwise a
> no-combat zone. Memory: [[vagrant-speaks-player-chooses]] +
> [[selva-core-framing]].

Actors in this game fall into two cosmological **forms** for combat
purposes:

- **Soul-form** — unjudged souls (the Vagrant, the Guide, any future
  Wood NPC who is a soul). Inside Hell, damned souls are also
  soul-form (they wear the human figura umana even when deformed).
- **Animal-form** — real biological animals + allegorical forces given
  physical form. The three legends (Lonza, Leone, Lupa) are animal-
  form, as are Selva-organisms (the leak-evolved descendants).
  Classical guardians (Cerberus, Minotaur, etc.) are animal-form-
  adjacent — they are infernal creatures, not damned souls, and obey
  the same combat-permission rules as animal-form actors.

### The Wood's combat protocol

**The selva oscura permits combat across forms; it forbids combat
within forms.** Stated explicitly:

- **Soul-on-soul: FORBIDDEN.** The Vagrant cannot damage the Guide.
  The Guide cannot damage the Vagrant. Future Wood NPCs (other
  unjudged souls who eventually arrive) cannot damage the Vagrant
  and cannot be damaged by him. This is the *Roundtable Hold*
  equivalent — a structural peace that the Wood enforces as a
  cosmological property of the place, not a per-NPC flag.
- **Soul-on-animal: PERMITTED.** The Vagrant CAN damage Lupa. Lupa
  CAN damage the Vagrant. The Guide could damage Lupa (he chose not
  to — see *story.md Beat 3* on the poisoned-carcass approach). Any
  future animal-form encounter in the Wood (a leak-evolved
  descendant species, etc.) is freely combatable by soul-form actors.
- **Animal-on-soul: PERMITTED** (the reverse of the above). Combat is
  symmetric across forms.
- **Animal-on-animal:** N/A within the Wood at game-start (only Lupa
  remains; Lonza and Leone died of starvation pre-game). The
  Selva-organism ecology that emerges later may produce animal-on-
  animal combat as part of world simulation; the protocol does not
  forbid it.

### Why this matters mechanically

The Lupa fight is **canonically permitted** by the form distinction.
She is animal-form; the Vagrant is soul-form; the rule allows the
combat. The fight is not a violation of the Wood's protocol — it is
the one form-pairing the protocol explicitly permits.

**After Lupa falls**, the Wood contains only soul-form actors (the
Guide, the Vagrant, eventually other arriving unjudged souls). With
no animal-form left, the cross-form combat-permission is vacuously
inert. The Wood becomes a true no-combat zone as a *consequence* of
Lupa's death, not as a separate magical effect that turns on. The
peace is permanent because the conditions for combat are permanently
removed.

If the Wood eventually re-populates with animal-form creatures (the
leak-evolved Selva-organism ecology per [creatures.md](creatures.md)),
combat returns automatically — the protocol works on form, not on a
timer or quest state.

### Why this matters narratively

- The opening Lupa encounter is the *only* combat the Vagrant can
  ever experience in this part of the Wood. Every subsequent visit
  is a soul-only zone. The Wood's no-combat property reads to the
  player as "this is a refuge"; the truth is "this is a refuge for
  souls because the apex animal is dead."
- The Guide could not have fought Lupa directly. A soul-form actor
  against a starving but functional apex predator is hopeless. The
  Guide's choice of poison + patience is *his form's only available
  approach*. He used craft because force was not on the table.
- Future Wood NPC arrivals (other unjudged souls finding their way
  to the threshold) cannot threaten the Vagrant and cannot be
  threatened by him. Wood-NPC dialogue can hint at past Wood-violence
  among animal-form creatures (Lonza, Leone, the legends' wider
  ecology) without putting any future NPC at combat risk.

### Engine implementation status

**Doc-locked, not yet code-locked.** Engine-side `Form` distinction
(an enum or actor flag) plus a Wood-protected-region property is the
clean shape. Today Lupa is the only animal-form actor and the only
potential damage source in the Wood, so the canonical rule is
*currently* equivalent to the existing faction-based `factionsHostile`
check. The form-rule becomes load-bearing the moment we ship a second
Wood-resident NPC or Selva-organism. Track in
[[engine-form-vs-faction-todo]] (memory) when the implementation
ticket lands.

## How contrapasso reads on the human skeleton

For a damned soul, the sin determines the deformation. Mechanical
levers (all cheap relative to a new rig):

1. **Bone scales** — lengthen the spine, shorten the legs, swell the
   gut, drop the head forward, broaden or narrow the shoulders.
   The silhouette of a glutton-pig is short legs + huge torso +
   forward-hung head + no neck.
2. **Mesh attachments at the head / hands / feet** — a snout where
   the head was, hooves where the hands were, talons where the feet
   were. Same joints; the rig doesn't care.
3. **Surface treatment** — bloated, sweating, hairless, scaled,
   charred, dripping, encrusted, smoking. The flesh tells the sin.
4. **Permanent additive posture pose** — hunched, twisted-backward,
   wing-armed, knuckle-walking. Layered onto the shared idle without
   authoring new clips.
5. **Movement quirks** — drag a leg, scuttle, lurch, charge, skitter.
   Per-clip retiming or speed scaling, not new animation.

## Working sketch of the contrapasso table

This will fill in as the bestiary grows. Placeholders are starting
points, not commitments. Slot-by-sin — many sins map to multiple
enemy variants over the project's life.

| Sin (Dantean circle) | Animal / form reference | Visual key |
|---|---|---|
| Gluttony | Swine | Short legs, swollen torso, snout, dripping |
| Lust | Bird-of-prey, blown about | Elongated arms, taloned hands, hollow-cheeked |
| Wrath | Mastiff / pit-fighter | Hyper-musculature, snarling jaw, charging gait |
| Sloth | Drowned / bloated | Limp, waterlogged, mired; no animal — *less than human* |
| Avarice / Hoarding | Spider-handed | Long grasping fingers, perpetual reach, hunched |
| Heresy | Faceless | Human skeleton, head replaced by a featureless mass / flame |
| Treachery | Reptile | Serpentine spine elongation, no neck, scaled surface |
| Schism | Cleaved / split | Visible wounds that never close (per Canto XXVIII) |
| Theft | Shifting / serpent-fused | Hands fused to snakes; intermittent form (per Canto XXV) |

The discoverability of the rule is part of the design — the player
should figure out *why* the pig-thing looks like a pig over time, not
be told.

## What the player ever learns explicitly

Probably nothing direct. Weapon descriptions, item lore entries, NPC
dialogue, environmental storytelling all hint. The Guide may comment
elliptically. The Compendium / bestiary entry (if one ships) names
the sin without spelling out the rule.

The player figures it out. That is the point.

## Implementation status

- **Current (this branch):** Enemies use the *default* form — same
  rig and proportions as the PC, no deformation pipeline. This is
  a placeholder. The shared-skeleton premise still holds; the
  deformation layer hasn't been built yet.
- **Next:** First enemy entity. Static, idle-loop, no AI, no damage.
  Serves as the foundation that the deformation pipeline will hang on.
- **Future, in roughly this order:**
  1. Bone scaling at load time (per-enemy proportions)
  2. Head / hand / foot mesh swap (per-enemy attachments)
  3. Surface treatment (per-enemy material variants)
  4. Additive posture pose layer
  5. Per-enemy movement quirks (retiming, speed scaling, drag-leg)
  6. First canonical guardian (likely Cerberus, since gluttons are
     the first circle past the vestibule and the swine-glutton is
     the first contrapasso reading we'll author)

## Open questions

- Do PC and NPCs of the *living* world (the Guide, etc.) share the
  same rig? Probably yes — see [pc-vs-npc.md](pc-vs-npc.md). The rule
  here is about the *damned*, not all entities in the game.
- How does the player's own body fit the rule? The PC is a damned
  soul too. Do we hint at the player's contrapasso visually? (TBD —
  could be a major late-game reveal, could be irrelevant.)
- Are there damned souls who *aren't* deformed — souls so neutral
  or self-aware that contrapasso hasn't reshaped them? (Limbo
  inhabitants — *Inferno* IV — aren't tormented and aren't
  described as deformed. Worth thinking about.)
- The bosses in the canonical-exceptions list are largely
  placeholders. Which ship, in what order, is a story / scope
  question for later.
