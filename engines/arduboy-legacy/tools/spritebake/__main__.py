"""spritebake CLI — run as `python -m tools.spritebake <command>`.

Commands:
  bake        Rebuild all sprites listed in a manifest and patch the .cpp.
  preview     Render a matrix PNG of selected sprites across one or more
              pipelines, without touching the .cpp.
  diff        Show byte-level diff between committed .cpp sprites and
              freshly-baked ones (dry-run of `bake`).
  list-stages List every registered pipeline stage with its one-line doc.
  roundtrip   Decode an existing NAME_data block and encode again; verify
              the round-trip produces identical bytes.

All commands take `-m/--manifest <path>` to pick the TOML file. If
omitted, walks up from cwd looking for `sprites.toml`.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Windows consoles default to cp1252 which chokes on em-dashes in our
# docstrings. Force UTF-8 on stdout/stderr before anything prints.
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass

from . import config, patch
from . import stages  # register every stage
from .core import SpriteData
from .loader import load_image, load_image_bbox, load_image_cell
from .pipeline import Pipeline, list_stages as _list_stages
from .encode import encode


REPO_ROOT = Path(__file__).resolve().parents[2]


def _find_manifest(explicit: Path | None) -> Path:
    if explicit:
        return explicit.resolve()
    # Walk up from CWD
    cur = Path.cwd().resolve()
    while True:
        candidate = cur / "sprites.toml"
        if candidate.exists():
            return candidate
        # Also check games/*/sprites.toml anywhere under the first ancestor with a .git
        if (cur / ".git").exists():
            hits = list(cur.rglob("sprites.toml"))
            # Exclude anything inside node_modules/dist/build dirs.
            hits = [h for h in hits if "node_modules" not in h.parts and "build" not in h.parts]
            if len(hits) == 1:
                return hits[0]
            if len(hits) > 1:
                raise SystemExit(
                    f"multiple sprites.toml found; specify with -m:\n  " +
                    "\n  ".join(str(h.relative_to(cur)) for h in hits)
                )
            break
        parent = cur.parent
        if parent == cur:
            break
        cur = parent
    raise SystemExit("no sprites.toml found; pass -m / --manifest <path>")


def _load_sprite_source(spec: config.SpriteSpec) -> SpriteData:
    if spec.bbox is not None:
        b = spec.bbox
        return load_image_bbox(
            spec.source,
            x0=b.x0, y0=b.y0, x1=b.x1, y1=b.y1,
            portrait_ratio=b.portrait_ratio,
        )
    if spec.slice is not None:
        s = spec.slice
        return load_image_cell(
            spec.source,
            cols=s.cols, rows=s.rows, index=s.index,
            trim_top_ratio=s.trim_top_ratio,
            trim_bottom_ratio=s.trim_bottom_ratio,
            trim_left_ratio=s.trim_left_ratio,
            trim_right_ratio=s.trim_right_ratio,
            autocrop_to_figure=s.autocrop_to_figure,
            autocrop_threshold=s.autocrop_threshold,
            portrait_ratio=s.portrait_ratio,
        )
    return load_image(spec.source, portrait_ratio=spec.portrait_ratio)


def _touchup_override(ident: str, width: int, height: int) -> SpriteData | None:
    """If art/touchups/<ident>.png exists, decode it as the bake output.
    Returns None when no override is present. The PNG must match the
    declared width/height; loaded as 1-bit (>=128 = lit)."""
    import numpy as np
    from PIL import Image as PILImage
    path = REPO_ROOT / "art" / "touchups" / f"{ident}.png"
    if not path.exists():
        return None
    img = PILImage.open(path).convert("L")
    if img.size != (width, height):
        raise RuntimeError(
            f"touchup {path.relative_to(REPO_ROOT).as_posix()} is {img.size[0]}x{img.size[1]}, "
            f"sprite declares {width}x{height}"
        )
    arr = np.array(img) >= 128
    return SpriteData(pixels=arr, meta={"touchup": str(path)})


def _bake_manifest(m: config.Manifest) -> dict[str, SpriteData]:
    """Run every sprite through its pipeline, return mapping ident -> SpriteData.
    If `art/touchups/<ident>.png` exists, it overrides the pipeline output."""
    baked: dict[str, SpriteData] = {}
    for spec in m.sprites:
        override = _touchup_override(spec.ident, spec.width, spec.height)
        if override is not None:
            baked[spec.ident] = override
            continue
        source = _load_sprite_source(spec)
        pipeline = spec.resolve_pipeline(m.pipelines)
        result = pipeline.run(source)
        # Defensive: ensure resulting sprite matches declared width/height.
        if result.width != spec.width or result.height != spec.height:
            raise RuntimeError(
                f"sprite {spec.ident!r}: pipeline produced {result.width}x{result.height}, "
                f"manifest declares {spec.width}x{spec.height}. Add a downscale stage."
            )
        baked[spec.ident] = result
    return baked


def cmd_bake(args: argparse.Namespace) -> int:
    m = config.load_manifest(_find_manifest(args.manifest), repo_root=REPO_ROOT)
    print(f"Manifest: {m.manifest_path}")
    print(f"Output:   {m.output.cpp}  (layout={m.output.layout}, progmem={m.output.progmem})")
    print(f"Sprites:  {len(m.sprites)}")
    baked = _bake_manifest(m)
    results = patch.patch_sprites_cpp(
        m.output.cpp,
        baked,
        layout=m.output.layout,
        progmem=m.output.progmem,
        dry_run=args.dry_run,
    )
    ok = sum(1 for r in results if r.status == "replaced")
    missing = [r.name for r in results if r.status == "not_found"]
    ambig = [r.name for r in results if r.status == "ambiguous"]
    print(f"\nReplaced {ok}/{len(results)} sprite arrays.")
    if missing:
        print(f"  NOT FOUND ({len(missing)}): {', '.join(missing)}")
    if ambig:
        print(f"  AMBIGUOUS ({len(ambig)}): {', '.join(ambig)}")
    if not args.dry_run:
        # Also write FX-bound sprite bytes to data/fx/<subdir>/<ident>.bin
        # and rebuild build/fxdata/data.bin + data_offsets.h. Keeps the
        # FX image in sync after a manifest-side dim/bbox change. The
        # interactive touchup/animate servers already do this on save;
        # bake needs it too so a one-shot `make` flow doesn't leave the
        # FX image stale.
        fx_status = _refresh_fx_data(m, baked)
        print(f"FX refresh: {fx_status}")
    if args.dry_run:
        print("(dry-run — no file changes written.)")
    return 0 if not (missing or ambig) else 1


def cmd_preview(args: argparse.Namespace) -> int:
    from .preview import render_matrix
    m = config.load_manifest(_find_manifest(args.manifest), repo_root=REPO_ROOT)
    out_path = Path(args.output) if args.output else (REPO_ROOT / "art" / "_spritebake_preview.png")
    out_path.parent.mkdir(parents=True, exist_ok=True)

    filter_names = set(args.sprite or [])
    sprites_to_bake = [s for s in m.sprites
                       if not filter_names or s.ident in filter_names or s.ident.removesuffix("_data") in filter_names]
    if not sprites_to_bake:
        print("no sprites match filter", file=sys.stderr)
        return 1

    # Build rows: either the single default pipeline, or compare multiple.
    pipeline_names = args.pipeline or []
    if not pipeline_names:
        pipeline_names = [None]  # None = use each sprite's own pipeline

    rows: list[tuple[str, list[SpriteData]]] = []
    col_names = [s.ident.removesuffix("_data") for s in sprites_to_bake]
    for pname in pipeline_names:
        row_sprites: list[SpriteData] = []
        for spec in sprites_to_bake:
            # Touchup override only applies when running the sprite's own
            # pipeline (pname is None). Comparing alternative pipelines
            # always re-runs the chain so the override doesn't muddy the diff.
            if pname is None:
                tu = _touchup_override(spec.ident, spec.width, spec.height)
                if tu is not None:
                    row_sprites.append(tu)
                    continue
            source = _load_sprite_source(spec)
            if pname is None:
                pipeline = spec.resolve_pipeline(m.pipelines)
            else:
                if pname not in m.pipelines:
                    raise SystemExit(f"unknown pipeline {pname!r}")
                # temporary SpriteSpec override
                override = config.SpriteSpec(
                    ident=spec.ident, source=spec.source, width=spec.width,
                    height=spec.height, pipeline=pname, slice=spec.slice,
                )
                pipeline = override.resolve_pipeline(m.pipelines)
            row_sprites.append(pipeline.run(source))
        rows.append((pname or "default", row_sprites))

    img = render_matrix(rows, col_names, scale=args.scale)
    img.save(out_path)
    print(f"wrote {out_path}")
    if args.open:
        import webbrowser
        webbrowser.open(out_path.as_uri())
    return 0


def cmd_diff(args: argparse.Namespace) -> int:
    m = config.load_manifest(_find_manifest(args.manifest), repo_root=REPO_ROOT)
    baked = _bake_manifest(m)
    results = patch.patch_sprites_cpp(
        m.output.cpp,
        baked,
        layout=m.output.layout,
        progmem=m.output.progmem,
        dry_run=True,
    )
    changed = 0
    for r, spec in zip(results, m.sprites):
        if r.status != "replaced":
            print(f"  [{r.status:9s}] {r.name}")
            continue
        # Compare byte-by-byte
        from .patch import read_existing_sprite
        current = read_existing_sprite(m.output.cpp, r.name, spec.width, spec.height, m.output.layout)
        new = baked[r.name]
        if current is not None and (current.pixels == new.pixels).all():
            print(f"  [same     ] {r.name}")
        else:
            print(f"  [DIFFERENT] {r.name} ({r.old_len} -> {r.new_len} bytes)")
            changed += 1
    print(f"\n{changed} sprites would change.")
    return 0


def cmd_list_stages(args: argparse.Namespace) -> int:
    print("Registered pipeline stages:")
    for name, doc in _list_stages():
        print(f"  {name:34s}  {doc}")
    return 0


def cmd_roundtrip(args: argparse.Namespace) -> int:
    m = config.load_manifest(_find_manifest(args.manifest), repo_root=REPO_ROOT)
    from .patch import read_existing_sprite
    mismatches = 0
    for spec in m.sprites:
        existing = read_existing_sprite(m.output.cpp, spec.ident, spec.width, spec.height, m.output.layout)
        if existing is None:
            print(f"  [missing  ] {spec.ident}")
            continue
        re = encode(existing, m.output.layout)
        from .encode import decode
        re_sprite = decode(re, spec.width, spec.height, m.output.layout)
        if (re_sprite.pixels == existing.pixels).all():
            print(f"  [ok       ] {spec.ident} ({len(re)} bytes)")
        else:
            print(f"  [MISMATCH ] {spec.ident}")
            mismatches += 1
    return 1 if mismatches else 0


def cmd_touchup(args: argparse.Namespace) -> int:
    """Open the sprite_editor in a browser seeded with this sprite's
    current baked bytes, and run a tiny HTTP helper that lets the editor
    save edits to art/touchups/<ident>.png and re-render previews."""
    import base64
    import http.server
    import json
    import socket
    import socketserver
    import threading
    import urllib.parse
    import webbrowser

    m_path = _find_manifest(args.manifest)
    m = config.load_manifest(m_path, repo_root=REPO_ROOT)
    spec = next((s for s in m.sprites if s.ident == args.ident or s.ident.removesuffix("_data") == args.ident), None)
    if spec is None:
        raise SystemExit(f"unknown sprite ident: {args.ident!r}")

    # Seed bytes are computed FRESHLY on every GET /seed.bin request
    # (see Handler below) so a save → page-refresh cycle picks up the
    # touchup the user just saved instead of the bytes captured at
    # server-start time. The initial b64-in-URL fallback below is only
    # used if the editor's /seed.bin fetch fails.
    def compute_seed_bytes() -> list[int]:
        override = _touchup_override(spec.ident, spec.width, spec.height)
        if override is not None:
            sd = override
        else:
            source = _load_sprite_source(spec)
            pipeline = spec.resolve_pipeline(m.pipelines)
            sd = pipeline.run(source)
        return encode(sd, m.output.layout)

    # Initial b64 (fallback only — primary path is /seed.bin):
    bytes_list = compute_seed_bytes()
    b64 = base64.b64encode(bytes(bytes_list)).decode("ascii")

    touchup_dir = REPO_ROOT / "art" / "touchups"
    touchup_dir.mkdir(parents=True, exist_ok=True)
    editor_path = REPO_ROOT / "tools" / "sprite_editor" / "index.html"
    # Re-read the editor HTML on each GET so iteration on editor changes
    # is one Ctrl+R away — no need to restart the server. The HTML is a
    # few KB so this is free.

    # Optional tracing layer: load the reference image (if any) once,
    # serve it as PNG bytes from /trace.png. Editor renders it as a
    # translucent underlay so the user can hand-trace 1-bit pixels.
    trace_png_bytes: bytes | None = None
    trace_path: Path | None = getattr(args, "trace", None)
    if trace_path is not None:
        from PIL import Image
        from io import BytesIO
        if not trace_path.is_absolute():
            trace_path = (REPO_ROOT / trace_path).resolve()
        if not trace_path.is_file():
            raise SystemExit(f"--trace path not found: {trace_path}")
        trace_img = Image.open(trace_path).convert("L")
        buf = BytesIO()
        trace_img.save(buf, format="PNG")
        trace_png_bytes = buf.getvalue()

    # Pick a free port
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a, **kw):
            pass  # silence

        def do_GET(self):
            url = urllib.parse.urlparse(self.path)
            if url.path == "/" or url.path == "/index.html":
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(editor_path.read_bytes())
                return
            if url.path == "/trace.png" and trace_png_bytes is not None:
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(trace_png_bytes)
                return
            if url.path == "/seed.bin":
                # Re-compute on every request so a save → reload cycle
                # picks up the latest touchup.
                fresh = bytes(compute_seed_bytes())
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(fresh)
                return
            if url.path == "/preview":
                # Re-bake all player_* sprites, honoring touchup overrides.
                from .preview import render_matrix
                from io import BytesIO
                m2 = config.load_manifest(m_path, repo_root=REPO_ROOT)
                cols, row = [], []
                for s2 in m2.sprites:
                    if not s2.ident.startswith("player_"):
                        continue
                    tu = _touchup_override(s2.ident, s2.width, s2.height)
                    if tu is not None:
                        row.append(tu)
                    else:
                        src = _load_sprite_source(s2)
                        pl = s2.resolve_pipeline(m2.pipelines)
                        row.append(pl.run(src))
                    cols.append(s2.ident.removesuffix("_data"))
                img = render_matrix([("touchup", row)], cols, scale=6)
                buf = BytesIO()
                img.save(buf, format="PNG")
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(buf.getvalue())
                return
            self.send_response(404); self.end_headers()

        def do_POST(self):
            url = urllib.parse.urlparse(self.path)
            if url.path == "/save":
                qs = urllib.parse.parse_qs(url.query)
                ident = qs.get("ident", [""])[0]
                if not ident:
                    self.send_response(400); self.end_headers(); self.wfile.write(b"missing ident"); return
                length = int(self.headers.get("Content-Length", "0"))
                data = self.rfile.read(length)
                target = touchup_dir / f"{ident}.png"
                target.write_bytes(data)
                rel = target.relative_to(REPO_ROOT).as_posix()
                # Auto-bake so the touched-up bytes flow into sprites.cpp
                # right away. Saves a manual `spritebake bake` step.
                # Also refreshes FX data.bin so SDL/Arduboy don't render
                # stale bytes after a touchup of an FX-bound sprite.
                bake_status = "skipped"
                fx_status = ""
                try:
                    m_reload = config.load_manifest(m_path, repo_root=REPO_ROOT)
                    baked_map = _bake_manifest(m_reload)
                    results = patch.patch_sprites_cpp(
                        m_reload.output.cpp,
                        baked_map,
                        layout=m_reload.output.layout,
                        progmem=m_reload.output.progmem,
                        dry_run=False,
                    )
                    ok = sum(1 for r in results if r.status == "replaced")
                    bake_status = f"baked {ok}/{len(results)}"
                    fx_status = " | " + _refresh_fx_data(m_reload, baked_map)
                except Exception as e:
                    bake_status = f"bake failed: {e}"
                print(f"[touchup/save] {rel} | {bake_status}{fx_status}")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"path": rel, "bake": bake_status}).encode("utf-8"))
                return
            self.send_response(404); self.end_headers()

    httpd = socketserver.TCPServer(("127.0.0.1", port), Handler)
    th = threading.Thread(target=httpd.serve_forever, daemon=True)
    th.start()

    url = (
        f"http://127.0.0.1:{port}/#"
        f"w={spec.width}&h={spec.height}"
        f"&name={spec.ident}"
        f"&layout={m.output.layout}"
        f"&import=b64:{urllib.parse.quote(b64)}"
        f"&touchup={spec.ident}"
    )
    if trace_png_bytes is not None:
        url += "&trace=/trace.png"
    print(f"Touchup helper listening at http://127.0.0.1:{port}")
    print(f"Editing: {spec.ident} ({spec.width}x{spec.height})")
    print(f"Saves go to: {touchup_dir.relative_to(REPO_ROOT).as_posix()}/{spec.ident}.png")
    print(f"Press Ctrl+C to stop the helper.")
    webbrowser.open(url)
    try:
        th.join()
    except KeyboardInterrupt:
        httpd.shutdown()
    return 0


# Sprite idents that live on FX flash. After any of these changes
# (touchup or animation save), the animate/touchup auto-bake step
# regenerates the FX bin files and runs the FX builder so data.bin
# stays in sync. As the migration scales (portraits, more class anims),
# this map grows; eventually it'll be a manifest field on each sprite.
#
# Maps ident -> output subdir under data/fx/. Lets us route bosses
# vs anim frames into different folders for organization.
_FX_BOUND_IDENTS = {
    "pen_l1_idle_f0_data": "anim",
    "pen_l1_idle_f1_data": "anim",
    "pen_l1_walk_f0_data": "anim",
    "pen_l1_walk_f1_data": "anim",
    "pen_l1_walk_f2_data": "anim",
    "pen_l1_walk_f3_data": "anim",
    "pen_l1_attack_f0_data": "anim",
    "pen_l1_attack_f1_data": "anim",
    "boss_charon_data": "bosses",
    "boss_minos_data": "bosses",
    "boss_cerberus_data": "bosses",
    "boss_plutus_data": "bosses",
    "boss_phlegyas_data": "bosses",
    "boss_medusa_data": "bosses",
    "boss_minotaur_data": "bosses",
    "boss_geryon_data": "bosses",
    "boss_lucifer_data": "bosses",
    # Player class portraits + world tokens (3 classes × 3 levels × 2 sizes).
    # Migrated to FX 2026-04-26 — total ~2 KB of bytes that don't fit
    # PROGMEM headroom. Penitent L1 in-world is handled by the existing
    # pen_l1_idle_f0_data anim entry (the canonical touched-up L1 sprite),
    # so player_penitent_lvl1_world_data is omitted.
    "player_penitent_lvl1_data": "player",
    "player_penitent_lvl2_data": "player",
    "player_penitent_lvl2_world_data": "player",
    "player_penitent_lvl3_data": "player",
    "player_penitent_lvl3_world_data": "player",
    "player_wretched_lvl1_data": "player",
    "player_wretched_lvl1_world_data": "player",
    "player_wretched_lvl2_data": "player",
    "player_wretched_lvl2_world_data": "player",
    "player_wretched_lvl3_data": "player",
    "player_wretched_lvl3_world_data": "player",
    "player_heretic_lvl1_data": "player",
    "player_heretic_lvl1_world_data": "player",
    "player_heretic_lvl2_data": "player",
    "player_heretic_lvl2_world_data": "player",
    "player_heretic_lvl3_data": "player",
    "player_heretic_lvl3_world_data": "player",
    # Pre-class "unburdened" Pilgrim — Hell's placeholder before
    # class-select (BURDEN = 0). Lives on FX with the rest of the
    # player portraits; bake is a starting point, hand-touched in
    # art/touchups/.
    "player_unburdened_data": "player",
    "player_unburdened_world_data": "player",
    "ub_idle_f0_data": "player",
    "ub_idle_f1_data": "player",
    "ub_walk_f0_data": "player",
    "ub_walk_f1_data": "player",
    "ub_walk_f2_data": "player",
    "ub_walk_f3_data": "player",
    "ub_attack_f0_data": "player",
    "ub_attack_f1_data": "player",
    # Fallen halo — sigil rendered during scene-paging cover transitions.
    # f0 = held sigil (dither target + SPM freeze); f1..f15 = post-SPM
    # exit animation played at 12 fps. 128×64 = 1024 B per frame, 16 KB
    # total. See docs/design/ "Fallen Halo".
    "fh_idle_f0_data":  "sigil",
    "fh_idle_f1_data":  "sigil",
    "fh_idle_f2_data":  "sigil",
    "fh_idle_f3_data":  "sigil",
    "fh_idle_f4_data":  "sigil",
    "fh_idle_f5_data":  "sigil",
    "fh_idle_f6_data":  "sigil",
    "fh_idle_f7_data":  "sigil",
    "fh_idle_f8_data":  "sigil",
    "fh_idle_f9_data":  "sigil",
    "fh_idle_f10_data": "sigil",
    "fh_idle_f11_data": "sigil",
    "fh_idle_f12_data": "sigil",
    "fh_idle_f13_data": "sigil",
    "fh_idle_f14_data": "sigil",
    "fh_idle_f15_data": "sigil",
    # Beatrice — title-screen portrait + crying transition. f0 is the
    # static portrait shown on the title screen (under the LOGO_TITLE +
    # PRESS A overlays); f1..f15 are the post-A-press exit animation
    # played at 12 fps in place of the sigil cover. 58×58 = 464 B per
    # frame, 16 frames = ~7.4 KB total on FX. See docs/design/ "Beatrice".
    "beatrice_data":     "title",
    "bea_idle_f1_data":  "title",
    "bea_idle_f2_data":  "title",
    "bea_idle_f3_data":  "title",
    "bea_idle_f4_data":  "title",
    "bea_idle_f5_data":  "title",
    "bea_idle_f6_data":  "title",
    "bea_idle_f7_data":  "title",
    "bea_idle_f8_data":  "title",
    "bea_idle_f9_data":  "title",
    "bea_idle_f10_data": "title",
    "bea_idle_f11_data": "title",
    "bea_idle_f12_data": "title",
    "bea_idle_f13_data": "title",
    "bea_idle_f14_data": "title",
    "bea_idle_f15_data": "title",
    "bea_idle_f16_data": "title",
    "bea_idle_f17_data": "title",
    "bea_idle_f18_data": "title",
    "bea_idle_f19_data": "title",
    "bea_idle_f20_data": "title",
    "bea_idle_f21_data": "title",
    "bea_idle_f22_data": "title",
    "bea_idle_f23_data": "title",
    "bea_idle_f24_data": "title",
    "bea_idle_f25_data": "title",
    "bea_idle_f26_data": "title",
    "bea_idle_f27_data": "title",
    "bea_idle_f28_data": "title",
    "bea_idle_f29_data": "title",
    "bea_idle_f30_data": "title",
    "bea_idle_f31_data": "title",
}


def _prune_stale_frames(m_path: Path, ident: str, state: str, new_count: int) -> str:
    """Drop sprites.toml [[sprites]] blocks AND fxdata/manifest.txt
    lines whose slice index is >= new_count for the animation
    art/animations/<ident>/<state>.png. Also unlinks the corresponding
    data/fx/<subdir>/<frame_ident>.bin files. Returns one-line status.

    Called from the editor save handler after a strip shrinks. Without
    this, stale per-frame entries linger in both files and produce
    out-of-range slice indices on the next bake (which silently turn
    into past-EOF zero-pixel "frames" or break the build).
    """
    import re
    text = m_path.read_text(encoding="utf-8")
    parts = re.split(r"(?m)^(?=\[\[sprites\]\]\s*$)", text)
    anim_src = f"art/animations/{ident}/{state}.png"
    pruned_idents: list[str] = []
    keep: list[str] = []
    for block in parts:
        if not block.lstrip().startswith("[[sprites]]"):
            keep.append(block)
            continue
        # Only consider frame entries pointing at this animation's strip.
        if anim_src not in block:
            keep.append(block)
            continue
        # Pull this block's slice index. Seed entry has index=0 and is
        # the canonical first frame — we never prune it. Frame entries
        # with index >= new_count are stale.
        m_idx = re.search(r"slice\s*=\s*\{[^}]*\bindex\s*=\s*(\d+)", block)
        if not m_idx:
            keep.append(block)
            continue
        idx = int(m_idx.group(1))
        if idx < new_count:
            keep.append(block)
            continue
        # Pull the ident so we can also drop the matching FX manifest
        # line and unlink the .bin file.
        m_ident = re.search(r'^\s*ident\s*=\s*"([^"]+)"', block, re.M)
        if m_ident:
            pruned_idents.append(m_ident.group(1))
        # Skip this block — drop it.
    if not pruned_idents:
        return f"prune: no stale frames past {new_count}"
    new_text = "".join(keep)
    if new_text != text:
        m_path.write_text(new_text, encoding="utf-8")

    # Also drop matching FX manifest lines + .bin files. The FX manifest
    # uses NAME = sprite_ident.upper().removesuffix("_DATA") and the
    # path data/fx/<subdir>/<sprite_ident>.bin.
    fx_manifest = REPO_ROOT / "tools" / "fxdata" / "manifest.txt"
    fx_lines_dropped = 0
    bins_dropped = 0
    if fx_manifest.is_file():
        fx_text = fx_manifest.read_text(encoding="utf-8")
        fx_keep: list[str] = []
        for line in fx_text.splitlines(keepends=True):
            stripped = line.split("#", 1)[0].strip()
            drop = False
            if stripped:
                parts2 = stripped.split(None, 1)
                if len(parts2) == 2:
                    name, _src = parts2
                    # Match the ident → manifest-name convention.
                    for spr_ident in pruned_idents:
                        manifest_name = spr_ident.upper().removesuffix("_DATA")
                        if name == manifest_name:
                            drop = True
                            fx_lines_dropped += 1
                            break
            if not drop:
                fx_keep.append(line)
        new_fx = "".join(fx_keep)
        if new_fx != fx_text:
            fx_manifest.write_text(new_fx, encoding="utf-8")

    # Unlink stale .bin files. We don't know each ident's subdir from
    # here (the routing map is in this file but per-ident); easier and
    # safer to glob across all data/fx/ subdirs.
    fx_dir = REPO_ROOT / "data" / "fx"
    for spr_ident in pruned_idents:
        for candidate in fx_dir.rglob(f"{spr_ident}.bin"):
            try:
                candidate.unlink()
                bins_dropped += 1
            except OSError:
                pass

    return (f"prune: dropped {len(pruned_idents)} sprites.toml blocks, "
            f"{fx_lines_dropped} fx manifest lines, {bins_dropped} bins")


def _sync_toml_dims(m_path: Path, ident: str, w: int, h: int, n: int) -> str:
    """Rewrite the manifest's width/height (and slice cols) in-place so
    they match what the editor just saved.

    Targets the seed sprite (matching `ident`) and any sliced frame
    sprites whose `source` points at art/animations/<ident>/*.png.

    Surgical regex edits only — preserves comments, blank lines, and
    surrounding formatting. Does NOT add or remove sprite blocks; if the
    saved frame count exceeds the existing slice indices, only the first
    N frames will be wired into sprites.cpp until you add more entries.
    Returns a one-line status string for logging.
    """
    import re
    text = m_path.read_text(encoding="utf-8")
    # Split into per-block sections delimited by `[[sprites]]` headers.
    # We rewrite each block independently so width/height edits in one
    # block can't bleed into another.
    parts = re.split(r"(?m)^(?=\[\[sprites\]\]\s*$)", text)
    anim_src_marker = f'art/animations/{ident}/'
    changed_blocks = 0
    fixed_w = fixed_h = fixed_cols = 0
    for i, block in enumerate(parts):
        if not block.lstrip().startswith("[[sprites]]"):
            continue
        # Match either the seed entry (ident == ident_to_sync) or a
        # frame entry pointing at the matching animations folder.
        is_seed  = re.search(rf'^\s*ident\s*=\s*"{re.escape(ident)}"\s*$', block, re.M)
        is_frame = anim_src_marker in block
        if not (is_seed or is_frame):
            continue
        new_block = block
        # width = N
        new_block, n_w = re.subn(
            r"^(\s*width\s*=\s*)\d+(\s*$)",
            lambda m: f"{m.group(1)}{w}{m.group(2)}",
            new_block, flags=re.M,
        )
        # height = N
        new_block, n_h = re.subn(
            r"^(\s*height\s*=\s*)\d+(\s*$)",
            lambda m: f"{m.group(1)}{h}{m.group(2)}",
            new_block, flags=re.M,
        )
        # slice = { cols = N, ... } — only on frame entries (the seed
        # has a bbox, not a slice).
        n_c = 0
        if is_frame:
            new_block, n_c = re.subn(
                r"(slice\s*=\s*\{\s*cols\s*=\s*)\d+",
                lambda m: f"{m.group(1)}{n}",
                new_block,
            )
        if new_block != block:
            parts[i] = new_block
            changed_blocks += 1
            fixed_w += n_w
            fixed_h += n_h
            fixed_cols += n_c
    if changed_blocks == 0:
        return f"toml sync: no entries to update for {ident}"
    new_text = "".join(parts)
    if new_text != text:
        m_path.write_text(new_text, encoding="utf-8")
    return f"toml sync: {changed_blocks} blocks (w={fixed_w} h={fixed_h} cols={fixed_cols})"


# ----------------------------------------------------------------------
# Animation metadata generator
# ----------------------------------------------------------------------
#
# Generates build/fxdata/animations.h with per-animation FRAME_COUNT,
# FRAME_BASE, and FRAME_STRIDE constants. This eliminates the need for
# hand-maintained PROGMEM frame-offset tables in platform code: any
# function reading <ANIM>_FRAME_BASE + N * <ANIM>_FRAME_STRIDE gets a
# correct FX offset without per-frame entries.
#
# Animation detection: a sprite is part of an animation iff its `source`
# matches `art/animations/<base_ident>/<state>.png` AND it has a slice
# with cols*rows > 1. The animation's base ident is the parent folder
# (e.g. `beatrice_data`) and its state is the file stem (e.g. `idle`).
# Frames within one animation share `(source, width, height)` and walk
# the slice index from 0 to cols*rows-1.
#
# What we emit per animation:
#   <ANIM>_FRAME_COUNT   = total frames on disk (cols * rows)
#   <ANIM>_FRAME_BASE    = FX byte offset of frame 0
#   <ANIM>_FRAME_STRIDE  = bytes per frame (width * height / 8)
#
# Code computes frame N's FX offset as:
#   <ANIM>_FRAME_BASE + N * <ANIM>_FRAME_STRIDE
#
# Stride invariant: every frame has identical bytesize, frames are
# emitted in slice-index order with no padding between them. This is
# true today (build pipeline appends frames sequentially via
# _refresh_fx_data + manifest order) and is enforced by static_asserts
# in animations.h itself.

def _generate_animations_h(m: config.Manifest) -> None:
    """Emit build/fxdata/animations.h. Walks the manifest, groups
    sprites by animation source path, and writes one header per
    animation. Idempotent: no-op if no animations are detected."""
    from collections import defaultdict
    out = REPO_ROOT / "build" / "fxdata" / "animations.h"
    out.parent.mkdir(parents=True, exist_ok=True)

    # ident -> list of (slice_index, sprite_ident)
    anim_groups: dict[tuple[str, str, int, int], list[tuple[int, str]]] = defaultdict(list)
    for spec in m.sprites:
        if spec.slice is None:
            continue
        total = spec.slice.cols * spec.slice.rows
        if total <= 1:
            continue
        # Only treat sprites under art/animations/ as animations.
        src_str = str(spec.source).replace("\\", "/")
        if "/art/animations/" not in src_str and not src_str.startswith("art/animations/"):
            continue
        # Extract <base_ident>/<state> from path
        # art/animations/<base>/<state>.png
        try:
            after = src_str.split("art/animations/", 1)[1]
            base, fname = after.split("/", 1)
            state = Path(fname).stem
        except (IndexError, ValueError):
            continue
        key = (base, state, spec.width, spec.height)
        anim_groups[key].append((spec.slice.index, spec.ident))

    if not anim_groups:
        # Still emit an empty header so includes don't fail on first
        # boot of a project without animations.
        out.write_text(
            "// AUTOGENERATED by tools/spritebake — DO NOT EDIT.\n"
            "// No animations detected.\n"
            "#pragma once\n"
            "#include \"types.h\"\n"
            "namespace anim {}\n",
            encoding="utf-8",
        )
        return

    lines = [
        "// AUTOGENERATED by tools/spritebake — DO NOT EDIT.",
        "// One <ANIM>_FRAME_{COUNT,BASE,STRIDE} triple per animation",
        "// detected in sprites.toml under art/animations/. Code reads",
        "// these to compute per-frame FX offsets without a hand-",
        "// maintained PROGMEM table.",
        "//",
        "// Source of truth: games/<game>/sprites.toml + the .bin files",
        "// in data/fx/. Regenerated whenever spritebake bake runs.",
        "",
        "#pragma once",
        "",
        "#include \"data_offsets.h\"  // OFFSET_<frame> constants",
        "#include \"types.h\"",
        "",
        "namespace anim {",
        "",
    ]

    for (base, state, w, h) in sorted(anim_groups.keys()):
        frames = sorted(anim_groups[(base, state, w, h)])
        # Frames must be contiguous (0..N-1) and base sprite (index 0)
        # is conventionally named without _f0 suffix, e.g. beatrice_data
        # not beatrice_data_f0. f1..fN-1 carry _f<N> suffix.
        first_ident = frames[0][1]
        # ANIM key in code: <BASE>_<STATE> uppercased, with the
        # trailing _DATA stripped (the manifest convention drops it
        # since "data" is true of every sprite ident).
        anim_key_raw = f"{base}_{state}".upper()
        anim_key = anim_key_raw.removesuffix("_DATA")
        # Column-major encoding rounds height up to whole 8-px pages,
        # so a 58-tall sprite emits ceil(58/8)=8 page rows × width
        # bytes. Match what encode.py actually produces.
        page_rows = (h + 7) // 8
        bytes_per_frame = w * page_rows

        # Strip on disk is the source of truth for frame count, NOT
        # the manifest entry count. Re-saving with fewer frames in the
        # editor leaves stale [[sprites]] entries pointing at out-of-
        # range slice indices; reading the strip directly avoids
        # baking past-EOF garbage as "frames" the runtime then plays.
        # Fallback to manifest count only if the strip is missing
        # (lets the build still produce something on a fresh checkout
        # before assets land).
        strip_path = REPO_ROOT / "art" / "animations" / base / f"{state}.png"
        total = len(frames)
        if strip_path.is_file():
            try:
                from PIL import Image as _PIL
                with _PIL.open(strip_path) as im:
                    sw, _sh = im.size
                if sw % w == 0:
                    total = sw // w
            except Exception:
                pass

        # FX offset constant for frame 0. The data_offsets.h symbol is
        # OFFSET_<manifest_name>. The manifest convention strips the
        # trailing _data from sprite idents (so bea_idle_f1_data →
        # BEA_IDLE_F1, beatrice_data → BEATRICE).
        first_sym = first_ident.upper().removesuffix("_DATA")
        base_offset_sym = f"OFFSET_{first_sym}"

        lines.append(f"// {base}/{state}.png — {total} frames at {w}x{h}")
        lines.append(f"constexpr u8  {anim_key}_FRAME_COUNT  = {total};")
        lines.append(f"constexpr u32 {anim_key}_FRAME_BASE   = fxdata::{base_offset_sym};")
        lines.append(f"constexpr u16 {anim_key}_FRAME_STRIDE = {bytes_per_frame};")
        # Stride invariant check: f1 must be at base + stride. Pick f1's
        # offset symbol if available — sprite at slice index 1 carries
        # _f1_data suffix in code per the existing naming convention.
        if len(frames) >= 2:
            f1_ident = frames[1][1]
            f1_sym = f"OFFSET_{f1_ident.upper().removesuffix('_DATA')}"
            lines.append(
                f"static_assert(fxdata::{f1_sym} == fxdata::{base_offset_sym} + {bytes_per_frame},"
            )
            lines.append(
                f"              \"{anim_key} f1 not at expected stride; FX manifest order changed\");"
            )
        lines.append("")

    lines.append("}  // namespace anim")
    lines.append("")
    out.write_text("\n".join(lines), encoding="utf-8")


def _refresh_fx_data(m: config.Manifest, baked_map: dict[str, SpriteData]) -> str:
    """Write FX-bound sprite bytes to data/fx/<subdir>/<ident>.bin and
    run the FX builder so build/fxdata/data.bin + data_offsets.h are
    fresh. Also copies data.bin into build-sdl/bin/ if that directory
    exists, matching what `make sdl` does. Returns one-line status."""
    import shutil
    import subprocess
    written = 0
    for ident, sprite in baked_map.items():
        subdir = _FX_BOUND_IDENTS.get(ident)
        if subdir is None:
            continue
        out_dir = REPO_ROOT / "data" / "fx" / subdir
        out_dir.mkdir(parents=True, exist_ok=True)
        bs = bytes(encode(sprite, m.output.layout))
        (out_dir / f"{ident}.bin").write_bytes(bs)
        written += 1
    if written == 0:
        return "no FX sprites changed"
    # Run the FX builder to regenerate data.bin + offsets header.
    builder = REPO_ROOT / "tools" / "fxdata" / "build.py"
    r = subprocess.run([sys.executable, str(builder)],
                       capture_output=True, text=True, cwd=REPO_ROOT)
    if r.returncode != 0:
        return f"fx builder failed: {r.stderr.strip()[-100:]}"
    # Generate animations.h alongside data_offsets.h — keeps animation
    # metadata (FRAME_COUNT, FRAME_BASE, FRAME_STRIDE) in sync with the
    # actual on-disk strip, eliminating the need for hand-maintained
    # PROGMEM frame-offset tables in platform code. Runs after the FX
    # builder so OFFSET_* symbols it references already exist.
    try:
        _generate_animations_h(m)
    except Exception as e:
        return f"FX rebuilt ({written} bins) but animations.h failed: {e}"
    # Copy data.bin into SDL bin dir if present, so the running SDL
    # backend picks up the new bytes on next launch.
    src = REPO_ROOT / "build" / "fxdata" / "data.bin"
    sdl_bin = REPO_ROOT / "build-sdl" / "bin"
    if sdl_bin.exists() and src.exists():
        shutil.copy(src, sdl_bin / "data.bin")
        return f"FX rebuilt ({written} bins) + copied to build-sdl/bin"
    return f"FX rebuilt ({written} bins) — no SDL bin dir to copy into"


def cmd_animate(args: argparse.Namespace) -> int:
    """Open the sprite_editor in animate mode for a sprite. The editor
    presents a per-state animation timeline (idle / walk / attack); the
    helper saves each state as a horizontal-strip PNG to
    art/animations/<ident>/<state>.png."""
    import base64
    import http.server
    import json
    import socket
    import socketserver
    import threading
    import urllib.parse
    import webbrowser

    m_path = _find_manifest(args.manifest)
    m = config.load_manifest(m_path, repo_root=REPO_ROOT)
    spec = next((s for s in m.sprites if s.ident == args.ident or s.ident.removesuffix("_data") == args.ident), None)
    if spec is None:
        raise SystemExit(f"unknown sprite ident: {args.ident!r}")

    # Frame 0 of the animation seeds with the touchup-aware baked bytes —
    # if the user has hand-edited art/touchups/<ident>.png that wins
    # over the pipeline output. This way base-sprite tweaks flow into
    # any new animation work automatically.
    override = _touchup_override(spec.ident, spec.width, spec.height)
    if override is not None:
        baked = override
    else:
        source = _load_sprite_source(spec)
        pipeline = spec.resolve_pipeline(m.pipelines)
        baked = pipeline.run(source)
    bytes_list = encode(baked, m.output.layout)
    b64 = base64.b64encode(bytes(bytes_list)).decode("ascii")

    anim_root = REPO_ROOT / "art" / "animations" / spec.ident
    anim_root.mkdir(parents=True, exist_ok=True)
    editor_path = REPO_ROOT / "tools" / "sprite_editor" / "index.html"
    editor_html = editor_path.read_bytes()

    # Optional tracing layer: load the reference image (if any) once,
    # serve it as PNG bytes from /trace.png. Editor renders it as a
    # translucent underlay so the user can hand-trace each animation
    # frame by panning/zooming the trace image to fit one cell at a
    # time and drawing 1-bit pixels over it. Same shape as touchup.
    trace_png_bytes: bytes | None = None
    trace_path: Path | None = getattr(args, "trace", None)
    if trace_path is not None:
        from PIL import Image
        from io import BytesIO
        if not trace_path.is_absolute():
            trace_path = (REPO_ROOT / trace_path).resolve()
        if not trace_path.is_file():
            raise SystemExit(f"--trace path not found: {trace_path}")
        trace_img = Image.open(trace_path).convert("L")
        buf = BytesIO()
        trace_img.save(buf, format="PNG")
        trace_png_bytes = buf.getvalue()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a, **kw):
            pass

        def do_GET(self):
            url = urllib.parse.urlparse(self.path)
            if url.path == "/" or url.path == "/index.html":
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(editor_path.read_bytes())
                return
            if url.path == "/trace.png" and trace_png_bytes is not None:
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(trace_png_bytes)
                return
            if url.path == "/anim/load":
                qs = urllib.parse.parse_qs(url.query)
                state = qs.get("state", [""])[0]
                if not state:
                    self.send_response(400); self.end_headers(); return
                target = anim_root / f"{state}.png"
                if not target.exists():
                    self.send_response(404); self.end_headers(); return
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(target.read_bytes())
                return
            if url.path == "/anim/list":
                # Lists every <state>.png in art/animations/<ident>/ so the
                # editor's dropdown can show existing animations (including
                # any user-created duplicates) after a server restart.
                states = sorted(p.stem for p in anim_root.glob("*.png"))
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(json.dumps({"states": states}).encode("utf-8"))
                return
            self.send_response(404); self.end_headers()

        def do_POST(self):
            url = urllib.parse.urlparse(self.path)
            if url.path == "/anim/save":
                qs = urllib.parse.parse_qs(url.query)
                ident2 = qs.get("ident", [""])[0]
                state = qs.get("state", [""])[0]
                w = qs.get("w", ["?"])[0]
                h = qs.get("h", ["?"])[0]
                n = qs.get("n", ["?"])[0]
                if not (ident2 and state):
                    self.send_response(400); self.end_headers(); return
                length = int(self.headers.get("Content-Length", "0"))
                data = self.rfile.read(length)
                target = anim_root / f"{state}.png"
                target.write_bytes(data)
                rel = target.relative_to(REPO_ROOT).as_posix()
                # Guardrail: sync the manifest's width/height (and slice
                # cols on related frame entries) to whatever the editor
                # just saved. Without this, the toml's declared sprite
                # size can drift from the strip on disk, and the next
                # editor launch loads the stale dims, fails to read the
                # strip, and silently resets to a single seed frame —
                # losing any unsaved canvas work and looking like the
                # saved animation has vanished. Source of truth is what
                # the user actually saved.
                try:
                    sync_status = _sync_toml_dims(m_path, ident2, int(w), int(h), int(n))
                except Exception as e:
                    sync_status = f"toml sync failed: {e}"
                # Prune stale per-frame entries past the new strip end.
                # When the strip shrinks, sprites.toml + the FX manifest
                # accumulate dead entries pointing past EOF. Without
                # pruning, the next bake either silently produces
                # zero-pixel "frames" or breaks. The editor's `state`
                # is the second path component (idle / walk / attack).
                try:
                    prune_status = _prune_stale_frames(m_path, ident2, state, int(n))
                except Exception as e:
                    prune_status = f"prune failed: {e}"
                # Auto-bake so the bytes flow into sprites.cpp without a
                # manual `spritebake bake` step. Animation saves are
                # always FX-bound (anim frames live on FX flash), so we
                # also refresh data/fx/anim/<ident>.bin and run the FX
                # builder. After this returns, only the SDL/Arduboy
                # rebuild step is left.
                bake_status = "skipped"
                fx_status = ""
                try:
                    m_reload = config.load_manifest(m_path, repo_root=REPO_ROOT)
                    baked_map = _bake_manifest(m_reload)
                    results = patch.patch_sprites_cpp(
                        m_reload.output.cpp,
                        baked_map,
                        layout=m_reload.output.layout,
                        progmem=m_reload.output.progmem,
                        dry_run=False,
                    )
                    ok = sum(1 for r in results if r.status == "replaced")
                    bake_status = f"baked {ok}/{len(results)}"
                    fx_status = " | " + _refresh_fx_data(m_reload, baked_map)
                except Exception as e:
                    bake_status = f"bake failed: {e}"
                print(f"[anim/save] {rel} state={state} {w}x{h} n={n} bytes={length} | {sync_status} | {prune_status} | {bake_status}{fx_status}")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"path": rel, "bake": bake_status}).encode("utf-8"))
                return
            self.send_response(404); self.end_headers()

    httpd = socketserver.TCPServer(("127.0.0.1", port), Handler)
    th = threading.Thread(target=httpd.serve_forever, daemon=True)
    th.start()

    url = (
        f"http://127.0.0.1:{port}/#"
        f"w={baked.width}&h={baked.height}"
        f"&name={spec.ident}"
        f"&layout={m.output.layout}"
        f"&import=b64:{urllib.parse.quote(b64)}"
        f"&animate={spec.ident}"
    )
    if trace_png_bytes is not None:
        url += "&trace=/trace.png"
    print(f"Animate helper listening at http://127.0.0.1:{port}")
    print(f"Editing: {spec.ident} ({baked.width}x{baked.height})")
    print(f"Saves go to: {anim_root.relative_to(REPO_ROOT).as_posix()}/<state>.png")
    print(f"Press Ctrl+C to stop the helper.")
    webbrowser.open(url)
    try:
        th.join()
    except KeyboardInterrupt:
        httpd.shutdown()
    return 0


def cmd_inspect_bbox(args: argparse.Namespace) -> int:
    from .inspect_bbox import render_bbox_inspect
    m_path = _find_manifest(args.manifest)
    m = config.load_manifest(m_path, repo_root=m_path.parent.parent.parent)
    out = render_bbox_inspect(
        m,
        ident_prefix=args.filter,
        padding=args.padding,
        out_path=args.output,
    )
    print(f"wrote {out}")
    if args.open:
        import subprocess
        subprocess.Popen(["cmd", "/c", "start", "", str(out)], shell=False)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="spritebake")
    parser.add_argument("-m", "--manifest", type=Path, default=None,
                        help="Path to sprites.toml (default: discover from CWD)")
    sub = parser.add_subparsers(dest="command", required=True)

    p_bake = sub.add_parser("bake", help="Rebuild all sprites and patch the .cpp")
    p_bake.add_argument("--dry-run", action="store_true", help="Don't write files")
    p_bake.set_defaults(func=cmd_bake)

    p_prev = sub.add_parser("preview", help="Render a matrix PNG")
    p_prev.add_argument("-o", "--output", type=Path, default=None)
    p_prev.add_argument("-s", "--sprite", action="append", help="Filter to specific ident(s)")
    p_prev.add_argument("-p", "--pipeline", action="append",
                        help="Compare one or more named pipelines (repeat to stack)")
    p_prev.add_argument("--scale", type=int, default=6)
    p_prev.add_argument("--open", action="store_true", help="Open the preview in the default viewer")
    p_prev.set_defaults(func=cmd_preview)

    p_diff = sub.add_parser("diff", help="Report which sprites would change under a rebuild")
    p_diff.set_defaults(func=cmd_diff)

    p_list = sub.add_parser("list-stages", help="List every registered pipeline stage")
    p_list.set_defaults(func=cmd_list_stages)

    p_rt = sub.add_parser("roundtrip", help="Verify existing NAME_data encodes/decodes identically")
    p_rt.set_defaults(func=cmd_roundtrip)

    p_tu = sub.add_parser("touchup",
                          help="Open the sprite_editor for a given sprite, with a local helper that saves edits to art/touchups/<ident>.png")
    p_tu.add_argument("ident", help="Sprite ident (e.g. player_heretic_lvl1_data, or _data suffix omitted)")
    p_tu.add_argument("--trace", type=Path, default=None,
                      help="Path to a reference image displayed under the canvas as a tracing layer (any image format Pillow reads; resized to the sprite's editor canvas). Use it to draw 1-bit pixels by tracing over a hard-to-bake source.")
    p_tu.set_defaults(func=cmd_touchup)

    p_an = sub.add_parser("animate",
                          help="Open the sprite_editor in animate mode for a sprite. Builds per-state animation strips at art/animations/<ident>/<state>.png")
    p_an.add_argument("ident", help="Sprite ident (e.g. player_penitent_lvl1_world_data, or _data suffix omitted)")
    p_an.add_argument("--trace", type=Path, default=None,
                      help="Path to a reference image displayed under the canvas as a tracing layer. Pan/zoom (Shift+drag, Shift+wheel) to align one source cell at a time, then trace 1-bit pixels over it.")
    p_an.set_defaults(func=cmd_animate)

    p_ib = sub.add_parser("inspect-bbox",
                          help="Render padded crops with bbox edges drawn — for tuning bbox coords")
    p_ib.add_argument("-f", "--filter", default=None,
                      help="Limit to idents starting with this prefix")
    p_ib.add_argument("--padding", type=int, default=30,
                      help="Pixels of context around the bbox (default 30)")
    p_ib.add_argument("-o", "--output", type=Path, default=None)
    p_ib.add_argument("--open", action="store_true", help="Open the result in the default viewer")
    p_ib.set_defaults(func=cmd_inspect_bbox)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
