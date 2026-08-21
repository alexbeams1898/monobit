# Point of Entry — credits

Third-party assets, with the licence each is used under. Anything not listed
here was made for this project.

## Sound

**Walking footsteps** (`footstep_walk_1.ogg` … `footstep_walk_8.ogg`)
- Source: Pixabay (ID 272246, "walking sound effect")
- Original: https://pixabay.com/sound-effects/walking-sound-effect-272246/
- License: Pixabay Content License (royalty-free, commercial use permitted)
- Modified: split into segments, trimmed, re-encoded to 22050 Hz mono Ogg Vorbis
- Also used by `games/prison-escape-game`

**Wand spray** (`wand_spray_start.ogg`, `wand_spray_loop.ogg`,
`wand_spray_stop.ogg`, `wand_dry.ogg`)
- Source: Freesound (ID 48068, "spray"), Freesound Community
- Original: https://freesound.org/s/48068/
- Modified: one take cut into its attack, its steady body and its release with
  `scripts/slice_held_sfx.py`; the short first burst kept separately as the dry
  cough. Re-encoded to 22050 Hz mono Ogg Vorbis, peak-normalised as one set.
