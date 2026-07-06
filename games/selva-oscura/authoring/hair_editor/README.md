# Hair editor -- authoring templates

These Blender geometry-nodes template files author new hair styles
from scratch. Output flows into the same mhclo attach pipeline as the
pre-packaged hair styles under `assets/characters/hair/`, so anything
baked from here plugs into every actor bake with zero pipeline change.

## Files

- `hair.blend` -- polygonal / card-strip hair templates driven by
  geometry nodes
- `fur.blend` -- fur / short-hair templates driven by geometry nodes

## When to use vs pre-packaged styles

Prefer a pre-packaged style from `assets/characters/hair/cc0/` or
`assets/characters/hair/ccby/` first -- 56 styles cover most needs.
Use these templates when:

- No pre-packaged style fits a specific character (Vagrant-appropriate
  cut, keeper hair, boss-specific look)
- A style needs custom tuning (length, part, volume) that morphs alone
  can't reach

## Output the pipeline expects

Anything a bake produces must land in a per-style directory shaped
like the existing entries:

```
assets/characters/hair/<license>/<style_id>/
  <style_id>.mhclo             # attachment weights against the humanoid basemesh
  <style_id>.mhmat             # material file (references the diffuse)
  <style_id>.obj               # mesh
  <style_id>_diffuse.png       # albedo texture (2048x2048 typical)
```

Once the four files are present, the style becomes selectable at bake
time via `gen_humanoid.py --hair-mhclo assets/characters/hair/<license>/<style_id>/<style_id>.mhclo`.

## Credits

Templates by Tomáš Klecer, MakeHuman Community, CC0. See top-level
`CREDITS.md` under "Hair authoring templates."
