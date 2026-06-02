"""
preview_clip.py - Local browser-based animation previewer.

Spins up a one-shot localhost HTTP server with:
  * /                    - viewer page (three.js, dropdown of every
                           available clip, timeline + skeleton toggle)
  * /clips               - JSON list of all discovered clips
  * /clip?name=<id>      - bytes of the requested clip (.glb or .fbx),
                           with the appropriate Content-Type

The viewer's dropdown is populated from /clips at page-load. Picking a
new clip swaps it in-place — no server restart, no script re-run.

Usage:
    python preview_clip.py                  # discover defaults, open picker
    python preview_clip.py <name>           # auto-select a clip on open
    python preview_clip.py --scan <dir>     # also scan <dir> for .fbx/.glb
    python preview_clip.py --list           # print discovered clips and exit
    python preview_clip.py --port 8765      # change server port

Default discovery roots:
  1. build/selva-oscura-attacks/<name>/<name>.glb       (built clips)
  2. games/selva-oscura/assets/characters/x_bot/        (mesh + ozz dir)
  3. games/selva-oscura/assets/characters/x_bot/source/<pack>/
                                                        (raw Mixamo .fbx —
                                                        every pack subdir is
                                                        scanned automatically)

`--scan PATH` may be repeated; each path is searched recursively for
.fbx and .glb files. This is how you preview a freshly downloaded pack
before wiring it into CMake.
"""

from __future__ import annotations

import argparse
import http.server
import json
import os
import re
import sys
import threading
import urllib.parse
import webbrowser
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
ATTACKS_BUILD_DIR = REPO_ROOT / "build" / "selva-oscura-attacks"
XBOT_ASSETS_DIR = (
    REPO_ROOT / "games" / "selva-oscura" / "assets" / "characters" / "x_bot"
)
PACKS_ROOT = XBOT_ASSETS_DIR / "source"
# Non-humanoid character roots. Each contains its own bundled .glb
# (mesh + every animation track in one file). Three.js GLTFLoader
# returns all tracks; viewer JS enumerates them as a sub-dropdown.
NONHUMANOID_ROOTS = [
    REPO_ROOT / "games" / "selva-oscura" / "assets" / "characters" / "wolf",
]


def sanitize(name: str) -> str:
    """Mirror the CMake sanitizer: lowercase, [^a-z0-9]+ -> _, trim _."""
    s = re.sub(r"[^a-z0-9]+", "_", name.lower())
    s = re.sub(r"_+", "_", s).strip("_")
    return s


def discover(extra_scan_dirs: list[Path]) -> list[dict]:
    """Return every clip we can preview, as dicts:
        { id, label, group, path, kind: 'glb'|'fbx' }

    `id` is what /clip?name=<id> consumes. We use the sanitized stem so
    multiple sources sharing a name don't collide; later sources in the
    list shadow earlier ones (preference: built > xbot > pack > extra).
    """
    found: list[dict] = []
    seen_ids: set[str] = set()

    def add(p: Path, group: str, label: str | None = None) -> None:
        kind = p.suffix.lower().lstrip(".")
        if kind not in ("glb", "fbx"):
            return
        base = sanitize(p.stem)
        if not base:
            return
        # Don't shadow on collision — multiple sources can legitimately
        # share a stem (a built .glb and the raw .fbx it came from). We
        # disambiguate by appending a group prefix when the bare id
        # already exists. The first registration wins the bare id; later
        # collisions become "<group_slug>__<base>".
        clip_id = base if base not in seen_ids else f"{sanitize(group)}__{base}"
        if clip_id in seen_ids:
            return
        seen_ids.add(clip_id)
        found.append(
            {
                "id": clip_id,
                "label": label or p.stem,
                "group": group,
                "path": str(p),
                "kind": kind,
            }
        )

    if ATTACKS_BUILD_DIR.is_dir():
        for sub in sorted(ATTACKS_BUILD_DIR.iterdir()):
            if sub.is_dir():
                glb = sub / f"{sub.name}.glb"
                if glb.exists():
                    add(glb, "Built clips")

    if XBOT_ASSETS_DIR.is_dir():
        for p in sorted(XBOT_ASSETS_DIR.glob("*.glb")):
            add(p, "X Bot assets")

    # Non-humanoid characters. Each ships its mesh + every animation
    # track inside one .glb (Blender's default export shape). The
    # viewer JS picks them apart into a per-clip sub-dropdown after
    # load; from discover()'s perspective each is one file entry.
    for root in NONHUMANOID_ROOTS:
        if not root.is_dir():
            continue
        char_name = root.name
        for p in sorted(root.glob("*.glb")):
            add(p, f"Wolf assets" if char_name == "wolf" else f"{char_name} assets")

    # Each pack subdir under source/ becomes its own optgroup so the
    # picker has a clean Pro Sword and Shield Pack vs Action Adventure
    # Pack split. Loose .fbx at the top of source/ (Stand To Roll,
    # Standard Idle, etc.) get a "FBX source (loose)" group. The
    # bound-character `X Bot.fbx` shipped inside each pack is filtered
    # out — it's the mesh, not a clip, and we already have it via the
    # X_Bot.glb conversion above.
    def _is_bot_mesh(stem: str) -> bool:
        return re.fullmatch(r"x[ _]?bot", stem.lower()) is not None

    if PACKS_ROOT.is_dir():
        for p in sorted(PACKS_ROOT.glob("*.fbx")):
            if not _is_bot_mesh(p.stem):
                add(p, "FBX source (loose)")
        for sub in sorted(PACKS_ROOT.iterdir()):
            if not sub.is_dir():
                continue
            for p in sorted(sub.glob("*.fbx")):
                if not _is_bot_mesh(p.stem):
                    add(p, f"FBX source ({sub.name})")

    for d in extra_scan_dirs:
        if d.is_dir():
            for ext in ("fbx", "glb"):
                for p in sorted(d.rglob(f"*.{ext}")):
                    add(p, f"Scan: {d.name}")

    return found


# ---------------------------------------------------------------------------
# Viewer HTML — three.js with GLTF + FBX loaders, animation mixer, dropdown
# clip picker. Self-contained: lives in a single string so the script ships
# as one file.
# ---------------------------------------------------------------------------
VIEWER_HTML = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>clip preview</title>
<style>
  body { margin: 0; background: #1b1b1b; color: #ddd;
         font-family: system-ui, sans-serif; }
  #app { position: fixed; inset: 0; }
  #topbar { position: fixed; top: 12px; left: 12px; right: 12px;
            display: flex; gap: 12px; align-items: center;
            background: rgba(0,0,0,0.55); padding: 8px 12px;
            border-radius: 8px; }
  #topbar select { flex: 1; background: #222; color: #ddd; border: 1px solid #333;
                   padding: 6px 8px; border-radius: 4px; font-size: 14px; }
  #topbar #info { font-size: 13px; color: #aaa; min-width: 14ch; text-align: right; }
  #ui  { position: fixed; left: 12px; bottom: 12px; right: 12px;
         display: flex; gap: 12px; align-items: center;
         background: rgba(0,0,0,0.55); padding: 10px 14px;
         border-radius: 8px; }
  #ui input[type=range] { flex: 1; }
  #ui button { background: #333; color: #ddd; border: 0;
               padding: 6px 10px; border-radius: 4px; cursor: pointer; }
  #ui button:hover { background: #444; }
  #ui span { font-size: 13px; min-width: 12ch; text-align: right; }
</style>
</head>
<body>
<div id="app"></div>
<div id="topbar">
  <select id="picker"></select>
  <select id="trackPicker" style="max-width: 28ch;"></select>
  <span id="info">loading…</span>
</div>
<div id="ui">
  <button id="play">Pause</button>
  <input id="scrub" type="range" min="0" max="1000" value="0" step="1">
  <span id="time">0.00 / 0.00s</span>
  <button id="skel">Skeleton</button>
  <button id="csv">Export CSV</button>
</div>

<script type="importmap">
{
  "imports": {
    "three":           "https://unpkg.com/three@0.160.0/build/three.module.js",
    "three/addons/":   "https://unpkg.com/three@0.160.0/examples/jsm/"
  }
}
</script>
<script type="module">
import * as THREE from 'three';
import { GLTFLoader }    from 'three/addons/loaders/GLTFLoader.js';
import { FBXLoader }     from 'three/addons/loaders/FBXLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

const app = document.getElementById('app');
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(devicePixelRatio);
renderer.setSize(innerWidth, innerHeight);
app.appendChild(renderer.domElement);
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x1b1b1b);

scene.add(new THREE.HemisphereLight(0xffffff, 0x303030, 1.0));
const dir = new THREE.DirectionalLight(0xffffff, 1.0);
dir.position.set(2, 5, 3);
scene.add(dir);
scene.add(new THREE.GridHelper(10, 10, 0x444444, 0x222222));

const camera = new THREE.PerspectiveCamera(45, innerWidth / innerHeight, 0.1, 200);
camera.position.set(2, 1.5, 3);
const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, 1, 0);
controls.update();
addEventListener('resize', () => {
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
});

let currentRoot = null;
let mixer = null;
let action = null;
let skeletonHelper = null;
let skeletonVisible = false;
const clock = new THREE.Clock();

const picker = document.getElementById('picker');
const trackPicker = document.getElementById('trackPicker');
const info   = document.getElementById('info');
let loadedAnimations = []; // most-recent loadClip's animation array
const playBtn = document.getElementById('play');
const skelBtn = document.getElementById('skel');
const scrub   = document.getElementById('scrub');
const timeOut = document.getElementById('time');
const csvBtn  = document.getElementById('csv');
let paused = false;
let currentMeta = null;

playBtn.onclick = () => {
  paused = !paused;
  playBtn.textContent = paused ? 'Play' : 'Pause';
};
skelBtn.onclick = () => {
  skeletonVisible = !skeletonVisible;
  if (skeletonHelper) skeletonHelper.visible = skeletonVisible;
};
scrub.oninput = () => {
  if (!action) return;
  const dur = action.getClip().duration;
  action.time = (scrub.value / 1000) * dur;
  paused = true;
  playBtn.textContent = 'Play';
  mixer.update(0);
};
csvBtn.onclick = () => {
  if (!action || !currentRoot || !currentMeta) return;
  // Sample the animation at 60 Hz from t=0 to t=duration. We seize
  // the mixer to step through manually, capturing world-space bone
  // positions. After, restore the mixer to t=0 and unpause; the user
  // sees a brief restart, no big deal.
  const dur = action.getClip().duration;
  const dt = 1.0 / 60.0;
  const samples = [];
  // Collect every bone (anything in the skeleton helper).
  const bones = [];
  currentRoot.traverse((obj) => {
    if (obj.isBone) bones.push(obj);
  });
  // Reset mixer to t=0, step manually frame-by-frame.
  action.time = 0;
  mixer.update(0);
  let t = 0.0;
  // updateMatrixWorld is required to refresh getWorldPosition results
  // after stepping the mixer (which only writes local matrices).
  while (t <= dur + 1e-6) {
    currentRoot.updateMatrixWorld(true);
    const row = { t: t, joints: [] };
    for (const b of bones) {
      const wp = new THREE.Vector3();
      b.getWorldPosition(wp);
      row.joints.push({ name: b.name, x: wp.x, y: wp.y, z: wp.z });
    }
    samples.push(row);
    mixer.update(dt);
    t += dt;
  }
  // Build CSV body.
  let csv = 'time_s,joint_name,x,y,z\n';
  for (const s of samples) {
    for (const j of s.joints) {
      csv += `${s.t.toFixed(4)},${j.name},${j.x.toFixed(6)},${j.y.toFixed(6)},${j.z.toFixed(6)}\n`;
    }
  }
  // Download.
  const blob = new Blob([csv], { type: 'text/csv' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `debug_bones_browser_${currentMeta.id}.csv`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
  // Restore mixer to t=0 so playback resumes cleanly.
  action.time = 0;
  mixer.update(0);
};

function playTrack(idx) {
  if (!mixer || !loadedAnimations || idx < 0 || idx >= loadedAnimations.length)
    return;
  if (action) action.stop();
  const clip = loadedAnimations[idx];
  action = mixer.clipAction(clip);
  action.play();
  trackPicker.value = String(idx);
  info.textContent =
    `${clip.name || '(unnamed)'} · ${clip.duration.toFixed(2)}s · ${currentMeta ? currentMeta.kind : ''}`;
}

trackPicker.onchange = () => {
  const idx = parseInt(trackPicker.value, 10);
  if (!isNaN(idx)) playTrack(idx);
};

function disposeCurrent() {
  if (currentRoot) {
    scene.remove(currentRoot);
    currentRoot.traverse((obj) => {
      if (obj.geometry) obj.geometry.dispose();
      if (obj.material) {
        const mats = Array.isArray(obj.material) ? obj.material : [obj.material];
        for (const m of mats) m.dispose();
      }
    });
    currentRoot = null;
  }
  if (skeletonHelper) {
    scene.remove(skeletonHelper);
    skeletonHelper = null;
  }
  mixer = null;
  action = null;
}

async function loadClip(meta) {
  disposeCurrent();
  currentMeta = meta;
  info.textContent = 'loading…';
  const url = `/clip?name=${encodeURIComponent(meta.id)}`;
  try {
    let root, animations;
    if (meta.kind === 'glb') {
      const gltf = await new GLTFLoader().loadAsync(url);
      root = gltf.scene;
      animations = gltf.animations || [];
    } else {
      const fbx = await new FBXLoader().loadAsync(url);
      root = fbx;
      animations = fbx.animations || [];
      // FBX commonly ships in cm (100x). Normalize so X Bot lands at
      // ~1.7m height, matching the GLB-loaded characters in the same
      // viewer.
      root.scale.set(0.01, 0.01, 0.01);
    }
    scene.add(root);
    currentRoot = root;

    skeletonHelper = new THREE.SkeletonHelper(root);
    skeletonHelper.visible = skeletonVisible;
    scene.add(skeletonHelper);

    loadedAnimations = animations;
    // Populate the in-file track sub-dropdown. Multi-clip GLBs (wolf
    // bundle has 14 animation tracks; ER bosses commonly ship the
    // same way) get one entry per track; single-clip files get one
    // entry. Switching entries calls playTrack(idx) without reload.
    trackPicker.innerHTML = '';
    for (let i = 0; i < animations.length; ++i) {
      const opt = document.createElement('option');
      opt.value = i;
      opt.textContent = `${i}: ${animations[i].name || '(unnamed)'} (${animations[i].duration.toFixed(2)}s)`;
      trackPicker.appendChild(opt);
    }
    trackPicker.style.display = (animations.length > 1) ? 'inline-block' : 'none';
    if (animations.length > 0) {
      mixer = new THREE.AnimationMixer(root);
      playTrack(0);
    } else {
      info.textContent = `${meta.label} · no animation · ${meta.kind}`;
    }
  } catch (err) {
    info.textContent = 'load failed: ' + err;
    console.error(err);
  }
}

async function init() {
  const clips = await fetch('/clips').then((r) => r.json());
  // Group into <optgroup>s by clip.group, preserve discovery order.
  const groups = new Map();
  for (const c of clips) {
    if (!groups.has(c.group)) groups.set(c.group, []);
    groups.get(c.group).push(c);
  }
  const idToMeta = new Map();
  for (const [groupName, items] of groups) {
    const og = document.createElement('optgroup');
    og.label = `${groupName} (${items.length})`;
    for (const c of items) {
      const opt = document.createElement('option');
      opt.value = c.id;
      opt.textContent = c.label;
      og.appendChild(opt);
      idToMeta.set(c.id, c);
    }
    picker.appendChild(og);
  }
  picker.onchange = () => {
    const meta = idToMeta.get(picker.value);
    if (meta) loadClip(meta);
  };
  // Pre-select via ?clip=<id> URL param if present.
  const params = new URLSearchParams(location.search);
  const initial = params.get('clip');
  if (initial && idToMeta.has(initial)) {
    picker.value = initial;
  }
  if (idToMeta.size > 0) {
    const meta = idToMeta.get(picker.value);
    if (meta) await loadClip(meta);
  } else {
    info.textContent = 'no clips found';
  }
}
init();

(function loop() {
  requestAnimationFrame(loop);
  if (mixer && !paused) {
    const dt = clock.getDelta();
    mixer.update(dt);
    if (action) {
      const dur = action.getClip().duration;
      const t = action.time % dur;
      scrub.value = String(Math.round((t / dur) * 1000));
      timeOut.textContent = `${t.toFixed(2)} / ${dur.toFixed(2)}s`;
    }
  } else if (action) {
    const dur = action.getClip().duration;
    timeOut.textContent = `${action.time.toFixed(2)} / ${dur.toFixed(2)}s`;
  }
  controls.update();
  renderer.render(scene, camera);
})();
</script>
</body>
</html>
"""


def serve(clips: list[dict], port: int, initial_clip: str | None) -> None:
    by_id = {c["id"]: c for c in clips}

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args, **kwargs):
            pass

        def _send(self, status: int, body: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            parsed = urllib.parse.urlparse(self.path)
            qs = urllib.parse.parse_qs(parsed.query)

            if parsed.path in ("/", "/index.html"):
                self._send(200, VIEWER_HTML.encode("utf-8"), "text/html; charset=utf-8")
                return

            if parsed.path == "/clips":
                # Strip absolute paths from the response — the client doesn't
                # need them, and exposing them is unnecessary information.
                public = [
                    {"id": c["id"], "label": c["label"], "group": c["group"], "kind": c["kind"]}
                    for c in clips
                ]
                self._send(200, json.dumps(public).encode("utf-8"), "application/json")
                return

            if parsed.path == "/clip":
                name = qs.get("name", [""])[0]
                meta = by_id.get(name)
                if meta is None:
                    self._send(404, b"unknown clip", "text/plain")
                    return
                try:
                    body = Path(meta["path"]).read_bytes()
                except OSError as ex:
                    self._send(500, f"read failed: {ex}".encode("utf-8"), "text/plain")
                    return
                ctype = "model/gltf-binary" if meta["kind"] == "glb" else "application/octet-stream"
                self._send(200, body, ctype)
                return

            self._send(404, b"not found", "text/plain")

    server = http.server.HTTPServer(("127.0.0.1", port), Handler)
    url = f"http://127.0.0.1:{port}/"
    if initial_clip:
        url += f"?clip={urllib.parse.quote(initial_clip)}"
    threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    print(f"preview server: {url}")
    print(f"  {len(clips)} clip(s) discovered")
    print("Ctrl-C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("clip", nargs="?",
                        help="optional sanitized clip id to pre-select")
    parser.add_argument("--list", action="store_true",
                        help="print discovered clips and exit")
    parser.add_argument("--scan", action="append", default=[], metavar="DIR",
                        help="extra directory to scan for .fbx/.glb (repeatable)")
    parser.add_argument("--port", type=int, default=8765,
                        help="local server port (default 8765)")
    args = parser.parse_args()

    extra_dirs = [Path(p).resolve() for p in args.scan]
    clips = discover(extra_dirs)

    if args.list:
        if not clips:
            print("no clips found.", file=sys.stderr)
            return 1
        # Group output for readability.
        current_group = None
        for c in clips:
            if c["group"] != current_group:
                current_group = c["group"]
                print(f"\n[{current_group}]")
            print(f"  {c['id']:40s}  {c['kind']}  {c['path']}")
        return 0

    if not clips:
        print(
            "no clips found. Build first (F7) or pass --scan <dir>.",
            file=sys.stderr,
        )
        return 1

    initial = args.clip
    if initial and initial not in {c["id"] for c in clips}:
        # Be permissive — accept the unsanitized form too.
        s = sanitize(initial)
        initial = s if s in {c["id"] for c in clips} else None
        if initial is None:
            print(
                f"clip '{args.clip}' not found; opening picker with no preselection.",
                file=sys.stderr,
            )

    serve(clips, args.port, initial)
    return 0


if __name__ == "__main__":
    sys.exit(main())
