# Story

> **Owns:** the main arc of the game — what the player is doing, why,
> and how the experience unfolds across cycles.
> **Status:** drafting

This document captures *the player's narrative experience*. Setting.md
owns the world's rules; story.md owns the arc through them. Where
something is mechanical, it points to other docs (classes.md,
fallback.md, dialogue.md). Where something is content (specific
dialogue lines, Grimoire text, cutscene frame-by-frame), it points
forward to writing-time work.

---

## Premise

A blank soul wakes in the *selva oscura*. He cannot reach the lit
hill. The path forward is downward. A figure called the Guide gives
him the means to descend. He goes.

He does not know he was chosen. He does not know who chose him. He
does not know what becoming the most-saturated soul Hell can produce
would mean. The world conceals its frame. The player learns what the
Vagrant does not: across runs, across cycles, through environmental
cues, NPC fragments, the Guide's slow breakdown, and the Grimoire.

By the end, the player understands the cosmology. The Vagrant never
quite does. He acts on partial information all the way through —
informed by the player's dialogue choices and what those choices
reveal him to be.

---

## The Vagrant

A blank soul. No memory of his pre-Hell life — no name, no death, no
history, no moral biography. His blankness is structural: Hell's
universal forgetting protocol leaves a *residue* (the shape of a
soul's sin, the imprint of its punishment) on most souls, but the
unmeasured retain no residue because no measurement happened. He is
*more blank than the damned*. He has nothing.

He is the **Vagrant** by Hell's framing — a category error, an
unjudged body without a place. Beatrice and her aligned speakers
(the Guide, late-game Grimoire) call him **Pilgrim**. He answers to
both, neither remembering the difference.

**The player's role.** The Vagrant has no fixed personality. The
player gives him a name (six letters, elicited by the Guide in the
opening sequence), responds for him through dialogue trees, and
shapes his posture toward the world through choices. The Vagrant is
*whoever the player makes him*. Setting.md's player-as-Hell framing
is one of the things the player learns over time; the Vagrant never
realizes it.

**No biography.** The game does not flesh out the Vagrant's pre-Hell
life. No memories surface. No NPCs remember him. No Grimoire entries
narrate him. The blankness is permanent — both in-world and as a
narrative commitment. When the player eventually learns Beatrice
chose him, the reason is *structural* (he was the most-unmeasured
soul Hell's failure produced), not *personal*. He is a discard who
turned out to be useful, not a hero who was worthy of selection.

**Visual / appearance.** The Vagrant has a sprite the player sees,
but he has no internal awareness of his own form. He doesn't reflect.
He doesn't think about how he looks. The Wood has no mirrors. He
moves as a shape, not a body.

**Voice register.** The Vagrant speaks via dialogue trees in **modern
register** — modern words for things he doesn't have old names for
(*"some old man with a stick"* rather than *"Charon"*; *"this place"*
rather than *"Acheron"*). Hell's voice — Grimoire entries, gate
messaging, NPC speech, environmental signage — uses the
Commedia-anchored register (Italian loanwords, Early Modern English).
The friction between the two registers is the world's friction-engine:
the player chooses modern phrasings; the world replies in old words.

---

## The opening sequence

Plays once per save. The Vagrant's first contact with the world.

Walkable interactive scene. **Not a cutscene.** Player controls the
Vagrant from frame one.

### Beat 1 — Cold-open in the basic-form Wood

The Vagrant wakes in the *selva oscura*. He doesn't know how he came.
The wood is dark, threatening — Dante's *"savage and harsh and
strong."* Wind moves through it. Undergrowth is real.

The dilettoso monte is visible in the far distance, almost-lit, dawn
suspended on its summit. Light is *almost there*. The instinct is
upward. The player walks freely; the obvious goal is the hill.

The basic-form Wood is the *pre-hub* version of the Wood — smaller,
sparser, the stripped-down version of the place that will later open
as a hub. (Once the Vagrant has met the Guide at Beat 3, the Wood
opens to its hub form, and the basic-form Wood is gone for that
save.)

Player can take the opening at any pace. There is no time pressure.
Five minutes or twenty — the player chooses.

### Beat 2 — Lupa, the last legend

> **Selva-canon refined 2026-05-31** per
> [[selva-wood-lore-locked-2026-05-31]]. The pre-failure *selva
> oscura* had three named apex-legends on the slope —
> **Lonza**, **Leone**, **Lupa** — but Lonza and Leone died during
> Hell's stagnation, before the Vagrant ever arrived. Only Lupa
> survives at game-start. Beat 2 is now the encounter with her
> alone. Earlier "fights all three" framing is superseded.

> **Implementation status (2026-05-31):** Lupa ships with the
> Quaternius CC0 wolf rig. Joint map, hurtboxes, archetype, and bake
> all wired (see [animals_and_multi_skeleton.md](animals_and_multi_skeleton.md)).
> Combat tuning, encounter pacing, and reward structure are WIP.

When the Vagrant approaches the chapel, **Lupa** is already there —
**sitting outside it**. Not stalking, not pacing. Sitting still.
She is visibly weak, tired, malnourished, gaunt (per Dante's *Inferno*
I.49–60, but worn further by the years she alone survived a dying
Wood). She looks sad. She looks like an animal at the end of
something. The player's first sight of her is grief, not threat —
most opening-boss reveals signal "this will hurt you"; Lupa's reveal
is the inverse. An animal that looks like it's barely surviving.

**Encounter beat: sitting → standing → fighting.** Lupa does not rush
the Vagrant. She stands up when he approaches close enough to be a
real threat — that stand is its own animation moment (Quaternius
`Jump_ToIdle` clip serves as the "rise to attention" beat;
`Idle_2_HeadLow` or similar for the sad-standing-idle pre-engage).
Then combat begins.

**Combat tuning paradox (LOCKED):** despite the visible weakness,
**Lupa is mechanically dangerous — two-tap deadly to an unprepared
player**. She is an apex predator running on her last reserves, and
an apex predator's last reserves are still apex. The contrast
between visual weakness and mechanical reality IS the tragedy: the
player kills something that looked sad, learns afterward it was
still dangerous, and feels worse about it. **Do not tune her down to
match the visual.**

Combat profile is the Unburdened class (see [classes.md](classes.md))
since no measurement has occurred. `permanent_on_death: true` —
fought once per save, same pattern as keepers. After she falls, the
chapel exterior stays empty in every subsequent cycle. The empty
chapel-front itself becomes the monument to what was.

**Tonal register: tragic, sift-of-melancholy.** The absence of Lonza
and Leone is felt in the encounter — the colle's slope was meant for
three; only one sits at the chapel. The player who pieces it together
(via item text / NPC dialogue / Grimoire fragments across runs) reads
the absence; the player who doesn't reads a lone gaunt apex-beast in
front of a stone tomb, which already lands tragically. Either way,
the encounter carries the weight of *what was lost.* Quiet grief
register, not horror, not boss-of-the-week.

**Texture note.** Lupa has been here a long time — long enough to
have outlasted her sister-legends, long enough to have hunted in a
Wood progressively emptying around her. Her fight has a *patina* of
endurance — wary moves, instinct sharpened by lonely centuries.
Worn but no less terrifying. The opening should communicate this
texture without stating it. Specifics deferred to combat / writing.

**Player-knowledge note (per
[[selva-epistemic-doctrine-2026-05-31]]):** the player is not told
why Lupa is alone. They are not told that there were three. They are
not told what killed the others. They encounter a wolf on a dead
slope; they may infer absence; the canon (Lonza and Leone died from
Hell's stagnation, Lupa is the lone survivor) is iceberg-below,
surfaced gradually only via fragments in later content.

### Beat 3 — The Guide arrives

After Lupa falls, the Guide emerges from the chapel. The chapel door
— locked at the Vagrant's arrival — opens, and the Guide steps out
into the Wood. Not before; specifically after the encounter, paralleling
Dante's Virgil meeting him in the lower place after the retreat.

The Guide speaks in old register. He is **warm, friendly,
procedural** — unambiguously a companion. No register-friction the
player should feel; no faint wrongness; no uncanny undertone. The
opening Guide is *fully himself*. He is **glad to see the Vagrant**
in a way he cannot articulate — the warmth of a guide-shaped soul,
finally able to be the thing he was named to be. He treats the
Vagrant's presence as expected. He explains the descent as the path
forward. He frames it as how souls proceed.

The gladness is real but the conditions for it were authored.
Beatrice positioned the Guide in this Wood, at this time, with this
context, for this arrival. He doesn't know this. He just feels
relief; he just feels the pleasure of finally being able to guide
someone. (See *The Guide / Identity*, below, for the full
instrumentalization.)

> **How Lupa actually died (canonical).** The Guide did NOT fight her.
> A soul-form actor cannot win a direct engagement with an apex animal,
> and the Wood permits combat across forms (see
> [bestiary.md](bestiary.md) *Soul-form vs animal-form*).
> Instead: the Guide arrived in the Wood, found Lonza's carcass near
> the chapel (Lonza had died of starvation in the dying pre-failure
> Wood — also Beatrice-arranged), poisoned the carcass, and waited
> inside the chapel for Lupa to scavenge it.
>
> Lupa, gaunt and starving — kin-of-Lonza, the last apex predator on
> the slope — ate the carcass. She had no reason not to: the carcass
> was unprecedented food, kin she had hunted alongside, and her hunger
> was insatiable by canon (*Inferno* I.49–60: *mai non empie la
> bramosa voglia*). The poison began acting immediately. By the
> time the Vagrant arrives at the chapel, Lupa is already dying —
> visibly weak, sad, sitting because she can no longer stand long.
>
> The Vagrant engages her. Mechanically the player damages her and
> "kills" her; canonically she was already dead the moment she took
> the bite. The Vagrant's role in her death is participatory but
> not load-bearing. She would have died on the slope within minutes
> if the Vagrant had simply walked past.
>
> The Guide perceives this as a deliberate act of self-preservation
> and path-clearing: an apex animal in his vicinity, an unjudged soul
> arriving, a poisonable carcass at hand, no other route. He believes
> he chose. He chose the way a guide-shaped soul placed precisely
> there with precisely those variables in front of him *would* choose.
> Beatrice did not need to compel him. She arranged conditions such
> that his own nature would produce the act. This is the pattern of
> every subsequent thing he does for the Vagrant: he believes he is
> acting; he is. He is also executing her plan. Both are true
> simultaneously. **The cruelty of his instrumentalization is that
> the act felt like his own choice.**
>
> The player's perception of the encounter: a starving wolf, a fight,
> a victory, a companion who emerges. The actual structure: Beatrice's
> opening move, executed through two pawns who do not know they are
> pawns. The reveal — that the Guide killed Lupa, that Beatrice
> authored the sequence — surfaces gradually via Grimoire fragments
> and (specifically) the Guide's posthumous entries at R2.

The Guide's *off-ness* develops only later, as the contrapasso-leak
arc proceeds. Per the degradation table (below), he is *coherent,
warm, responsive* through keepers 1-3; cracking through keepers 4-6;
incoherent through keepers 7-8; fully degraded at all keepers down.
The early Guide is the warmest companion in the game; the late Guide
is the wreck of that companion. The early warmth must be real and
unambiguous so the late kill is grief.

He drops one seed line, casually:

> *"Souls do come through here. Many of them. More now than there
> used to be."*

This is the only hint at Hell's failure in the opening. Factual
on its surface. *More now than there used to be* opens a door for
attentive players to walk through. The player who notices may
form questions; the player who doesn't reads it as flavor.

The Guide asks the Vagrant **what to call him**. The player types
six letters. The name is *given*, not *recovered* — the Vagrant
does not remember his real name; he answers to whatever the player
provides. (NAME_ENTRY UI specifics in [ux.md](ux.md).)

The Guide then offers **the Signing** (*La Firma*). This is a
ritual he performs on the Vagrant, not an item he hands over. He
explains its function as a formality of intake: *"Pilgrim, thou
art unmarked. None can pass into the depths unmeasured. Bend thy
head; I shall sign thee."*

### Beat 4 — The choice (class-picker UI)

After the Guide's framing dialog, a **class-picker UI** fires:
modal full-screen, four options laid out clearly, two-step
confirmation, no back button, explicit-permanent text in the UI
chrome. The Guide's belief that the Signing is necessary is HIS
frame; the UI presents the choice honestly so the player can
commit with eyes open.

Options:

- **Penitent / Heretic / Wretched** (any of the three) →
  Signing happens cosmologically → imprint is created → the
  Vagrant receives **the Crucible**. Stats unlock. Standard path.
- **Refuse** → no Signing → refusal-channel opens → the Vagrant
  receives **the Censer**. Stats stay at baseline; growth comes
  from riversamento alone via the Censer (per setting.md
  *The unburdened path*).

The choice is decisive at this beat. The Signing window is
cosmologically one-shot for the Guide because the Lupa-fall
arrangement supplied the chrism-equivalent substance the ritual
required; once spent, the Guide has nothing to draw on. Later
Erasures re-enable the ritual via Vagrant-gathered material
prerequisites and re-run the same class-picker UI.

The Guide responds differently:

- **On accept:** encouraging. The Vagrant has stepped onto the path
  the Guide expects. The Guide is reassured.
- **On refuse:** *worried*. The Guide does not understand
  declining; he believes the Vagrant is making the journey harder
  than it needs to be. He may say something like *"Thou art
  certain? The path is not made for the unmeasured."* He thinks he
  is looking at stubbornness. The framework Beatrice supplied —
  that the path is *made for the measured* — is the only frame he
  has, and the unburdened Vagrant violates it in a way the Guide
  cannot articulate. He does not know that he himself is also
  unmeasured; he does not remember refusing at the gates centuries
  ago.

Per the Pawn Doctrine: the Guide believes the Signing is care for
the Vagrant. He does not know (concealed pre-R2) that the Signing
is also the mechanism by which Beatrice's saturation claims an
unjudged soul. From his point of view, he is welcoming a pilgrim.
From the canonical point of view, Beatrice configured the ritual
his guide-shape produced.

The worry is not *almost-recognition.* It is the Guide's framework
failing to handle a case Beatrice did not prepare him for. (See
*The Guide / Identity*, below — Beatrice supplied the role and the
framework; she did not anticipate an unburdened Vagrant when she
built either.)

### Beat 5 — Transition

The Vagrant proceeds to the gate of Hell. The basic-form Wood
transitions to the **hub-Wood** — the place expanded for ongoing
runs. The opening sequence ends. The descent begins.

### What the player knows after the opening

- The Vagrant is in some version of Dante's Hell.
- He cannot reach the lit hill.
- Beasts in the Wood drove him back. He killed them.
- A figure called the Guide met him, named him, gave him passage.
- The path forward is downward.
- The Guide signed him (class-picker) or did not (unburdened).

What the player does *not* know after the opening (concealed
intentionally — see *Concealment* below):

- That Beatrice exists, or chose him.
- That the Guide is her instrument.
- That he is unjudged / chosen / different from other souls.
- That he is Hell-as-observer (player-as-Hell framing).
- That a post-Lucifer fight exists.
- That cycles will be cosmologically meaningful (the player will
  initially read death-as-restart as roguelike convention).
- That four endings exist, and they fork on path + final choices.

---

## Concealment as principle

The story arc is built on **gradual revelation**. The opening
presents the world as routine afterlife processing. Reveals are
earned across cycles — through gameplay, NPC fragments, Grimoire
unlocks, environmental cues, and the Guide's slow breakdown.

**Two design rules:**

1. **Every reveal should be earnable through attention.** Players
   who pay attention can figure out the truth before it is stated.
   Players who don't will be surprised when the reveal lands.
   *Both kinds of players should feel rewarded* — the careful
   player by being right, the casual player by being moved.

2. **The player learns through Hell.** The player is Hell-as-observer
   (per setting.md *Perspective — who the player is*); the Grimoire
   is Hell's voice narrating its own situation; NPC fragments are
   souls speaking from inside their punishment; the Vagrant himself
   is hollow and does not learn — the player synthesizes from Hell's
   self-narration plus what souls let slip in their lucid windows.
   The Vagrant carries no understanding across cycles; he is not the
   audience. The player is the only consciousness in the loop that
   can integrate what unlocks.

The pacing of specific reveals is locked in *Reveals*, below.

---

## The Guide

A persistent presence in the Wood and (early- to mid-game) in Hell.
The Vagrant's most-frequent conversational partner. The dialogue-
volume center of the game.

### Identity

He is a soul who **refused Hell's measurement at the gates**. The
selva oscura received him — the natural cosmological destination of
the unjudged (per setting.md *The selva oscura as cosmological
destination*). He has been there since.

**He is unjudged**, like the Vagrant. The same cosmological rule
applies — Hell cannot grip him because he carries no imprint. This is
what made him, much later, useful to Beatrice. She found him already
deflected, already settled in the Wood, already in the right state for
her to use without altering him. He is the right tool *because* he
has been forgotten by Hell.

**He does not remember being that.** His refusal-state has eroded his
memory of the original act. He does not know he was the first
unburdened. He does not know Beatrice found him long after he had
lost any sense of his origins. He just is the Guide as he understands
himself: a helpful figure in the Wood.

**Character.** Easygoing, intelligent, tough, protective. A genuinely
*good* soul. The kind of person who, in life, would be reliably there
for others — quietly capable, refusing his own importance, returning
warmth without expectation. The tragedy of his fate works *because*
he is good. A used pawn who was selfish or weak would be a different
story (a Patches betrayal). The Guide is the inverse: his goodness IS
the leverage Beatrice uses on him. His instincts for help, for
protection, for steady presence are real and unaltered; they are
also the exact instruments by which the plan proceeds. He lost his
way (refused measurement, eroded memory, became Beatrice's vessel) —
but he never lost the *shape*. The shape is what kills him.

**He does not know Beatrice's plan.** He believes he is helping. His
warmth, his help, his belief that he is doing the right thing — all
real, all genuine. He is a pawn who does not know he is a pawn.

**Every act of his is plan-execution.** From the Lupa-poisoning
(*Beat 2*, *Beat 3* above) through to the climax (*The climax*
below), the Guide acts on what feels to him like agency: practical
choice, self-preservation, helping the Vagrant, doing what a guide
does. The Vagrant-facing perception aligns with this. The canonical
reality is that every choice was authored — Beatrice arranged
conditions such that his guide-shape, placed in those conditions,
would naturally produce the outcome her plan required. She did not
compel; she configured. The Lupa-poisoning was the first authored
act the player witnesses (after the fact). The Signing-offer is
the second. Every Hell-projection is the same pattern. **He believes he
chose. He did. He chose the way she needed him to.** Both are true.

**What he is, before Beatrice.** The Guide's *guide-shape* — his
inclination toward help, his warmth, his patience — predates his
arrival in the Wood. His pre-Hell soul (whatever life he led before
Hell) had this shape. When he refused measurement at the gates, Hell
installed nothing in him (no imprint to place) — but the pre-existing
shape of his soul was not erased. He arrived in the selva oscura as
an unjudged soul whose natural inclination was to be helpful.

**What he is, after Beatrice.** When Beatrice found him, his memory
of his original life had eroded and his guide-shape had no
biographical anchor. She named what he was: *the Guide*. She
supplied the role-as-identity his amnesia could not produce on its
own. She positioned him in this Wood at this time for the Vagrant's
arrival — placed for this moment, ready for this moment. The Guide
has been *himself* (guide-shaped) inside a *named role* (her
construction) inside a *configured arrival sequence* (her arrangement)
from his placement onward.

**What this means.** His warmth at the Vagrant's arrival is real —
he is genuinely a helper-soul finally able to help. His role-as-Guide
is also real — he believes himself a guide and acts the part with no
pretense. Both are him. Both were captured by Beatrice. He is not a
fake; he is *himself, used.* The cruelty of his instrumentalization is
that she did not replace him with something he wasn't; she co-opted
what was already true about him, deprived him of opportunity to
express it on his own terms, and released it on the Vagrant when her
plan needed it.

His unmeasured state is what lets him host Beatrice's projection
without Hell installing him into something else. **The same projection
channel doubles as a sangue conduit** — when each fallen keeper's
ring restores under its new mangled keeper, the bottled sangue of
that ring starts moving again, and some of it routes through
Beatrice's channel into the Guide. Senza forma should make him
impervious; the channel overrides this. The result is degradation:
substance flowing through him erodes the vessel.

His destruction is *acceptable cost* in Beatrice's plan. She did not
premeditate it as the goal; she did not think hard enough about whether
using him would destroy him. The result is the same. The Guide is
*also* one of her pawns — alongside the Vagrant — used without his
knowledge, never told what he is, dying not knowing.

### Geography

His real presence is in the Wood. His Hell-side appearances (per-
keeper, mid-run) are **projections** sustained by Beatrice's
intervention.

- **Wood-body:** thick. The Guide as he is.
- **Hell-projection:** thin. A trace of the Wood-body, sustained
  remotely.

### Functions

In Hell, the projection appears at per-keeper interludes (between a
circle's play segment and its boss). It offers:

- **Heal** (RELIC equivalent — sangue-for-heal).
- **Offerings** (stat upgrades for class-pickers).

The Guide is not aware of the cosmological role these functions play.
He believes he is providing standard guide-services.

### Calls the Vagrant Pilgrim

The Guide addresses the Vagrant as **Pilgrim**, not Vagrant. He is a
Beatrice-aligned speaker in his vocabulary, even though he doesn't
know Beatrice or his alignment. *Pilgrim* is the name he was given
for the role he plays.

Attentive players may notice that the Guide is the only character
(other than late Grimoire entries) who consistently uses the formal
register name. **This is Beatrice's vocabulary threading through
him.** She supplied the formal address as part of her
instrumentalization — *Pilgrim* is the name she gave him for the
role he plays. He uses it without knowing it is her word. On replay
after R2, attentive players reread this as the architect speaking
through the warmest exchange in the game: every time the Guide says
*Pilgrim*, Beatrice's voice has been threading the dialogue.

### Degradation across cycles

As keepers fall, **each consumed circle's long-bottled sangue is
released and starts moving again**. Some routes through Beatrice's
projection channel into the Guide. He becomes a cumulative
receptacle of restored-ring sangue — by 9 keepers down, loaded with
substance from all 9 circles' worth of releases.

The Hell-projection fails first. The Wood-body holds longer.

| Cycle stage | Hell-projection | Wood-body |
|---|---|---|
| 1-3 | Reliable. Appears at every per-keeper interlude. Coherent, helpful. | Coherent, warm, responsive. |
| 4-6 | Cracks. Sometimes appears mid-sentence. Sometimes doesn't appear. Sometimes contradicts himself. | Visibly tired. Slower. Sadder. Less responsive. |
| 7-8 | Rare. When it appears, it is a wisp. | Forgets which path the Vagrant is on. Repeats earlier statements. Sometimes doesn't recognize the Vagrant. |
| All keepers down | Stopped entirely. No more in-Hell appearances. | Fully degraded. Loaded with sangue from all 9 restored circles. |

**Mechanical consequence:** the player loses access to in-Hell
heal/offerings progressively before the climax. By 9 keepers down,
the player has been operating without reliable Guide services for
much of the late game. This is the resource-economy expressing the
cosmology.

**Emotional consequence:** the Guide is the warmest, most personal
NPC in the game. Cycles of warmth across the early and middle game
build the player's attachment. By the late cycles, his breakdown is
visible and painful. By the final encounter, the player has been
watching him deteriorate for hours.

### The climax

After all 9 keepers are felled, the Vagrant returns to the Wood. The
Guide is fully degraded — full of every contrapasso, no longer
himself, monstrous-but-confused.

**This encounter is the surface for R2** (the Beatrice reveal — see
*Reveals*, below). Posthumous Grimoire entries unlock at the Guide's
death and name Beatrice for the first time anywhere in the game.
Until this moment, the player has had no information about her at
all — only the unidentified title-screen image they have walked past
since boot.

**On class-picker paths: combat.**

The Guide attacks. His combat profile cycles through the 9 circles'
contrapasso behaviors as attack patterns — a wind-blast (Lust), a
weight-drop (Greed), a poisoned rain (Gluttony), a burning-tomb
gesture (Heresy), a hooked-grab (Fraud / Malebranche), an
ice-encasing (Treachery), and so on. He is the contrapasso made
flesh. (Specifics in [classes.md](classes.md) / per-encounter design.)

The Vagrant kills him.

The Guide dies *still believing he was helping*. He does not know
what is happening to him. His battle-dialogue is fragmentary and
bewildered — broken liturgy, half-finished offers, gestural
guidance from a body that no longer holds together. He does not
say *"I was a pawn"* or *"I refused"* — he doesn't know either.
The reveal is not in his words. It is in the *fact of his
incoherent breakdown* and in posthumous Grimoire fragments.

**On the unburdened path: handover.**

The contrapasso is in him equally, but the framework Beatrice
supplied does not have an attack-pattern for an unburdened Vagrant.
The Guide-as-tool was configured for a class-picker; the unburdened
Vagrant slips past the configuration. With no instruction to follow,
the Guide does what his original soul-shape (helper, guide) defaults
to — he passes the relic. The Vagrant receives it willingly, or it
falls from the Guide's outstretched hand without resistance. He
fades. No fight. The contrapasso saturating him is held back not by
recognition but by the absence of a configured response.

His final words on this path are minimal. Gestural, half-finished.
The player infers from the totality of his arc what he was. The
unburdened gets the same revelation as the class-picker, but
quieter.

### What the Guide leaves behind (the relic)

The Guide carries cosmological residue from his role as Beatrice's
projection-channel. At his death, that residue collapses out of him
and the Vagrant inherits something. The specific form and function
of this inheritance is TBD — leveling is already portable from Beat 4
via the Crucible / Censer, so the old "portable OFFERINGS access" role
the Hand used to fill no longer exists.

- **Severed at the kill** (class-picker) or **passed at the handover**
  (unburdened).
- **The Vagrant carries it.** Whatever it is, the relic is in
  inventory, terrible to look at, terrible to know what was done.
- **Function TBD.** Possible roles:
  - Healing item (the pre-Hand-retired healing problem may resolve
    here)
  - A second cosmological tool that complements the vessel
  - A late-game progression unlock that requires the Guide's residue
  - A late-game class-evolution gate
- **Permanent across cycles.** Once acquired, the Vagrant has it for
  the rest of the save.

### Tone — the kill is Blaidd-coded

Reference: Blaidd in *Elden Ring*. A friendly mentor figure who
goes mad and the player has to put down. The kill is grief and
relief at the same time.

The Guide's role in story.md is to be **the warmest, most-personal
NPC across many cycles** before the breakdown. Killing him should
feel horrible *and* a relief — the resource pressure of the late
cycles, when his projection has stopped appearing in Hell, has
built up an actual mechanical reason to want the encounter to
end. The player is complicit in his death; they wanted it for
reasons that turn out to be Hell's reasons (the Guide's residue
is genuine progression).

### The truth comes out posthumously

The Vagrant — and the player — learns who the Guide was *after*
killing him. Specifically:

- Posthumous Grimoire entries that unlock at the climax (this is also
  where Beatrice is named for the first time anywhere in the game —
  see *Reveals*, below).
- NPC fragments dropped by the few NPCs still encounterable late
  in the game.
- Environmental cues in the Wood after the Guide's death (his
  body / the dropped Hand persists; players who return notice).

The truth is not stated. It is assembled by the player from
posthumous evidence. The reveal lands as *recognition*, not
exposition.

---

## World texture

Two textures suffuse the back half of the game. Neither is a discrete
reveal — both are gradually-obvious world-conditions that the player
absorbs across the descent and only fully resolves at later reveals.

### Hell is failing

A progressive-obviousness texture, not a moment.

- **Hint at start:** the Guide's Beat 3 line — *"Souls do come through
  here. Many of them. More now than there used to be."* Sufficient.
- **Conveyance, layered across the descent:**
  1. **NPC dialogue past-tense refrain.** Each circle's NPC drops one
     short past-tense fragment in their lucid window — phrased per
     their sin's register. *"There was room in the wind, once."* /
     *"Minos used to look at each of us."* Concise, one or two lines
     per NPC.
  2. **Per-circle mechanical pressure.** Each circle is harder than
     the last despite getting smaller — the difficulty curve *is*
     the saturation. Each circle's signature contrapasso has become
     a different thing than it was: the wind in Lust isn't *more
     wind*; it's wind thickened past its original function.
  3. **Per-circle landmark cue.** One or two memorable images per
     circle that show structural strain (a wall bowing, a queue that
     never shortens, an architectural element giving way). Specifics
     deferred to per-circle design.
  4. **Audio signature.** Per setting.md's location-determined audio:
     ambient murmurs denser than the keeper's motif can fully cut
     through; mid-circles thicker than their slot.

The word *failing* is never spoken; the player arrives at it.

### The Vagrant is treated differently

Also progressive-obviousness, also not a moment. Resolves at R2 (the
Beatrice reveal) rather than as its own beat.

- The *Pilgrim* register the Guide consistently uses; the dialogue
  branches that fire only for the unjudged; NPCs reacting to him as
  off-pattern; the persistent strangeness of run-end (*NOT YET / THOU
  DOST NOT BELONG*).
- The player accumulates *I am not like the others* without anyone
  naming why. The why arrives at R2, bundled with the rest of the
  Beatrice reveal.

---

## Reveals

The story arc is built on **gradual revelation** (per *Concealment as
principle*, above). Reveals R1-R5 are the discrete narrative beats
where the player's understanding inverts. Each has a *trigger* (what
unlocks the reveal) and a *surface* (where the player sees it).

### R1 — Forced repentance via second death is what kills do

What every kill has been. The act the damned cry out for and Hell
will not give. Granted by the Vagrant because he is unjudged — and
when he receives it, the protocol fires but does not complete (no
imprint to grip; the soul is ejected to the Wood instead of being
finished).

- **Phrase enters vocabulary from run 1.** The *NOT YET / THOU DOST
  NOT BELONG* death-card teaches the cosmological refusal in two
  registers from the player's first death (specifics in setting.md
  *Second death*).
- **Hint at start:** an early Grimoire entry quoting *Inferno I:117*
  — *"ch'a la seconda morte ciascun grida"* — paired with a fragment
  about *the gift the damned cry out for and Hell will not give.* The
  player has the phrase and the literary referent; not yet the
  connection through the Vagrant's kills.
- **Trigger:** mid-game, around keeper 3 (Gluttony). Guide is still in
  his coherent window per the degradation table.
- **Surface:** the Guide, in the Wood, between runs. Names the
  symmetry directly. Concise. Two beats: kills are sacramental; the
  Vagrant cannot be finished. The cosmological rule of *the unjudged*
  is named here for the first time.
- **What the player synthesizes retroactively:** every shade and
  keeper killed has been *resolved*, not destroyed. The kill-loop
  is a sacrament-loop. The Vagrant has been performing the
  throughput Hell stopped doing. *And* — the persistent strangeness
  of game-over (sangue lost, Vagrant rises again) is now a named
  cosmological rule. Every prior death was Hell trying its protocol
  on a soul it cannot grip. The cycle structure was never roguelike
  convention — it was the cosmology.
- **Why the Guide as surface:** NPCs are blinkered (per setting.md);
  the Grimoire alone would be cold; the warmest character delivering
  this is heaviest. Also seeds R2 — late-game the player remembers
  this line and asks *how did he know?*

### R2 — Beatrice exists, has a plan, the Vagrant is her chosen unjudged instrument

**The largest reveal in the game.** Single grand moment. Absorbs the
Vagrant's *unjudged class* framing, the Guide's *fellow-pawn* status,
and the *class-evolution-as-Hell-loading* implication.

- **Hint at start:** the title screen. Beatrice unidentified, halo
  intact, slow-disintegration animation. Player has been seeing her
  since boot. **No other clue exists in the game until R2 fires** —
  no NPC fragment, no Grimoire reference, no environmental cue. The
  title screen is the entire foreshadowing track.
- **Texture across the back half (keepers 3-9, post-R1 / pre-R2):**
  the player accumulates two questions without external resolution:
  *what is wrong with Hell?* (R1's *Hell is failing* texture) and
  *what is wrong with me?* (R1's *Vagrant is treated differently*
  texture). No reference to any agent outside Hell. No fragments of
  *her*. The Grimoire does not name her.
- **Trigger:** the Guide's hostility (class-picker) or fade
  (unburdened) at the Wood, after all 9 keepers are felled.
- **Surface (the cluster):**
  - **Guide's battle-dialogue and fade-dialogue.** Broken liturgy,
    half-finished offers, fragmentary gestures. He uses Beatrice's
    phrasings without knowing he is quoting her. Player hears her
    voice through his breakdown.
  - **Posthumous Grimoire entries** unlock at the Guide's death.
    These are the *explicit* surface — the entries that name
    Beatrice, name the unjudged condition as a class property both
    the Vagrant and the Guide share, name the placement, name the
    plan in broad shape.
  - **The Hand.** The item the Vagrant carries forward — now reread
    as the hand of a peer, another unjudged soul Beatrice used.
- **What the player synthesizes (all at once):**
  - The woman on the title screen has been Beatrice the whole time.
  - She is the agent behind everything — the placement, the Guide's
    instrumentalization, the entire arc.
  - The Vagrant is unjudged — and unjudged souls are a cosmological
    class. Most go to selvas oscuras of their own. The Vagrant is in
    Hell because Beatrice put him there.
  - The Guide is also unjudged. He never knew. He died not knowing.
    Both Vagrant and Guide have been pawns; the Guide was used through
    a channel Beatrice constructed, and the channel destroyed him.
  - **Class evolution has been Hell loading itself into the Vagrant**
    (class-picker, the class-picker is Hell's installation taking
    shape inside him) **or routing through him into Beatrice**
    (unburdened — riversamento has been pouring substance INTO HER
    SUBSTRATE, not into a neutral reservoir; per setting.md *Sangue
    saturation of Beatrice*). The Vagrant's transformation across
    the game has not been his choice; it has been the cosmology
    operating on him.
  - **Unburdened-specific R2 horror:** every riversamento has been a
    drop of Hell-substance into Beatrice. The figure on the title
    screen — the disintegrating Madonna-coded form — has been
    transforming in real-time, across every boot of every cycle.
    The halo cracking is Hell working outward inside her substrate.
    The unburdened Vagrant thought he was choosing the gentle
    ending; he has been *making a Satan-of-Paradiso* inside the
    figure he is converging toward. PURITY, the ending he is
    racing toward, will kill her before the transformation
    completes. On post-R2 replay, every prior boot is horror.
  - Beatrice's intentions: radical reform of a failing cosmology,
    executed by a god ground down past coherence and visibly
    destabilized. Not a dictator — a stir-crazy reformer who acted
    because the staring became unbearable. The unburdened reveals
    a second layer: she is also *the medium* of one of the endings,
    and her substrate has been bearing the cost of that medium-ship
    throughout.
- **Tone:** the player does not get a clean villain. They get a
  fellow soul who broke under conditions the player has now
  experienced enough of to *understand.* The horror is partly that
  the player cannot fully condemn her — they have walked through the
  same broken cosmology and felt the strain.

### R3 — TRANSFIGURATION / the throne

The Vagrant has been groomed for Lucifer's seat. The post-Lucifer
sequence is the cosmos's emergency response to the structural
vacancy. Surviving qualifies him; the throne is offered in silence.

- **Hint at start:** none. Active concealment — the entire descent
  reinforces *Lucifer = end of game.*
- **Trigger:** post-heavens-descent (R4 Branch A), class-picker only.
- **Surface:** the throne. Silent. Cocytus is empty. No NPC, no
  Grimoire pop-up, no Beatrice voice, no descended figure offers it.
  The cosmology has produced a vacancy and the player walks into it.
  Three actions: **sit (TRANSFIGURATION), sit-with-stats-maxed
  (SURFEIT — only available if all three stats invested to cap),
  refuse and walk away (REFUSAL).**
- **What the player synthesizes:** the throne is structural, not
  authored. *No one is steering this.* The cosmology improvised; the
  Vagrant is the only candidate available. Their class-evolution arc
  was *grooming for this* (locked at R2). Three of four endings are
  acceptable to Beatrice; only REFUSAL opposes her — and at REFUSAL,
  she descends rabid (R4 Branch C).
- **Tone:** the cosmological emergency, witnessed alone. The architect
  is offstage; the cosmology has run out of speakers. The decision is
  not narratively framed by anyone. Just the throne, and the player.

### R4 — Post-Lucifer cosmological response

The cosmos responds to Lucifer's defeat differently depending on
what kind of soul produced it. Three branches:

**Branch A — Class-picker / heavens-descent.**
- *Trigger:* Lucifer felled by class-picker.
- *Surface:* three sequential fights in Cocytus, one per realm, in
  geometric-ascent order. **Hell's wave** (the three traitors freed
  from Lucifer's mouths — Judas / Brutus / Cassius — as one
  three-form boss); **Purgatory's wave** (the Angel of the Gate);
  **Heaven's wave** (Mary). Mary is the climactic fight — the figure
  whose Madonna-iconography Beatrice has been impersonating,
  descending to clean up the transgression Beatrice unleashed.
- *Tone:* unwelcome, claustrophobic, horror-coded. The descent is
  threat assessment, not heroic ascent — the cosmos defending itself
  against the threat the Vagrant has become. Surviving qualifies
  him for the throne (R3 follows).
- *Beatrice does not descend on this branch.*

**Branch B — Unburdened / Beatrice fight (PURITY).**
- *Trigger:* Lucifer felled by Diaphanous Vagrant. Firing cocks but
  does not fire; Beatrice is the holdback.
- *Surface:* Beatrice descends — already fully rabid, already coming
  apart. Stop-motion materialization, halo half-shattered. She does
  not announce. She lunges. Single-state combat, no phases, fully
  unhinged from arrival. **No recognition of the Vagrant** — she
  fights an obstacle, not a person. Whatever she says is fragmentary
  throughout. The title-screen disintegration completes in real-time
  during the fight.
- *Combat hook:* the Vagrant fights by riversa-ing in front of her —
  channeling more of himself out, advancing the firing, undoing her
  further. Each pour-out is a damage event. He defeats her by
  completing the act she has descended to stop.
- *Aftermath:* she falls. The firing fires. Cascade per setting.md:
  Beatrice → Lucifer's effects → shades and keepers → Hell's
  geography → the Vagrant. Hell ceases.

**Branch C — REFUSAL / Beatrice fight.**
- *Trigger:* class-picker walks away from the throne after surviving
  the heavens-descent. The throne stays empty; nothing fires.
  Beatrice cannot accept this.
- *Surface:* Beatrice descends rabid — the *fresh-snapped* version
  (the unraveling completes in this instant, not across the unburdened
  arc). Same combat profile as Branch B; the figure is the same. She
  descends to prevent the Vagrant from leaving Cocytus. Defeating her
  is the cost of sealing the refusal.
- *Aftermath:* she falls. The Vagrant walks out of Cocytus. REFUSAL
  ending fires. Hell stays broken; the anomaly persists.

**The unifying rule: Beatrice descends on the paths she is mad at.**
REFUSAL (no change) and PURITY (her own ending). She does not descend
on TRANSFIGURATION (her plan succeeds) or SURFEIT (impersonal cosmic
catastrophe). The player meets her only on the paths that fail her.

### R5 — The player is Hell

The player has been Hell-as-observer the entire game. Setting.md
*Perspective — who the player is* commits the structural truth.
R5 is the discrete reveal of this truth.

R5 is the most abstract reveal. Explicit naming earlier would
cheapen it. Instead: **mechanical hints throughout the entire game,
discrete reveal at the final scene of each ending.**

- **Hints throughout (mechanical, not declarative):** see setting.md
  *Perspective — who the player is*. The Vagrant is hollow; he
  doesn't speak; the player names him; the Grimoire is Hell's voice;
  the interface is Hell's bureaucracy; the death-card has been the
  player's two voices in conflict from run 1. None of these name
  R5 — they produce the *feeling* of being a puppeteer in Hell's
  chair.
- **Trigger:** the final scene of each ending. R5 fires differently
  per path; the surface is the ending itself.
- **Surface — per ending:**

  **TRANSFIGURATION final scene.** The Vagrant takes the throne;
  becomes Satan-2; Hell renews around him. As the geography
  reconfigures, the camera pulls back from the Vagrant-on-the-throne,
  far enough that the player's perspective becomes explicit — the
  player has just installed themselves a new vessel. Possible text:
  *YOU REMAIN.* Hell continues; the player continues as Hell; the
  Vagrant has been absorbed into the role. The cycle is the
  cosmology's; the player is the cosmology.

  **SURFEIT final scene.** The Vagrant ruptures; cosmic war;
  universe ends. The player's view *also* ends. Black screen.
  Silence. Possible text: *YOU END.* Or no text — the screen is
  black, the audio is gone, and the player sits with the absence.
  Hell ended; the player ended; everything ended. The horror is
  that the player's *position* (Hell-as-observer) has been
  consumed alongside Hell. There is no observer left because
  there is nothing to observe.

  **REFUSAL final scene.** The Vagrant walks out of Cocytus; Hell
  stays broken; Beatrice is dead; the anomaly persists. In the
  cutscene's pace, the Vagrant turns toward the camera. He does
  not speak. But he looks. The player understands they have been
  *seen* by their own avatar, finally, in the only ending where
  the act of refusal gave the Vagrant enough will to register the
  puppeteer. Possible text: *YOU REMAIN. SO DOES IT.* — both the
  player and Hell persist; the failure is that *neither of you
  ends.*

  **PURITY final scene.** The cosmic firing fires through the
  Vagrant; cascade unmakes Hell. The player's view changes during
  the firing — the screen empties (Cocytus → geography → selva
  oscura → title screen, image dispersing). Then black. Possible
  text: *YOU WITNESSED.* Past tense. The player was the witness;
  Hell is gone; there is nothing more to observe. The player's
  role as Hell-as-observer is complete and concluded.

- **What the player synthesizes (per ending):**
  - TRANSFIGURATION: continuation horror — *I am now Hell installed.*
  - SURFEIT: total absence — *I ended with everything.*
  - REFUSAL: trapped persistence — *I and Hell, both broken, forever.*
  - PURITY: completion — *the watching is done. The role resolves.*

- **Why the *YOU* text overlays.** Through the entire game, even
  *THOU DOST NOT BELONG* uses *thou* — Early Modern register the
  player parses as Hell's archaic voice, not yet read as *to me.*
  The R5 *YOU* is the modern register, breaking frame. **It is
  the only time in the game *YOU* is addressed to the player
  explicitly.** The phrases are heavy because they are unique.

---

## What's still TBD in this doc

1. **Cycle structure as narrative experience.** What's different about
   cycle 1 vs. cycle 5 vs. cycle 9 for the player, beyond the Guide's
   degradation already locked. World-state changes, NPC absences (NPCs
   fade across cycles per setting.md), gate messaging degradation.

2. **The Grimoire as the surface that delivers reveals (specifics).**
   The Grimoire's *role* is locked (Hell's voice, see setting.md
   *The Grimoire is Hell's voice*). Open: what the entries actually
   contain; what unlocks when; how the register-shift across the
   game (Hell waking up to itself) is paced into specific entries.
   R2's posthumous Grimoire entries especially — the explicit naming
   of Beatrice and the unjudged condition at the Guide's death —
   need their shape locked.

3. **The four endings as narrative experiences (cutscene-level
   detail).** Shape locked at the *Reveals / R5* level (per-ending
   final scenes drafted). Open: full cutscene specifics, frame-by-frame
   beats, exact text overlays (*YOU REMAIN.* / *YOU END.* / *YOU
   REMAIN. SO DOES IT.* / *YOU WITNESSED.* are working candidates;
   final wording TBD).

4. **Beatrice's appearances** — mostly answered. She does not appear
   before R2. She appears as a fight on REFUSAL and PURITY. Open: any
   other appearances (dreams between cycles, post-credit content,
   subsequent-boot title-screen state changes after PURITY).

---

## Cross-references

- [Setting](setting.md) — the world this story takes place in. The
  cosmological rules, the geography, the locked terms (Vagrant /
  Pilgrim, sangue, riversamento, the Signing, markers, the Erasure,
  vestigia, the Crucible, the Censer).
- [Classes](classes.md) — the Vagrant's combat capabilities,
  class-pick fork, evolutions, the Unburdened path mechanics.
- [Companions](companions.md) — the Guide as character, in
  greater detail.
- [Dialogue](dialogue.md) — NPC tree shapes, register per
  speaker, state-aware branching.
- [Inventory](inventory.md) — items in detail, including the
  Hand's mechanics.
- [Fallback](fallback.md) — death, retry, and what cycles mean
  mechanically.
