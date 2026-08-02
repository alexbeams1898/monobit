#!/usr/bin/env python3
"""Serves the observation-tree tool (tree.html) rooted at THIS game.

The tool always operates on the game it lives inside -- the server resolves the
game folder from its own location, so there is no folder picking anywhere.
Run `python tree.py` (or double-click tree.bat); Ctrl+C stops it.

Binds localhost only. Writes exactly two files (psyche.json + its layout
sidecar); everything else is read-only.
"""
import json
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
WEB = TOOLS / "tree"  # the static app (index.html + js modules + css)
GAME = TOOLS.parent
OBS = GAME / "config" / "psyche.json"
LAYOUT = GAME / "config" / "psyche.layout.json"
ACTIONS = GAME / "config" / "actions.json"
NPCS = GAME / "config" / "npcs"
SCENES = GAME / "config" / "scenes"
PORT = 8737
CTYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
          ".css": "text/css; charset=utf-8", ".json": "application/json"}


def read_json(path, fallback):
    if not path.exists():
        return fallback
    return json.loads(path.read_text(encoding="utf-8"))


def world_progression():
    """The map's own order: levels ranked by warp-distance from the start level
    (the one holding the id-less PlayerSpawn), plus which level each placed
    encounter lives in. This is the tool's PROGRESSION axis -- the story moves
    through places, and the map is the only honest source of that order."""
    ldtk_path = GAME / json.loads(
        (GAME / "config" / "world.json").read_text(encoding="utf-8"))["ldtk"]
    j = json.loads(ldtk_path.read_text(encoding="utf-8"))

    def field(e, name):
        for fi in e.get("fieldInstances", []):
            if fi.get("__identifier") == name:
                return fi.get("__value")
        return None

    warps = {}       # level -> [target levels]
    placements = {}  # encounter id -> level
    start = None
    for lvl in j.get("levels", []):
        lid = lvl["identifier"]
        warps.setdefault(lid, [])
        for li in lvl.get("layerInstances", []):
            for e in li.get("entityInstances", []):
                ident = e.get("__identifier")
                if ident == "Warp" and field(e, "target_level"):
                    warps[lid].append(field(e, "target_level"))
                elif ident == "PlayerSpawn" and not field(e, "id"):
                    start = lid
                elif ident in ("Encounter", "Npc") and field(e, "encounter"):
                    placements[field(e, "encounter")] = lid
    ranks = {}
    frontier = [start] if start else []
    rank = 0
    while frontier:
        nxt = []
        for lid in frontier:
            if lid in ranks or lid not in warps:
                continue
            ranks[lid] = rank
            nxt.extend(warps[lid])
        frontier = nxt
        rank += 1
    for lid in warps:  # unreachable levels sit past everything reachable
        ranks.setdefault(lid, rank)
    return {"levels": ranks, "placements": placements}


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/data":
            npcs = {}
            if NPCS.is_dir():
                for f in sorted(NPCS.glob("*.json")):
                    j = json.loads(f.read_text(encoding="utf-8"))
                    npcs[j.get("id", f.stem)] = j.get("name", j.get("id", f.stem))
            scenes = []
            if SCENES.is_dir():
                for f in sorted(SCENES.glob("*.json")):
                    s = json.loads(f.read_text(encoding="utf-8"))
                    s.setdefault("id", f.stem)
                    scenes.append(s)
            self._send(200, json.dumps({
                "psyche": read_json(OBS, {}),
                "actions": read_json(ACTIONS, {}),
                "npcs": npcs,
                "scenes": scenes,
                "world": world_progression(),
                "layout": read_json(LAYOUT, {}),
            }).encode())
        else:
            # Static app files, locked inside the tree/ dir.
            rel = "index.html" if self.path == "/" else self.path.lstrip("/")
            target = (WEB / rel).resolve()
            if WEB.resolve() in target.parents and target.is_file():
                self._send(200, target.read_bytes(),
                           CTYPES.get(target.suffix, "application/octet-stream"))
            else:
                self._send(404, b"not found", "text/plain")

    def do_POST(self):
        if self.path != "/api/save":
            return self._send(404, b"{}")
        n = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(n))
        OBS.write_text(json.dumps(body["psyche"], indent=4, ensure_ascii=False) + "\n",
                       encoding="utf-8", newline="\n")
        LAYOUT.write_text(json.dumps(body["layout"], indent=4) + "\n",
                          encoding="utf-8", newline="\n")
        return self._send(200, b"{}")

    def log_message(self, *args):
        pass  # keep the console quiet


if __name__ == "__main__":
    server = HTTPServer(("127.0.0.1", PORT), Handler)
    url = f"http://127.0.0.1:{PORT}/"
    print(f"[tree] editing {OBS}")
    print(f"[tree] open {url}  (Ctrl+C stops the tool)")
    webbrowser.open(url)
    server.serve_forever()
