# Game assets in Git

**tldr:** this repo's `.git` is 365MB to carry 157MB of live content, because
Git keeps every version of every binary forever and cannot compress the ones
that matter. Art assets are why game studios use Perforce. These are the numbers
from this repo, measured rather than cited.

## What it costs here

| | |
|---|---|
| `.git` on disk | **365 MB** |
| blobs reachable from HEAD | 157 MB (5,056 objects) |
| blobs ever committed | 580 MB (9,278 objects) |
| **unreachable from HEAD** | **423 MB — 73%** |

Nearly three quarters of the repository is files that no longer exist. Git
history is immutable: a deleted asset is still fetched by every clone, forever.
Most of that is a `.glb` mesh library added and then removed.

## Why compression does not help

Git deltas objects against similar ones. It works on text and fails on art, and
the reason is not that the files are binary — it is that they are **entropy
coded**. PNG, OGG, JPEG and compressed glTF are bit-packed: change one pixel and
the Huffman table shifts, so every downstream byte differs and no window matches.

Packed size against raw, measured across this repo's whole history:

| type | raw | packed | ratio |
|---|---|---|---|
| `.jpg` | 4.5 MB | 4.5 MB | **1.00x** |
| `.ogg` | 40.6 MB | 39.7 MB | **1.02x** |
| `.mp3` | 10.0 MB | 9.0 MB | 1.11x |
| `.png` | 50.9 MB | 43.3 MB | 1.17x |
| `.glb` | 266.0 MB | 218.1 MB | 1.22x |
| `.blend` | 66.6 MB | 16.4 MB | 4.06x |
| `.h` | 15.3 MB | 3.0 MB | 5.15x |
| `.cpp` | 68.9 MB | 7.4 MB | **9.36x** |
| `.ldtk` | 7.3 MB | 0.2 MB | **36.68x** |

Git is not refusing to try. It tries and gets nothing back. Source compresses
nine to one; authored art compresses not at all. `.blend` does better than the
rest because it is a container of structured data rather than a finished
entropy-coded stream.

## What Perforce does differently

Four properties, none of which Git has:

- **Server-enforced exclusive checkout.** Two people cannot both edit a `.psd`,
  because an unmergeable file that two people changed has no correct resolution.
  Git has no locking; the conflict is discovered after the fact.
- **Partial workspaces.** A client syncs a subset of the depot. Git clones
  everything reachable, because reachability is its only selector.
- **Server-side history.** Old revisions stay on the server. Clients stay thin.
- **Per-path permissions**, so an art depot and a code depot can differ.

Binaries are stored whole per revision rather than deltified — Perforce does not
pretend compression will work either. The difference is where the cost lands.

## What Git LFS does and does not fix

LFS replaces the file with a ~130-byte pointer, so clone weight goes away. It
does not fix the rest: history is still immutable, binaries still cannot merge,
storage is still file-level so every version of a `.psd` is kept whole, and
locking is a client-side convention that a plain `git push` ignores. Bandwidth
is metered by the hosting provider and charged to the repository owner.

## What this repo does instead

Nothing, so far — which is the honest answer at this size. 365MB is well inside
GitHub's limits (100MiB per file, ~1–5GB recommended per repo), and a solo
developer never hits the locking problem that drives studios to Perforce.

The mitigations available, in increasing order of disruption:

- **`-delta` attributes** on art paths, so Git stops spending repack CPU
  attempting compression that returns 1.00x.
- **Shallow and partial clone in CI.** `--depth 1` is already the default for
  `actions/checkout`. Note that `--filter=blob:none` *fails open*: without
  `uploadpack.allowFilter` on the server it prints a warning and hands back a
  full clone.
- **Git LFS**, which fixes clone weight and nothing else.
- **History rewrite** to drop the 423MB of unreachable blobs. This changes every
  downstream SHA, invalidates existing clones, and breaks the tags that releases
  point at. It is the only thing that actually reclaims the space, and the cost
  is why it has not been done.

## What this is evidence of

The storage half of the problem, felt directly. Not the locking half — one
person never collides with themselves — and not scale: 365MB is four orders of
magnitude below a AAA depot, where source content runs to terabytes per branch
and a first sync is measured in hours.

What it does demonstrate is the shape of the curve and the reason for it, which
is the part that does not change with size.
