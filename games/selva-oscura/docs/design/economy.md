# Economy

> **Owns:** the sangue economy — currency, sources, sinks, accounting,
> caps, balance.
> **Status:** structural locks; balance tuning TBD.

## Currency: sangue

There is one currency: **sangue**. It is not money. It is the
substance Hell is made of (coagulated suffering — per setting.md
*Sangue and the law of substance*). Every shape in Hell — shades,
keepers, walls, gates — is composed of it. The Vagrant collects it
by extracting it from souls he kills.

There are no vendors. There is no marketplace. The economy is
**cosmological, not transactional** — sangue flows in and out of the
Vagrant by laws of substance, not by commerce.

## Sources

The Vagrant obtains sangue exclusively by **extracting it from new
shades and keepers** (per setting.md). There is no scavenging, no
quest-reward sangue, no NPC gifts of sangue.

- **Shade kills.** Per-shade sangue payouts. Pre-keeper shades
  (hypertrophic-free, indulging in the failing-state circle) give
  baseline; post-keeper shades (hypertrophic-constrained, mid-
  suffering in the restored circle) give meaningful payouts per
  the restored throughput rate.
- **Keeper kills.** Boss-tier sangue payouts at each circle's keeper
  fight. The first descent kills the *original* keeper; subsequent
  descents kill the *NPC-fused keeper* (same boss, progressive
  disfigurement — per setting.md *NPC-successor structure*). The
  fused-keeper kills also yield boss-tier payouts.
- **NPC promotion fight.** Post-keeper, the circle's NPC vanishes
  from their visible post and fuses with the keeper's role. Their
  presence on subsequent descents is *the boss fight* (above) — no
  separate NPC-kill event distinct from the keeper fight.

## Sinks

Sangue leaves the Vagrant in three ways. The cosmological law:
**sangue moves outward only** (setting.md). Once it is in the
Vagrant, it either becomes part of him (installation) or pours out
(riversamento) — it does not flow back from a living vessel of its
own accord.

- **Installation (OFFERINGS).** Class-pickers spend sangue at the
  Guide's projection (per-keeper interludes) or via the Hand
  (post-acquisition, anywhere) to invest into stats. Sangue absorbed
  is gone forever — no refunds, no respec to recover it (the Erasure
  changes class but does not return invested sangue).
- **Riversamento.** Unburdened path's defining act. Pour sangue
  out into the second-death reservoir Beatrice tends. Sites in the
  Wood pre-Hand; anywhere post-Hand (Hand functions as portable
  riversamento site).
- **The Cord.** Each Cord use costs sangue (charged at moment of
  use). Cost depends on circle and keeper-status: pre-keeper higher
  (Hell resists), post-keeper lower. Class-pickers only — unburdened
  has no Cord.

## Reclamation

Beyond voluntary sinks, sangue leaves the Vagrant on **second death**
(run-end). Per setting.md *Sangue and the law of substance, rule 2*:
**Hell reclaims its substance from the dead.** All wallet sangue
returns to Hell's reservoir on the Vagrant's death.

- Wallet sangue is *not* recoverable. No bloodstain, no corpse-run.
- Stats / lifetime sangue total / riversato lifetime total persist
  across cycles. Wallet does not.
- The Cord (voluntary retreat) preserves wallet sangue; only death
  zeroes it.

## Disgorgement — wallet returns to Hell at segment boundaries

A central refinement of how sangue moves between collection and
spending: **picking up sangue does not commit the Vagrant. Spending
does.**

Hell does not consider the substance installed in the Vagrant until
he has chosen to *use* it. A drop in the wallet is not a drop drunk.
The wallet is a transit-vessel, not a guilt. The committing act is
**consent** — the OFFERINGS purchase, the riversamento pour-out, the
Cord-charge — not the presence of sangue on his person.

**At segment boundaries, the Vagrant's unspent wallet returns to
Hell.** The mechanic is called **Disgorgement.** The wallet zeroes;
the Vagrant enters the next segment carrying nothing. Only what he
*spent* has been installed (or poured out, or paid as Cord-toll) —
and once spent, it is permanent.

**Boundary set (working candidates):** on Wood-return; on
CIRCLE_CARD entry; possibly on per-keeper interlude. Exact set TBD
at gameplay tuning. The principle: each major segment-shift flushes
the wallet so the player's relationship to sangue is
*immediate-spend or lose it*, not *stockpile across the descent*.

**Lifetime totals are unaffected.** Lifetime sangue total keeps
counting every drop ever picked up, because Hell remembers what it
offered even when the Vagrant didn't spend it. The number on
RECKONING reflects offerings, not commitments.

### How Disgorgement is felt

The first time Disgorgement fires for a save, the game shows the
ritual: a brief beat at the segment boundary where the wallet is
seen to empty. After that first reveal, Disgorgements are silent —
the wallet simply reads 0 at the next segment without ceremony. The
mechanic is taught once, then runs in the background.

This is also how the game communicates the *shape* of the unburdened
path implicitly. A player who notices that the wallet resets, and
that OFFERINGS / riversamento are the only acts that *take* from
them irreversibly, begins to intuit that there is a path defined by
*never spending at OFFERINGS*. The unburdened path is exactly that
path made cosmologically explicit (per setting.md *The Vagrant / The
unburdened path*) — but the player can deduce its shape from
watching Disgorgement before the path is ever named.

### Engineering notes

- **Wallet zero on boundary.** `sangue_vessel = 0` at each Disgorgement
  trigger. Specifics deferred to engine work.
- **First-time cinematic flag.** A single bit in `meta` (e.g.
  `meta.seen_disgorgement`) — first Disgorgement sets the bit and
  triggers the ceremony; subsequent ones check the bit and skip.
- **Lifetime counter unaffected.** `meta.total_sangue_earned`
  continues to accumulate on pickup, not on spend.

## Accounting structure

Two distinct sangue counters:

- **Wallet sangue** — current run's collected sangue. Resets on
  death. The number on the HUD.
- **Lifetime sangue total** — running total of all sangue ever
  collected. Persists across cycles. Used for some state-aware NPC
  dialogue branching (per setting.md *NPC dialogue is state-aware*).
  Bounded at **3,999,999** (the renderer's vinculum-Roman cap, per
  setting.md *Hell's accounting cap*).
- **Riversato lifetime total** — for unburdened, the running total
  of sangue poured out. Persists across cycles. State-aware NPC
  dialogue branches on this.

Lifetime totals are *display* values — they don't unlock anything
mechanically. They feed dialogue state-awareness and the player's
sense of progress.

## Pressure mechanics

Hell does not let the Vagrant hoard sangue (setting.md). The vessel
is a wound that holds substance briefly. Pressure is implemented in
three ways:

- **Overflow loss.** Wallet has a soft cap; sangue collected past it
  is lost. Cap value TBD; intent: the player feels pressure to spend
  before they can collect more.
- **Scaling investment cost.** Stat investments cost more as stats
  rise. Late-game investments require more sangue per increment than
  early ones. Curve TBD at tuning.
- **Lifetime cap (UX-only).** Lifetime sangue counter bounded at
  3,999,999 (renderer cap). The game guides the player to spend
  sangue as they approach this. The cap is a UX concern, not an
  ending.

The principle: **sangue at rest is sangue Hell is jealous of.**
Holding wallet sangue produces drift loss; investing or pouring out
preserves what the Vagrant has gained.

## Path-specific economic shape

| Aspect | Class-picker | Unburdened |
|---|---|---|
| Stats unlocked | Yes | **No** (locked at 0/0/0) |
| OFFERINGS available | Yes (Guide / Hand) | No (cannot install sangue) |
| Riversamento | No | Yes (the defining act) |
| Cord | Yes | **No** |
| Wallet pressure | Spend at OFFERINGS | Pour at riversamento sites / Hand |

The unburdened economy is stranger by design. Sangue accumulates with
**no obvious purpose** at first — riversamento mechanics emerge
gradually through state-aware NPC dialogue, environmental sites, the
Guide, and Grimoire entries (per setting.md *The unburdened path*).

## Cosmological framing (the economy is the cosmology)

The sangue economy is not separate from the cosmology — it *is* the
cosmology, expressed mechanically. Every economic act is a
cosmological act:

- **Killing a shade** is granting second death (per *Forced
  repentance through second death*). The sangue payout is the
  resolved soul's substance returning to circulation.
- **Investing sangue at OFFERINGS** is Hell installing more of itself
  into the class-picker. The Vagrant's stats rise because Hell is
  loading itself in.
- **Pouring sangue (riversamento)** is the unburdened making his own
  form in Beatrice's frame, building toward PURITY's terminus.
- **Losing sangue on death** is Hell trying its protocol on the
  unjudged and reclaiming its substance even though the resolution
  doesn't land.

The economy is therefore *not* a balance-curve abstraction layered
on top of the world. It is the world's substance flowing through the
Vagrant in patterns the cosmology dictates.

## Open questions

- **Wallet cap value.** Specific number; TBD at tuning.
- **Per-shade / per-keeper / per-NPC sangue payout values.** TBD at
  tuning; constrained by intended pacing of investment opportunities.
- **OFFERINGS investment curve.** Specific cost-per-stat-tier; TBD.
- **Cord cost curve.** Specific sangue cost per circle / keeper-
  status; TBD.
- **Drift-loss rate** (overflow / per-time decay if any). TBD;
  intent is *Hell does not let the Vagrant hoard*, mechanism TBD.
- **Disgorgement boundary set.** Which segment-shifts trigger wallet-
  zero. Working candidates: Wood-return, CIRCLE_CARD entry, possibly
  per-keeper interlude. TBD at tuning.
- **Disgorgement first-time cinematic** — single beat, frame-by-frame
  TBD.
- **NPC reward economics.** Whether NPCs grant sangue (vs. items /
  unlocks / lore) on first encounter. Per setting.md NPCs provide a
  benefit on first encounter; the form of that benefit per NPC is
  open.

## Cross-references

- [Setting](setting.md) — *Sangue and the law of substance*, *Hell's
  accounting cap*, *Forced repentance through second death*,
  *Per-circle reactivity*.
- [Inventory](inventory.md) — items that interact with sangue (Cord,
  Hand, Erasure).
- [Classes](classes.md) — stat investment via OFFERINGS.
- [Story](story.md) — the unburdened path's discovery (riversamento
  mechanics emerge gradually, not exposed at start).
- [Fallback](fallback.md) — wallet returns to Hell on second death;
  vestigia preserve persistent state including lifetime sangue total.
