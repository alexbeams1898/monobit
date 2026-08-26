# Point of Entry — credits

Third-party assets, with the licence each is used under. Anything not listed
here was made for this project.

**Sources are kept.** A recording this game cuts a sound out of lives in
`assets/audio/source/`, alongside the entry below that says where it came from.
A processed file whose original has been thrown away cannot be re-cut, and its
licence rests on nothing but this document being right -- which is how three
assets in this repo ended up with no traceable origin at all.

## Sound

**Walking footsteps** (`footstep_walk_1.ogg` … `footstep_walk_8.ogg`)
- Source: Pixabay (ID 272246, "walking sound effect")
- Original: https://pixabay.com/sound-effects/walking-sound-effect-272246/,
  kept at `assets/audio/source/walking_indoors.mp3`
- License: Pixabay Content License (royalty-free, commercial use permitted)
- Modified: cut on the footfalls by `scripts/chop_footsteps.py`, one gain across
  the set, re-encoded to 22050 Hz mono Ogg Vorbis at -3 dBFS
- Also used by `games/prison-escape-game`

**Wand spray** (`wand_spray_start.ogg`, `wand_spray_loop.ogg`,
`wand_spray_stop.ogg`, `wand_dry.ogg`)
- Source: Freesound (ID 48068, "spray"), Freesound Community
- Original: https://freesound.org/s/48068/
- Modified: one take cut into its attack, its steady body and its release with
  `scripts/slice_held_sfx.py`; the short first burst kept separately as the dry
  cough. Re-encoded to 22050 Hz mono Ogg Vorbis, peak-normalised as one set.

**Hurt** (`hurt.ogg`)
- Source: Freesound (ID 186763)
- Original: https://freesound.org/s/186763/
- Modified: bitcrushed, re-encoded to 22050 Hz mono Ogg Vorbis at -3 dBFS
- Also used by `games/prison-escape-game` (as `cop_hit.ogg`)

**Death** (`death.ogg`)
- Source: UNKNOWN -- carried over from `games/prison-escape-game`
  (`cop_death.ogg`), where it has no recorded origin either. Placeholder until
  its provenance is established or it is replaced.
- Modified: re-encoded to 22050 Hz mono Ogg Vorbis at -3 dBFS

**Impacts** (`hit_1.ogg` … `hit_4.ogg`)
- Source: synthesised for `games/prison-escape-game` by its
  `scripts/gen_placeholder_sounds.py` -- noise burst, soft-clip distortion,
  four variations cut to avoid phasing
- Modified: re-encoded to 22050 Hz mono Ogg Vorbis at -3 dBFS
