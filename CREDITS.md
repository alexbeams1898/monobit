# Credits and Attribution

This project uses third-party assets under various open licenses. Every asset
below is free to redistribute; the corresponding license text is either vendored
alongside the asset or linked out to its canonical source.

## Character Sprites -- LPC Universal Spritesheet

All player, skeleton, and cop sprites are composited at build time from the
**Liberated Pixel Cup (LPC) Universal Spritesheet** art set.

- **Project:** Universal-LPC-Spritesheet-Character-Generator
- **Upstream:** https://github.com/LiberatedPixelCup/Universal-LPC-Spritesheet-Character-Generator
- **Licenses:** GPL 3.0 / CC-BY-SA 3.0 / OGA-BY 3.0 (per-asset; see CSV)
- **Fetch script:** `game/scripts/fetch_lpc.py`
- **Build pipeline:** `fetch_lpc.py` -> `bake_palettes.py` -> `assemble_spritesheet.py`
- **Vendored license + credits files:** `game/assets/sprites/lpc/LICENSE/`
  - `LICENSE-upstream.txt` -- upstream repo's LICENSE file (GPL 3.0 copy)
  - `CREDITS.csv` -- authoritative per-file attribution from upstream, verbatim

The CSV at `game/assets/sprites/lpc/LICENSE/CREDITS.csv` is the canonical,
per-file attribution record. It is the upstream project's own authoritative
credits file and is kept in sync with every new fetch.

### Layers actually used

We only fetch a narrow subset of upstream's art. The folders below are copied
under `game/assets/sprites/lpc/raw/` and re-baked into the final per-character
sheets. Every entry is attributed in `CREDITS.csv`.

| Layer     | Upstream path                              | Notes                         |
|-----------|--------------------------------------------|-------------------------------|
| Body      | `spritesheets/body/bodies/male/`           | palette-swapped for skin tone |
| Head      | `spritesheets/head/heads/human/male*/`     | 5 face variants               |
| Eyes      | `spritesheets/eyes/human/adult/neutral/`   | 4 colors (see note below)     |
| Hair      | `spritesheets/hair/<style>/adult/`         | 17 styles, 12 colors          |
| Facial    | `spritesheets/beards/*/`                   | 12 beards + mustaches         |
| Torso     | `spritesheets/torso/clothes/<style>/`      | shortsleeve, longsleeve       |
| Legs      | `spritesheets/legs/{pants,shorts}/`        |                               |
| Feet      | `spritesheets/feet/{shoes,boots}/basic/`   | 12 colors                     |
| Headwear  | `spritesheets/hat/formal/{bowler,tophat}/` | foundation for head armor     |

**Note on eyes:** The upstream `CREDITS.csv` does not yet list the
`eyes/human/adult/neutral/` folder explicitly; the files are present in the
repo but are attributed to the base-body artists (the neutral eye is an
expression variant of the face, not a standalone asset). See the base-body
entries in the CSV for the canonical author list.

### Unique artists referenced in our subset

Aggregated from `CREDITS.csv` across every upstream folder this project uses:

- Benjamin K. Smith (BenCreating)
- Bluecarrot16
- Carlo Enrico Victoria (Nemisys)
- Durrani
- Eliza Wyatt (ElizaWy)
- Evert
- JaidynReiman
- Joe White
- Johannes Sjölund (wulax)
- kcilds / Rocetti / Eredah
- laetissima
- Manuel Riecke (MrBeast)
- Matthew Krohn (makrohn)
- MuffinElZangano
- Nila122
- Page
- Stephen Challener (Redshrike)
- Thane Brimhall (pennomi)
- thecilekli
- TheraHedwig

This list is non-exhaustive by design -- individual per-file attribution lives
in `CREDITS.csv`. Whenever `fetch_lpc.py` pulls new art, re-running
`curl -sSL -o game/assets/sprites/lpc/LICENSE/CREDITS.csv <upstream-url>` keeps
the record current.

### Palette definitions

`fetch_lpc.py` also downloads palette definition JSON files under
`game/assets/sprites/lpc/raw/_palettes/`. These come from the same upstream
repo (`palette_definitions/*/`) and are authored by the same LPC contributor
list. They drive `bake_palettes.py` at build time to generate color variants of
the grayscale master PNGs (skin tone, clothing color).

---

## Music

**Purgatory Extreme Metal Music Pack** by David KBD
- License: CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/)
- Source: https://davidkbd.itch.io/purgatory-extreme-metal-music-pack
- Tracks used: gateways, hades, purgatory, sacrifice, blood_soaked_earth,
  bone_grinders_ballad, from_the_dark_past, grave_rot_requiem, life_eternal,
  mutilations_melody, on_fire, putrid_desecration, scream, the_ritualist,
  the_slicing_strain, visceral_vengeance

## Tileset

**Dungeon Crawl Stone Soup (DCSS) Tileset**
- License: CC0 (Public Domain)
- Source: https://opengameart.org/content/dungeon-crawl-32x32-tiles
- Repository: https://github.com/crawl/crawl/tree/master/crawl-ref/source/rltiles
- Used for: `dungeon_tiles.png`

## Fonts

**Cinzel** by Natanael Gama
- License: SIL Open Font License 1.1
- Source: https://fonts.google.com/specimen/Cinzel

## Sound Effects

**Cop radio chatter** (`cop_bark_1` through `cop_bark_14`)
- Source: Freesound (ID 77121)
- Original: https://freesound.org/s/77121/
- Modified: split into segments, bitcrushed, chorus, stereo Haas effect

**Cop hit sound** (`cop_hit.ogg`)
- Source: Freesound (ID 186763)
- Original: https://freesound.org/s/186763/
- Modified: bitcrushed

**Running footsteps** (`footstep_run_1.ogg` through `footstep_run_8.ogg`)
- Source: Pixabay (ID 268478, "running on concrete")
- Original: https://pixabay.com/sound-effects/running-on-concrete-268478/
- License: Pixabay Content License (royalty-free for commercial use)
- Modified: split into segments, trimmed, re-encoded to 22050 Hz mono Ogg Vorbis

**Walking footsteps** (`footstep_walk_1.ogg` through `footstep_walk_8.ogg`)
- Source: Pixabay (ID 272246, "walking sound effect")
- Original: https://pixabay.com/sound-effects/walking-sound-effect-272246/
- License: Pixabay Content License (royalty-free for commercial use)
- Modified: split into segments, trimmed, re-encoded to 22050 Hz mono Ogg Vorbis

All other sound effects were procedurally generated for this project.

## Libraries

- **SDL2** -- zlib License -- https://www.libsdl.org/
- **EnTT** -- MIT License -- https://github.com/skypjack/entt
- **nlohmann/json** -- MIT License -- https://github.com/nlohmann/json
- **miniaudio** -- MIT-0 / Public Domain -- https://miniaud.io/
- **GLAD** -- MIT License -- https://github.com/Dav1dde/glad
- **Catch2** -- BSL-1.0 License -- https://github.com/catchorg/Catch2
- **Tracy** -- BSD 3-Clause -- https://github.com/wolfpld/tracy
- **stb_image** -- MIT / Public Domain -- https://github.com/nothings/stb
