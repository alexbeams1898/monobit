# Architecture — real-world references

Every authored building/structure in Selva Oscura is based on a real
architectural reference. This doc catalogs which asset references
which building, so:

- Anyone rebuilding an asset knows what to look up.
- Style stays coherent across the world (all Italian, mostly medieval,
  mixture of ecclesiastical and fortified).
- We don't accidentally invent generic-fantasy shapes when a real
  Italian precedent exists.

**Register:** period-Italian medieval. Not French Gothic (Notre-Dame,
Amiens), not English (Salisbury), not fantasy-eclectic. Italian
Romanesque, Italian Gothic, Italian fortified.

---

## Selva surface

### Wood chapel — Madonna di Vitaleta

**Reference:** Cappella della Madonna di Vitaleta, near San Quirico
d'Orcia, Val d'Orcia, Tuscany. Freestanding neo-Gothic chapel on a
low rise surrounded by cypresses.

**Why it fits:** small, iconic, roadside-religious, unaccompanied by
larger surrounding architecture. Reads as the ONLY thing in a
landscape. That's exactly the Wood chapel's role — a single
manmade point in the Selva otherwise dominated by trees, terrain,
weather. Also reads unmistakably Italian without needing signage.

**Asset:** `assets/world/static_meshes/chapel_exterior.glb` +
`chapel_interior.glb`. Dimensions are approximately the real
building's (recorded pre-2026-06). If rebuilding, look up
Madonna di Vitaleta photos + floor plan and match.

---

## Limbo

### Noble Castle — Castel Sant'Angelo

**Reference:** Castel Sant'Angelo, Rome. Originally Hadrian's
mausoleum (139 AD), successively fortified and adapted into a papal
fortress-residence across the medieval period.

**Nested structure (outer to inner):**

- Water-filled moat around the pentagonal fortifications
- Pentagonal outer curtain wall (Paul IV, 1561-1565) with four
  pentagonal bastions (Sangallo the Elder)
- Medieval fortified courtyard walls layered over the Roman core
- Cylindrical drum (Hadrian): 64m diameter, 21m tall
- Interior spiral ramp (Hadrian): 120m long helical corridor, 6m
  tall × 3m wide, from ground up to the summit
- Central burial chamber at the ramp's summit: 8.5m square × 10.2m
  tall

**Vertical layout — six levels:** Livello 0 (ground / Roman entrance
/ spiral ramp start), Livello 1 (medieval level), Livello 2
(Renaissance papal apartments), Livello 3 (library), Livello 4
(rooftop terrace with angel statue), Livello Bastioni (outer
bastion fortifications).

**Real Castel Sant'Angelo dimensions (Hadrian's era + medieval
additions). Selva uses these at 1:1 scale — see below for scale
lock. Sources: Wikipedia, ArcheoRoma, Wellesley Piranesi archive,
Rome architectural guides.**

| Element | Dimension |
|---|---|
| Square base | 89m × 89m per side, 15m tall |
| Cylindrical drum | 64m diameter, 21m tall |
| Base + drum height | 36m |
| Original crowning structure (quadriga + tholos) | ~14m tall |
| Total original height (Hadrian era) | ~50m |
| Spiral ramp | ~120m long, ~2.7m wide, ~5.5m tall |
| Burial chamber (top of ramp) | 8.5m × 8.5m × 10.2m tall |
| Passetto di Borgo (medieval papal escape corridor) | ~800m long |
| Pentagonal outer wall + four bastions | (Paul IV, 1561-1565) |
| Water-filled moat | encircles pentagonal outer wall |

**Selva scale: 1:1 (faithful to source).**

Limbo's floor is at Y ≈ -43m, ceiling at Y ≈ -7m — 36m of vertical
headroom in the current terrain. The real Castle is ~50m tall (base
+ drum + crowning), which overshoots by ~14m. Selva accommodates
this by opening Limbo's rock-vault ceiling UPWARD over the Castle's
footprint — a natural stone dome that receives the Castle's mass.
This is thematically load-bearing: Dante says the Castle sits "at
the foot of a hill" ("a piè d'un nobile castello"), and the ceiling
opening IS the hill made manifest in Limbo's cavernous geography.
Selva's Castle is tall enough that Limbo's world had to make room
for it.

**Modest terrain rise under the Castle: 5-8m.** A gentle hill under
the base to reinforce the "approached from below" language. Player
walks up to reach the Castle's foot. The moat sits in a natural
depression at the hill's base.

**Adaptations for Selva:**

- The moat is a branch of Acheron — the river curves south from its
  main channel to loop around the Castle grounds and rejoin itself,
  a water-filled ditch of dissolving substance. Demons occupy the
  Castle because they're not dissolvable by Acheron (different
  substance category from damned souls); their underlings on the
  plain cannot easily cross the moat, so demon density is higher
  inside the walls than outside.
- The interior spiral ramp is the primary vertical traversal —
  boss's throne sits at the summit, in the old burial chamber /
  Renaissance papal apartments space.
- Passetto di Borgo (the historical papal escape corridor) is a
  candidate for the descent-to-Circle-II passage: reads as the
  demon's "way out" that the player reverses.
- Selva's failing state can be expressed as collapsed/breached
  rings. The canonical Seven Walls of Limbo (per Dante) map onto the
  five nested rings of the real Castel Sant'Angelo with two rings
  documented as fallen or missing — visible gaps in the layout
  reinforce "civilization tried and is failing."

**Constraints from Dante** (still non-negotiable):

- Seven concentric walls (mapped: 5 present + 2 fallen)
- Seven gates, one per wall
- Rivulet moat (mapped: Acheron branch)
- Meadow of fresh green grass inside (mapped: interior courtyard,
  now polluted/petrified in the failing state)
- Interior on high ground ("luminoso e alto")
- Single fire — the only light in all of Limbo (mapped: the fire's
  location within the Castle is TBD; historically the Roman burial
  chamber or a courtyard hearth are strong candidates)
- Approached from below at the foot of a hill

**Selva-side design intent:**

- Big — Lothric Castle-tier in scale, many rooms and passages
- Italian, medieval-fortified, scholarly/ecclesiastical inflection
- In its failed state: infested by the fallen Canto IV pagans, led
  by corrupted-Aristotle (per [[project_limbo_fallen_pagans_bosses]]).
  Corruption spreads top-down through the intellectual hierarchy;
  underlings leak into Limbo's plain.
- Boss fight inside; passage to Circle II beyond it.

**Traversal shape (noted for later, not solved yet):**

- Player enters from an unusual angle (not the front gate) — Souls
  progression convention. Something forces a side/rear approach.
- Interior is twisting, non-linear paths through rings and levels —
  Souls-typical routing that circles back, uses shortcuts, opens
  gates behind you.
- Boss arena on the GROUND FLOOR, not at the summit. The traversal
  is a descent through the Castle, not an ascent.
- Descent staircase to Circle II is beyond the boss arena — dropping
  through the Castle's foundation into whatever comes next.

This inverts the historical Castel Sant'Angelo's spiral ramp
(historically an ASCENDING corridor to the burial chamber at the
top). In Selva, the ramp still exists but the significant vertical
motion is DOWNWARD through the Castle into the earth below Limbo.

**Constraints from Dante** (canonical, non-negotiable):

- Seven concentric walls, each "lofty" (`sette alte mura`)
- Seven gates, one per wall (`sette porte`)
- A rivulet moat around the outside (`bel fiumicello`)
- A meadow of fresh green grass inside (`prato di fresca verdura`)
- The interior meadow is on high ground ("luminoso e alto")
- A single fire — the only light in all of Limbo, casting a
  hemisphere of light around it
- Approached from below at the foot of a hill (`a piè d'un nobile
  castello`)

**Selva-side design intent:**

- Big — Lothric Castle-tier in scale (many rooms, paths, passages,
  vertical stack)
- Italian, medieval-fortified with scholarly/ecclesiastical
  inflection (housed philosophers, poets, warriors)
- In its failed (Selva) state: infested by a demon who has taken
  the throne; underlings leak into Limbo's plain
- Boss fight inside; passage to Circle II beyond it

---

## Doctrine

- Every new structure gets an entry here at authoring time. If you
  can't name a real reference, it's not ready to build.
- Composite references (part of building A + massing of building B)
  are legitimate — just name both.
- "Something Italian and old" is not sufficient. Name the specific
  building.
