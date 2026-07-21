# Wayworn Hush — Story Frame

> **Owns:** the spine of the game — why he is here, what he is doing, what stops
> him, and how the quest structure works. The premise sits in
> [THEME.md](THEME.md); this is the concrete frame that serves it.
>
> **Never surfaced as lore.** The player is told none of this directly, ever. It
> is felt through structure and voice, per THEME.md's iron rule. This doc exists
> to keep the writing coherent, not to be discovered.
>
> **Status:** frame settled tonight; specifics (what appears, the first question,
> individual roadblocks) deliberately open.

## tldr

A walk he always meant to take and never did — he died before taking it. The
afterlife *is* the walk. He sets out thinking he's nearly home; the way won't let
him finish until he really is. The surface goal is getting home (south, through
the woods). The woods have grown vast into constructed areas, which is why the
walk can't resolve. He never learns *why* he can't leave — because the answer was
never information. It's him.

## 1. Why he is here **[LOCKED]**

- **A walk he meant to take and never did.** Ordinary, small — not a vacation, a
  *walk*. The thing that would have been good for him that he never made time
  for. He died with it undone.
- **The afterlife is the walk.** He finally takes it, as the passage itself. This
  is why the place is a familiar shore-and-woods and never reads as an obvious
  afterlife — it's the kind of place he'd have gone. No cosmic transit, no
  arrival scene: he simply *is* on it.
- **Death gave him the walk.** The one thing he never made time for is now the
  only thing there is time for. Underneath the melancholy, this is a kindness —
  the joy, not the irony ([THEME.md](THEME.md)).

## 2. What he is doing **[LOCKED]**

- **Surface goal: get home.** Concrete, human, needs no plot. He thinks he's
  nearly done and heads south to finish the walk. This is what pulls the player.
- **He believes there is a way home**, and keeps trying. The drive is his, not
  the game's.
- **The real goal is underneath.** Every thread ostensibly about getting home is
  actually about a piece of him. The "way home" they lead to is a thing he comes
  to understand about himself, not a path.
- **He never finds out why he can't leave.** There is no mechanism to discover,
  only a person to become. The answer isn't hidden — it's *unfinished*. The
  ending is recognition, not revelation.

## 3. The world **[LOCKED in shape]**

- **Beach -> Woods -> the far end (south).** The macro-route is roughly linear
  and directional; south is home / where home was, grounded in the real geography
  (the shore faces north onto the water, so land and home lie south).
- **The woods have grown.** Once a small strip of trees by the road on the way to
  the shore; now vast, encapsulating the game — separate constructed areas, like
  towns with no people. This is why the walk back can't resolve and why he can't
  "remember the walk" — the known strip became an unknowable world.
- **The areas are constructs, made for him.** Rendered from memory (hiking, places
  he knew) but *implicitly*, not explicitly — it stays a plain forest and
  contains what a forest contains. It never looks or acts like an afterlife.
- **The forest is his friends, rooting for him.** The world is love wearing
  landscape. It is *for* him even when it blocks him — a block that is actually
  care ([THEME.md](THEME.md): worth demonstrated by a world that responds). Never
  stated; on the surface it is just woods and a guy grumbling at a fallen log.

## 4. The impediment — roadblocks **[LOCKED in shape]**

- **A roadblock is an EarthBound-shed:** a concrete thing he can't yet do that
  blocks progress, solved by a specific task. Authored fully, in order.
- **Comedic.** Annoying shit keeps happening and he just takes it and works out
  how to get past — no matter how ([VOICE.md](VOICE.md) §3b). The pratfall and the
  love are the same event: the annoyance IS the friends not letting him quit.
- **Several roadblocks in sequence**, each connecting one area to the next. The
  first is the seam between the beach and the first forest zone — the moment he
  turns to walk home and can't.
- **Each roadblock is an arc:** one goal, several routes (looker / maker /
  reasoner), decoupled at play time. Runs on the existing flag + `unlock_when` +
  arc-validator machinery ([Arcs.h](../../include/Arcs.h)).
- **Escalation is depth, not path.** The route through an area is open; the *next*
  threshold only passes a person who has grown through the current one. So he
  moves south gradually without a forced path — the linearity is in the depth
  required, not the road.
- **Mostly converge, occasionally branch.** Routes rejoin at the same next area by
  default (minimalism). A branch exists only when the passage itself produces
  something worth seeing the other routes shouldn't get — a pocket only certain
  pilgrims reach, never a parallel forest.

## 5. Questions ARE the quest system **[LOCKED]**

The consolidation that ties character, loop, and structure into one thing. He was
a person who asked great questions ([CHARACTER.md](CHARACTER.md)); the quests of
his afterlife literally *are* his questions.

- **A thought ends on a question.** The best ones don't conclude — they open a
  question that reframes.
- **A question is a quest.** It appears in a quest-style UI, titled by the question
  itself ("Why is no one here?" where another game would say "Retrieve the
  Amulet"). No marker, no objective arrow — a question his mind is holding, the
  Outer Wilds Ship Log / MAP-ARCHITECTURE §6 model.
- **Observations feed the open question.** Later observations answer, deepen, or
  complicate it.
- **Enough converging observations form a conclusion** — the question resolves,
  which sets a flag. That flag is what a roadblock's route gates on.
- **The full chain:** observe -> thought -> question opens (quest) -> more
  observations -> conclusion (quest closes, flag set) -> roadblock route satisfied
  -> walk south.
- **The game opens on his first question** — what he contemplates at the water
  resolves into one question, which appears in the notebook and points him at the
  first observation. That first question is effectively the game's first line.

### The notebook holds both **[LOCKED]**

His mind, externalized — one surface, not two systems (minimalism):
- **Thoughts** — what he's concluded/observed (the record; already built as a
  derived collection).
- **Questions** — the open threads, quest-style, titled by the question.

A worked question becomes a settled thought, so the two halves are one pipeline at
different stages. Note this makes the notebook hold *state* (which questions are
open, what has fed them, whether they resolved) — more than it stores today, but
its natural home, not a new system.

## Open (deliberately)

- **What appears in the opening** — something washes up / flies in / shows up. The
  humor is the interruption itself (deep contemplation, then *thunk*, and a very
  un-profound reaction). The object waits on knowing him better; it should point
  him toward leaving.
- **The first question's exact words** — his reaction to that thing, phrased as a
  question. Plain and a little funny on its face; meaningful only in hindsight.
- **Whether the areas are memories or just woods** — leaning implicit-memory.
- **Why there are no people** — between "his own solo journey" and "his desire for
  isolation," possibly the same thing.

## Cross-references

- [THEME.md](THEME.md) — the premise this frame serves.
- [CHARACTER.md](CHARACTER.md) — who he is; the writer's bible the voice draws on.
- [VOICE.md](VOICE.md) — the register; the comedy that carries the roadblocks.
- [OPENING.md](OPENING.md) — the narrator hand-off into the opening contemplation.
- [MAP-ARCHITECTURE.md](MAP-ARCHITECTURE.md) §6 — quests-as-monologue, now concrete.
- [Arcs.h](../../include/Arcs.h) — the roadblock-as-arc machinery.
