# Wayworn Hush — Credits & Asset Attribution

Third-party assets used in Wayworn Hush, with required attribution.

## Player character sprite (LPC)

The protagonist sprite is built from **Liberated Pixel Cup (LPC)** assets via the
Universal LPC Spritesheet Generator. LPC assets are released under a combination
of **CC-BY-SA 3.0, OGA-BY 3.0, and GPL 3.0** — attribution is required.

The full per-layer attribution (every author, license, and source link for each
sprite layer used) is preserved verbatim in
[`assets/sprites/lpc/CREDITS.txt`](assets/sprites/lpc/CREDITS.txt). The layers
used and their contributing artists:

- **Body / base (Thick Male, Run/Climb):** bluecarrot16, JaidynReiman,
  Benjamin K. Smith (BenCreating), Evert, Eliza Wyatt (ElizaWy), TheraHedwig,
  MuffinElZangano, Durrani, Johannes Sjölund (wulax), Stephen Challener
  (Redshrike)
- **Head:** bluecarrot16, Benjamin K. Smith (BenCreating), Stephen Challener
  (Redshrike)
- **Face / expression:** JaidynReiman, ElizaWy, Stephen Challener (Redshrike)
- **Hair (jewfro):** JaidynReiman
- **Torso (short-sleeve t-shirt):** ElizaWy, JaidynReiman, Stephen Challener
  (Redshrike), Johannes Sjölund (wulax)
- **Legs (pants):** JaidynReiman, ElizaWy, Bluecarrot16, Johannes Sjölund
  (wulax), Stephen Challener (Redshrike)
- **Feet (sandals):** Nila122, JaidynReiman, Matthew Krohn (makrohn), Johannes
  Sjölund (wulax)

Primary sources:
- Universal LPC Spritesheet Generator —
  https://github.com/liberatedpixelcup/Universal-LPC-Spritesheet-Character-Generator
- LPC base assets —
  https://opengameart.org/content/liberated-pixel-cup-lpc-base-assets-sprites-map-tiles
- LPC character bases — https://opengameart.org/content/lpc-character-bases

**License compliance note:** because LPC assets include CC-BY-SA 3.0 and GPL 3.0
material, any distribution of the game's LPC-derived sprites must carry this
attribution and honor the share-alike terms of those licenses. Keep
`assets/sprites/lpc/CREDITS.txt` shipped with any build that includes the
character sprite.

## World tilesets

Overworld terrain + object tilesets are derived from **ArMM1998's "Zelda-like
tilesets and sprites,"** released under **CC0** (public domain — no attribution
required; credited here as good practice). The source sheets are kept unmodified
in [`assets/tilesets/source/`](assets/tilesets/source/); the shipped tilesets are
those sheets scaled ×2 to 32px and color-snapped by
[`tools/tileset/slice.py`](tools/tileset/slice.py) (see
[docs/design/MAP-PIPELINE.md](docs/design/MAP-PIPELINE.md)).

- Source: https://opengameart.org/content/zelda-like-tilesets-and-sprites
- Author: ArMM1998 (Armando Montero)
- License: CC0 1.0 Universal (public domain dedication)

## Music

Ambient score composed by Kira Stream (see `assets/audio/`).

## Ambient sound effects

The bedroom's morning sounds are processed from free Pixabay uploads (Pixabay
Content License — free use, no attribution required; credited here as good
practice). The unmodified downloads are kept in
[`assets/audio/source/`](assets/audio/source/); the shipped `.ogg` files are
trimmed, peak-normalized to −1 dBFS, and converted to 22050 Hz mono.

- **alarm_loop.ogg** — "Alarm Clock" by freesound_community
- **birds.ogg** — "Morning Song Birds" by freesound_community
- **tv_murmur.ogg / tv_glitch.ogg** — "TV Static" + "TV Glitch" by freesound_community
- **tv_off.ogg** — "TV Shutdown" by Dragon-Studio

The door enter/exit transitions and the observe/notebook UI tones are original
synthesized sounds, made for this game.

## Interior furniture (Sierrassets)

Household furniture and electronics (the TV and other interior props), plus the
cat (composited into `assets/sprites/cat_walk.png` from the pack's pets sheet),
come from **sierrassets' "Pixel Art Furniture Pack"** (name-your-own-price,
itch.io). The
license (kept beside the sheets in
[`assets/tilesets/source/sierrassets/LICENSE.txt`](assets/tilesets/source/sierrassets/LICENSE.txt))
grants perpetual commercial use and derivative works in shipped games; the raw
pack may not be redistributed outside the game, so it lives only in this private
source tree.

- Source: https://sierrassets.itch.io/pixel-art-furniture-pack
- Author: sierrassets
- License: Sierrassets custom license (commercial use + derivatives OK; no
  redistribution of the assets themselves)

Only the pack sheets actually drawn from are kept here; the shipped tilesets are
those sheets repacked onto the 16px authoring grid by
[`tools/tileset/repack.py`](tools/tileset/repack.py), which reads the pack's
per-sprite slice exports. Those exports are a local input (re-download the pack
to regenerate), not repo content.
