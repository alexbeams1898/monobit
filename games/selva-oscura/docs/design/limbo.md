# Limbo (Circle I) — design (WIP)

**All sections WIP.** Nothing locked. Decisions get firmed up through
iteration in-game; this doc captures the current direction + the
canonical source material + the alternative paths considered.

---

## Canonical reference (Inferno III + IV)

### Spatial layout per Dante

```
   ACHERON near shore         ← unbaptized/uncommitted souls wait for Charon
        (Inferno III ends)
        |
   ACHERON crossing           ← Charon's ferry; Dante crosses in unconsciousness
        |
   ACHERON far shore = Limbo's edge
        (Inferno IV starts)
        |
   wide plain of darkness     ← the famous "d'i sospiri" (air of sighs)
   wandering virtuous-pagan      no torment, no joy, infinite sighs
   souls                         featureless, dim
        |
   NOBLE CASTLE               ← mid-Limbo, the ONLY light source
   ("nobile castello")           — self-made fire conquers a "hemisphere
                                   of darkness"
        |
   plain continues
        |
   DESCENT to Lust (II)       ← cliff at the far end of Limbo
```

### The Noble Castle's canonical attributes

| Element | Canonical form |
| --- | --- |
| Light source | Self-made fire — the only light in all of Limbo |
| Walls | Seven concentric (representing 7 liberal arts / 7 virtues) |
| Gates | Seven, one per wall |
| Moat | Stream that parts for the worthy ("come terra dura") |
| Meadow inside | "Fresca verdura" — fresh green grass |
| Inhabitants | Poets (Homer, Horace, Ovid, Lucan, Virgil), philosophers (Aristotle, Plato, Socrates), scientists, Saladin, Avicenna, Averroes |
| Mood | Soft measured speech, dignified company, the only Hell-place without active torment |
| Approach | "A piè d'un nobile castello" — at the foot of a hill, approached from below |

---

## Selva-specific divergence: Limbo non-functional

Selva's Inferno is non-functional. The infrastructure exists but the
systems don't work. For Limbo specifically:

- **The Castle's fire is OUT.** The "hemisphere of darkness" has
  reclaimed the keep. This is the load-bearing inversion — everything
  else follows.
- **The Castle's inhabitants — Dante's honored pagan philosophers,
  poets, and warriors — have fallen.** They are not gone. They are
  not replaced. They ARE the demons now. Millennia of Lucifer's slow
  propagation (per [[project_lucifer_act_root_cosmology]]) reaching
  Limbo had no keeper to transform, no contrapasso to invert, and no
  outside invader to make demons of — so it worked on the substance
  it found: the virtuous pagans. Corruption spread TOP-DOWN through
  the intellectual hierarchy Dante describes. Aristotle (the
  presiding intellect) fell first — everyone honored him, so he
  concentrated the most corruption. Those closest to him fell next.
  Then the outer rings. The Castle's original social structure
  remains intact; every node in it is corrupted.
- **Charon is absent.** Souls supposed to be ferried across Acheron
  cannot cross. They pile up on the near shore as larvae — soul-
  landfill choking the bank.
- **The inhabitants** — gone, hidden, scattered, faded? TBD. What
  remains is the architecture of their attempt to preserve dignity in
  the dark.

### Castle final-state — three candidates (TBD)

- **Ruin**: collapsed walls, dead fire pit. Player explores the
  evidence of who used to live there. Restoring it = part of the
  gameplay loop.
- **Dark but intact**: fully built, no light. Inhabitants displaced
  or hidden. Player relights the central fire as a story beat = first
  major light in Limbo.
- **Missing**: meadow's there but nothing built. Foundations or a
  single broken arch where it stood. Most extreme reading.

### Castle inversion table (canonical → Selva failed-state)

| Canonical | Inverse |
| --- | --- |
| Self-made fire conquers darkness | Fire extinguished; cold ash in the hearth |
| Seven concentric walls (order) | Walls breached, collapsed, gaps; maybe a single arch still standing |
| Seven gates (controlled access) | Gates broken, hanging, sealed shut from outside |
| Moat parts for the worthy | Moat stagnant or overflowing; no longer recognizes worth |
| Fresh green meadow | Grass petrified, ash-grey — same material as the Limbo plain |
| Pagans in dignified company | Empty, skeletal traces, or souls dispersed onto the plain |
| Soft measured speech | Silence; or the d'i sospiri creeping inside |
| Built on a hill, approached from below | Hill leveled or sunk; on flat ground or in a depression |
| Fire is the only light in Limbo | Without it, total darkness — or pinpoints of dying glow from improvised sources (see "Light direction" below) |
| Clear path from Acheron to Castle | Path broken / obscured / branches into wrong paths |
| The dignity in dignified torment-free space | Torment arrives even here, OR the dignity itself becomes the torment |

---

## Light direction (combined A+B, currently the direction)

**Premise**: Limbo is dark — the central fire is out. But the
inhabitants tried to preserve light at smaller scales before fading.

### What player sees at game-start

- Limbo mostly dark with sparse failing lights scattered across the
  plain — campfires, oil lamps, braziers, glowing fungus bowls.
- Most are out (cold ash). A few flicker dimly. None match what the
  Castle's fire used to be.
- Reads as: civilization tried and is failing. The player is the
  latest in a line of someones who came here and tried to light it.

### Player's first crafting

- First crafting unlock: a **torch**. Materials harvested from a
  dying or failed light source (oil-soaked rag from a dead lamp, dry
  tinder from an abandoned fire).
- Lore framing: hell offers nothing but what it naturally has;
  player has to MAKE light from limbo's materials, not pull it from
  inventory. The torch is inherited project — continuing what the
  inhabitants started.

### Light source vignettes (TBD count + placement)

- 5–10 authored static-mesh + tiny narrative tag across Limbo.
- Mix: some campfires (ground), some braziers (waist height), some
  wall-sconce oil lamps, some glowing-fungus bowls (organic).
- Some flicker. Most are out. Maybe 1–2 still burn brightly enough
  to give player a "rest point" of light to navigate by.
- Each becomes a potential resource node when the crafting system
  lands — harvestable materials for player's first recipes.

### Castle as the scaled-up version

- The Castle's central fire being out IS the big version of every
  small failed light. Same story at scale.
- If the player restores the Castle's fire (lore-major beat,
  probably gated by completing major content), that's the moment
  Limbo's "hemisphere of darkness" gets pushed back. The pagan
  inhabitants might return / wake / re-emerge.

---

## Areas (canonical → our current geometry)

| Canonical area | Selva current state |
| --- | --- |
| Acheron near shore (Charon's ferry landing) | The shore on the descent-stair side of the Acheron trench. Larvae piles authored here (TBD). |
| Acheron crossing | Player walks down the trench's sloped banks, across the riverbed (when Charon's gone — no ferry needed), up the far bank. |
| Limbo wandering plain | The far shore of Acheron in Limbo (the wider extent past the trench). Where most exploration happens. Where most failed light sources are. |
| Noble Castle (mid-Limbo) | TBD location on the far shore. Castle layout, scale, state per above. |
| Plain continues past Castle | Beyond the Castle, toward... |
| Descent to Lust | Cliff/waterfall/sinkhole at the far edge of Limbo. Per [[project_acheron_river_lore]] in memory: this is where the Acheron river continues downstream into Circle II (becoming Styx eventually). |

---

## Open questions (TBD)

- What does the Noble Castle look like architecturally? (Italian
  fortified palazzo? Roman temple complex? Something other?)
- Where exactly does it sit on the far shore — at the back edge of
  Limbo (against the cavern wall)? Mid-plain (visible from the
  Acheron banks)? Off-center?
- How big should it be relative to the player and Limbo's extent?
- How does the player CROSS Acheron in v1 — wade through the
  riverbed (no ferry, water is shallow), authored bridge, fade-to-
  sleep cinematic (canonical: Dante crosses unconscious)?
- The seven concentric walls — author all seven (faithful)?
  Author one symbolic outer arch (legible)? Skip the walls entirely
  and have just the central fire pit + grounds?
- What sound does Limbo make? The d'i sospiri is canonical; in our
  failed state it might be amplified, or absent (souls gone),
  or distorted.
- Are the wandering souls the player encounters in Limbo player-
  visible NPCs, or just ambient sound + dim silhouettes in the
  distance?

---

## Boss + miniboss roster (Castle, LOCKED-DIRECTION 2026-07-07)

Every boss in the Castle is one of Dante's Canto IV honored pagans,
fallen. The Vagrant fights his way through the corrupted classical
canon in reverse-honor order: outermost rings first, alpha last.

**Alpha (final boss of Limbo Castle): Aristotle.**
*"Il maestro di color che sanno"* — Master of those who know. The
presiding intellect of Dante's pagan hierarchy, concentrated the most
corruption, fell first. Everything below flows from his fall. Fought
at the summit of the Castle (Livello 2/3 mapping to Castel
Sant'Angelo's papal apartments / burial chamber). His imprint on the
Castle IS the Castle's failed state.

**Major minibosses** (miniboss = named Canto IV figure, boss-tier
fight):

- **Homer** — presiding figure of the poets. Carries a sword
  ("falchion"). Poet-warrior hybrid. Fought as a miniboss guarding
  his ring of poets (Horace, Ovid, Lucan as lesser fights or
  environmental). Verse patterns in his corrupted speech.
- **Caesar** — "in armour with gerfalcon eyes." Presiding figure of
  the fallen warriors. Imperial, martial. Commands the outer walls /
  courtyards of the Castle.

**Solitary optional miniboss:**

- **Saladin** — Dante placed him "alone, apart" from every other
  group. In Selva he fell differently, or holds a specific isolated
  space in the Castle (a chamber, a tower). Optional duel-style
  encounter. Muslim sultan, warrior-king; his corruption reflects a
  different tradition than the Greco-Roman rest.

**Specialty minibosses (theme-bound corruptions):**

- **Galen** — historical physician. Fallen: corrupted physician-
  demon. Ties directly to the pus/wound imagery of the Castle's
  infection. Environmental role: guardian of some medical-tainted
  chamber or vault.
- **Hippocrates** — the other historical physician. Companion or
  rival to Galen in the corrupted-medicine space.
- **Averroes** — "who the great Comment made" (Dante's phrase).
  Historical commentator on Aristotle. Fallen: demon of twisted
  interpretation, weaponizing philosophical text. His corruption
  parallels his life's work — he still commentates on Aristotle,
  but the commentary is now a weapon.
- **Socrates and Plato** — stood nearest Aristotle in Dante. Second-
  and third-tier philosophers. Fought as the ring immediately before
  Aristotle, at the top of the spiral ramp.

**Rank-and-file corrupted (non-boss enemies / underlings):**

Outer philosophers: Democritus, Diogenes, Anaxagoras, Thales, Zeno,
Empedocles, Heraclitus. Fought as regular corrupted-philosopher
enemies in the outer rings.

Roman republicans (Brutus, Lucretia, Julia, Marcia, Cornelia) and
Trojan/Roman figures (Hector, Aeneas, Camilla, Penthesilea, Latinus,
Lavinia, Electra) — regular corrupted-warrior enemies.

**Design implications:**

- Every fight in the Castle is a mercy killing of a celebrated
  ancestor. The Vagrant is putting down Aristotle, Homer, Caesar.
  Melancholy of high tragedy runs beneath every combat.
- The bestiary/study system gets infinite material — each fallen
  figure has a real historical biography the player can piece
  together. Combat context deepens with study.
- Loot in the Castle's library (Livello 3 of the historical Castel
  Sant'Angelo maps to Selva's library level) includes the writings
  of the figures the player is about to fight or has just felled.
  Reading them AFTER the kill hits harder than reading them before.
- Corruption spread top-down through millennia. The fights are
  encountered in REVERSE — outer rings (warriors) first, poets
  second, philosophers third, Aristotle last. The player is
  literally traversing the hierarchy from bottom to top, arriving
  at the alpha last.

**Cosmological weight:**

Aristotle — and every fallen figure in the Castle — is technically
an unjudged/unbaptized soul. That's WHY they are in Limbo in the
first place per Dante. So the alpha of the Castle is in the same
substance-category as the Vagrant: both unjudged, both unprocessed
by Hell's judgment protocol.

The Vagrant needs an imprint (via the Guide's Signing) to be able
to act on Aristotle — unimprinted unjudged versus unimprinted
unjudged = no differential grip. The Signing gives the Vagrant the
specific shape he needs to counter what Aristotle has evolved into
over millennia. Beatrice knew. This is why the Signing is the
prerequisite for the descent.

---

## Cross-references

- Canonical text: `docs/reference/inferno_longfellow.txt` (Cantos
  III + IV).
- Selva inferno cosmology + Charon-absent lore: memory file
  `project_acheron_river_lore`.
- Inferno-vertical-stack design (each circle smaller + deeper): memory
  file `project_inferno_vertical_stack`.
- Selva non-functional doctrine (game-wide): TBD — currently scattered
  across `setting.md` + `creatures.md`. Consolidate later.
