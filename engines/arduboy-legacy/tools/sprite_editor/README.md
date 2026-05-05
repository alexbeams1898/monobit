# sprite_editor

Standalone browser-based 1-bit pixel editor for the mono engine.

Single HTML file, no dependencies, no server. Opens in any modern
browser by double-click or via `python -c "import webbrowser; webbrowser.open(...)"`.

## Features

- Pen / Erase / Fill / Line / Rect tools
- Undo / redo stack (200 deep)
- PNG import (threshold, Lanczos/Bilinear/Nearest, Floyd-Steinberg dither)
- C array import (column-major, bit-0-top and alternates)
- C array export (PROGMEM-ready, paste into sprites.cpp)
- Flip H/V, rotate, shift with optional wrap
- Live 1x/2x/4x preview panels
- Clickable hover coordinate readout

## URL-hash seed

The editor accepts initial state from the URL fragment so spritebake can
pre-populate the canvas without clicking through a file dialog:

```
index.html#w=20&h=24&name=boss_lucifer_data&layout=col-major-topbit0&import=0x00,0x01,...
```

Recognized keys:

- `w`, `h` — canvas dimensions
- `name` — value to prefill the Export-C-array name field
- `layout` — `col-major-topbit0` (default), `col-major-topbit7`,
  `row-major-msb`, `row-major-lsb`
- `import` — comma-separated byte literals (`0x..`, `0b..`, decimal)
  OR `b64:<base64>` for compact encoding

## Roundtrip with spritebake

```bash
# Print a URL that launches the editor seeded with the current bytes of a sprite
python -m tools.spritebake -m games/rpg/sprites.toml edit boss_lucifer_data
# (TODO: this subcommand not yet implemented — use manual approach below)
```

Manual approach for now:
1. Extract the bytes you want to edit from sprites.cpp (or let spritebake
   bake fresh ones).
2. Build a URL hash as above.
3. Hand-edit in the browser.
4. Click Export → Copy, paste back into sprites.cpp (or into a spritebake
   inline-import flow).

## Keyboard shortcuts

- **B** pen, **E** erase, **F** fill, **L** line, **R** rect
- **Ctrl+Z** undo, **Ctrl+Y** redo
- **Arrow keys** shift the canvas 1 pixel
- **Shift+drag** with Rect = filled
- **Alt+click** = flood fill regardless of current tool
- **Right-click drag** = paint with OFF (erase)
