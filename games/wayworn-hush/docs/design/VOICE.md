# Wayworn Hush — Voice & the Confidence Arc

> **Owns:** who the pilgrim is, how he talks to himself, and how that changes as
> he grows. Governs all thought-writing. The register doctrine every observation
> is authored against.
>
> Surface concept lives in [DESIGN.md](DESIGN.md) / [ORIGINAL_NOTES.md](ORIGINAL_NOTES.md);
> the submerged theme in [THEME.md](THEME.md); the mechanic in
> [PSYCHE.md](PSYCHE.md). This doc is the voice that
> carries all three.

## tldr

Two layers, always: the **observation** is objective and sincere — what is
actually there. The **thought** is him — first person, a body, a mood, and a
sense of humor that runs crude. The comedy is the mundane intruding on the
poetic, never a punchline.

The arc is **confidence**, and the load-bearing rule is counterintuitive: he is
**less funny at the start**. Early humor is deflection — he is uncomfortable, so
he dismisses rather than looks. Late humor is ease — he is comfortable enough to
engage, so it gets specific, observant, weirder, better. The crudeness never
goes away. It gets *better*.

---

## 1. The two layers **[LOCKED]**

| Layer | Voice | Function |
|---|---|---|
| **Observation** | Objective, plain, sincere. No jokes. No "I." | What is actually there. |
| **Thought** | First person. A body, a mood, a personality. | Him. |

> *"A green bush, damp with rainwater."*
> *"I really need to take a piss."*

The observation earns the beauty; the thought undercuts it with a person. Both
are true at once, which is the whole register. A thought that is merely a
description with feelings bolted on is a failed thought — the thought is where he
*is*, not more of what the world is.

## 2. The humor **[LOCKED]**

- **Mundane intruding on the poetic.** Not wordplay, not gags, not quips. The
  landscape is sincere; the body has needs; both are real.
- **Crude is allowed and wanted.** Bodily, unglamorous, a little gross. A guy
  alone for weeks stops performing dignity.
- **Never a punchline.** No setup/payoff structure. The comedy is tonal
  collision, and it should read as him thinking, not as the game being funny at
  him.
- **It is affectionate, never bitter.** He is not sneering at the world or at
  himself. Even the deflections come from discomfort, not contempt.

A guy on a solemn pilgrimage who mostly needs to pee is a *person*, not a symbol.
The goofiness is what earns the quiet later — grief that hasn't been goofy first
reads as pretension.

## 3. The confidence arc **[LOCKED — the spine]**

**Growth does not make him wiser. It makes him more comfortable.**

The tiered-thought mechanic already deepens text as faculties rise
([PSYCHE.md](PSYCHE.md)). Tiers are **not** more
information — a higher tier is not a better fact. Tiers are **register**:

| | Low | High |
|---|---|---|
| **Stance** | Uncomfortable, dismissive, flinching from his own attention | At ease, engaged, committed to the bit |
| **Humor** | Defensive — the joke is a way of *not looking* | Loose — the joke comes from actually seeing |
| **Certainty** | Hedges, shrugs, "whatever," "I never learned these" | Names it plainly, trusts what he sees |
| **Length** | Clipped. He wants out of the moment. | Willing to stay a while. |

The same bush:

- **Low:** *"Bush. Wet. Whatever. Need to piss."*
  Not engaging — dismissing. Barely a joke. Uncomfortable being alone with his
  own attention.
- **High:** *"Perfect spot, honestly. Rain already did half the work. Sorry to
  the bush."*
  In on it. Noticed the rain, made it a bit, apologized to a plant. A guy
  enjoying his own company.

What changed is not wit. It is that **he is comfortable being alone with
himself** — which is the entire theme, delivered through a piss joke, and never
once stated.

**Corollary — solitude reads differently at each end.** Early, alone is
uncomfortable, so he deflects. Late, alone is fine, so he plays. Peace is when
your own company is good company.

## 3b. The mundane carries the character **[LOCKED — pillar]**

**No mundane system is exempt from personality.** The chores of being a game —
saving, menus, inventory, item descriptions, failure, the pause screen — are
surface for the voice, not plumbing to get through. EarthBound's most-remembered
moments are pure UI: calling your dad to save, an ATM, a phone call home. The
boring parts are where the character lives.

This game has a lot of mundane — crafting, foraging, camping, the notebook, the
Self tab. Each is either a spreadsheet or it is him. It should be him.

**Punishment is the same rule applied to cost.** The game has no fail-state
([GAME-SYSTEMS.md](GAME-SYSTEMS.md) §4: "you can't lose"), but it does have cost
— a missed moment, a ruined material, a way that shuts for a while. Cost is
allowed. It has to be *interesting*, and usually funny.

- **The joke is on HIM, never on the player.** If the player feels mocked for a
  mistake, it is punitive and it is wrong. If *he* is the one who looked stupid,
  the player laughs with him. Same event, opposite feel.
- **The world embarrasses him; it does not threaten him.** He slips on the wet
  rock. He spooks the thing he was watching.
- **Cost is not permanent.** A soft lock, never a hard one — the way back is
  always there eventually. (How that reopening works is a design question, not
  settled here.)
- **Cost + comedy from the same beat**, so the game never chooses between weight
  and warmth.

**It measures the arc.** Early, something goes wrong and he is embarrassed —
defensive, deflecting, keen to leave. Late, the same pratfall just amuses him.
**How well he takes the joke is how at peace he is** — the confidence arc (§3),
read through failure instead of through attention.

## 4. Why this makes growth non-linear **[LOCKED]**

Stats rise by use ([Growth](../../include/Growth.h)), and which stats rise is the
player's doing. So the confidence he earns is *specific to what he attended to*:
a Wonder-heavy pilgrim grows certain about different things than a Reason-heavy
one, and a survival-heavy one is at home in different places.

**His "self" is the stat distribution the player grew.** "He learns more about
himself" is therefore not a scripted revelation — it is the literal shape of the
save file. There is no single correct pilgrim, and no linear personality track.

Authoring consequence: a thought's register is gated on **the faculty that thought
belongs to**, not on an overall progress number. He is confident where he is
practiced and still hedging where he is not — the same as any person.

## 5. Authoring rules

- **Write both ends.** Every thought needs at least its uncomfortable and its
  at-ease register. Writing only one loses the mechanic.
- **Never write a wiser man.** If a high-tier thought reads as sage, profound, or
  serene, it is wrong — rewrite it as *relaxed*. He is not becoming a monk.
- **Never explain the arc.** No thought may notice that he has changed, that he
  is more at peace, or that he used to be uncomfortable. The player feels it by
  comparison; he never narrates it.
- **Keep the crudeness at the top.** The high end is not refined. A late-game
  thought is *funnier and cruder* than an early one, not more tasteful.
- **The observation layer never grows.** It is objective at every tier. Only the
  thought changes — because only he changes.
- **The theme stays submerged** ([THEME.md](THEME.md)). Nothing here licenses
  stating the afterlife, grief, death, or "peace" as subject matter. Being at
  ease is shown as tone, never as topic.

## 6. What it means for the ending

The treasure is not an object and not a lesson. It is that he is comfortable —
"just being you is enough," free to be himself. Read on the surface: a long walk
that made a man easy in his own skin. Read underneath ([THEME.md](THEME.md)):
a soul arriving at peace, released.

Both readings must be true, and the second is never spoken.

## Cross-references

- [THEME.md](THEME.md) — the submerged subject this voice carries.
- [ORIGINAL_NOTES.md](ORIGINAL_NOTES.md) — source-of-truth for tone; the concept
  in the co-creator's own words ("learns more about himself and what he's
  actually looking for").
- [PSYCHE.md](PSYCHE.md) — the tier mechanic §3 gives
  register to.
- [AESTHETIC.md](AESTHETIC.md) — the melancholic-uplifting surface register.
