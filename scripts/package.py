"""Builds a game's release archive.

One packager for every game. There were three, one per game, and they had
already drifted: prison-escape's shipped its asset tree whole, selva's
subtracted four patterns from it, wayworn's had grown a third shape. The exe
path drifted the same way and cost three silent release failures before
anybody looked.

What ships is DECLARED, in each game's .release-config.yml, as a list of paths
to include. Not a list to exclude. An exclusion list fails open: whatever a new
pipeline drops into the asset tree ships by default, and the only signal is the
download size. An inclusion list fails closed -- a new kind of runtime data
does not ship until somebody says so, and check_assets.py fails the build when
something the game loads is not covered.

Sources are deliberate too. Everything but the executable and its DLLs comes
from the SOURCE tree, never from build/bin. That directory is derived, is never
pruned, and on a developer machine holds every asset that has existed on any
branch -- 1.9GB of it here against an 820MB source tree. Packaging from it
means shipping whatever happens to be lying around.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import zipfile

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import releaseconfig  # noqa: E402

# Filename stems that mark third-party attribution. These ship whatever else
# does -- the LPC sprites and the CC-BY audio are used on terms that require
# the licence to travel with the binary. A game whose manifest misses one fails
# rather than shipping an unattributed asset.
#
# Matched case-insensitively against the whole filename. Upstream packs are not
# consistent: this repo carries LICENSE-upstream.txt, CREDITS.csv, license.txt
# and VT323-OFL.txt, and a case-sensitive glob finds three of the four on Linux
# while finding all four on Windows -- the worst kind of gate, one that passes
# locally and drops a licence in CI.
ATTRIBUTION_MARKERS = ("license", "credits", "-ofl", "copying", "attribution")

DEFAULT_SIZE_LIMIT_MB = 250


class PackageError(Exception):
    """Anything that should stop a release rather than ship a bad archive."""


# ---------------------------------------------------------------------------
# Manifest
# ---------------------------------------------------------------------------


def tracked_files(game_dir: pathlib.Path) -> set[pathlib.Path]:
    """Paths git tracks under this game, relative to it.

    The manifest is intersected with this, so what ships is bounded by what is
    in the repository. Globbing the working tree instead lets a release depend
    on the machine that built it: this game directory holds gitignored
    authoring trees, and matching `assets/characters/**/*.png` against the disk
    selected 412MB of them here while selecting 13MB on a clean checkout. A
    release that differs by builder is not a release.
    """
    try:
        done = subprocess.run(["git", "ls-files", "-z", "--", "."],
                              cwd=game_dir, capture_output=True, check=True)
    except (OSError, subprocess.CalledProcessError) as e:
        raise PackageError(f"git ls-files failed in {game_dir}: {e}") from e
    return {pathlib.Path(p) for p in done.stdout.decode().split("\0") if p}


def manifest_files(game_dir: pathlib.Path,
                   patterns: list[str]) -> dict[pathlib.Path, pathlib.Path]:
    """Resolve include patterns to {relative destination: absolute source}.

    A pattern naming a directory takes that whole tree, so `config` reads the
    way somebody writing the file expects rather than needing `config/**/*`.
    """
    tracked = tracked_files(game_dir)
    out: dict[pathlib.Path, pathlib.Path] = {}
    for pattern in patterns:
        direct = game_dir / pattern
        matches = direct.rglob("*") if direct.is_dir() else game_dir.glob(pattern)
        for src in matches:
            if not src.is_file():
                continue
            rel = src.relative_to(game_dir)
            if rel in tracked:
                out[rel] = src
    return out


def uncovered_attribution(game_dir: pathlib.Path,
                          covered: dict[pathlib.Path, pathlib.Path]) -> list[pathlib.Path]:
    """Attribution files under the game's assets that the manifest misses."""
    found = {p for p in tracked_files(game_dir)
             if p.parts and p.parts[0] == "assets"
             and any(m in p.name.lower() for m in ATTRIBUTION_MARKERS)}
    return sorted(found - set(covered))


# ---------------------------------------------------------------------------
# Build outputs
# ---------------------------------------------------------------------------


def find_exe(build_dir: pathlib.Path, exe_name: str) -> pathlib.Path:
    """Locate the built executable under build/bin.

    Searched rather than named. CMake writes each game to its own
    subdirectory, and a second copy of that path written down by hand is
    exactly what went stale and failed three releases after tagging.
    """
    matches = sorted((build_dir / "bin").rglob(exe_name))
    if not matches:
        raise PackageError(
            f"{exe_name} not found under {build_dir}/bin -- build the game first")
    return matches[0]


def _to_native(paths: list[str]) -> list[str]:
    """Convert MSYS/Cygwin paths to ones this Python can open.

    ldd reports `/mingw64/bin/libfoo.dll`, which the MinGW Python cannot see:
    it is a Windows process and that prefix only means something to the shell.
    cygpath is what knows the mapping.
    """
    if not paths or os.name != "nt":
        return paths
    try:
        done = subprocess.run(["cygpath", "-m", *paths],
                              capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return paths
    return [line for line in done.stdout.split("\n") if line.strip()]


def parse_ldd(output: str) -> list[str]:
    """Non-system DLL paths named in ldd output.

    Split out from the subprocess call so the filtering can be tested without a
    toolchain. System DLLs are dropped: they are on the target machine already,
    and copying them off this one is how you ship a build that only runs on the
    machine that made it.
    """
    raw: list[str] = []
    for line in output.split("\n"):
        if "=>" not in line or ".dll" not in line.lower():
            continue
        path = line.split("=>", 1)[1].strip().split(" (")[0].strip()
        low = path.lower().replace("\\", "/")
        if not path or "/windows/" in low:
            continue
        raw.append(path)
    return sorted(set(raw))


def dll_dependencies(exe: pathlib.Path) -> list[pathlib.Path]:
    """Non-system DLLs the executable loads, resolved to openable paths.

    Discovered rather than listed, so a new dependency ships without anybody
    remembering to add it.
    """
    try:
        done = subprocess.run(["ldd", str(exe)], capture_output=True,
                              text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as e:
        raise PackageError(f"ldd failed on {exe}: {e}") from e

    out: list[pathlib.Path] = []
    raw = parse_ldd(done.stdout)
    seen: set[str] = set()
    for native in _to_native(sorted(set(raw))):
        p = pathlib.Path(native)
        if p.is_file() and p.name.lower() not in seen:
            seen.add(p.name.lower())
            out.append(p)
    return out


def cache_values(build_dir: pathlib.Path) -> dict[str, str]:
    """Settings recorded in CMakeCache.txt, as {name: value}."""
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        raise PackageError(f"no CMakeCache.txt in {build_dir} -- configure the build first")
    out: dict[str, str] = {}
    for line in cache.read_text(encoding="utf-8", errors="replace").split("\n"):
        line = line.strip()
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        name, _, value = line.partition("=")
        out[name.split(":", 1)[0]] = value
    return out


def verify_build_config(build_dir: pathlib.Path, required: list[str]) -> None:
    """Refuse to package a build configured the wrong way.

    Some games need a setting to produce a shippable executable at all --
    wayworn-hush compiles an absolute chdir to the BUILD MACHINE'S source tree
    unless WAYWORN_RELEASE is on, which works everywhere except somebody
    else's computer. The old per-game script knew that and configured its own
    build; a general packager cannot know it, so the game declares it and this
    checks it against what the build directory actually recorded.
    """
    if not required:
        return
    cache = cache_values(build_dir)
    for item in required:
        name, _, want = item.partition("=")
        name, want = name.strip(), want.strip()
        got = cache.get(name)
        if got is None:
            raise PackageError(
                f"{build_dir}/CMakeCache.txt has no {name}; the release config "
                f"requires {item}. Reconfigure with -D{item}.")
        if got.strip().lower() != want.lower():
            raise PackageError(
                f"{name} is {got!r} but this game requires {want!r} to build a "
                f"shippable executable. Reconfigure with -D{item}.")


def read_version(game_dir: pathlib.Path) -> str:
    """The scope's version, from its own project() declaration."""
    cml = game_dir / "CMakeLists.txt"
    if not cml.is_file():
        raise PackageError(f"missing {cml}")
    m = re.search(r"project\([^)]*VERSION\s+(\d+\.\d+\.\d+)",
                  cml.read_text(encoding="utf-8"), re.S)
    if not m:
        raise PackageError(f"no project(... VERSION x.y.z) in {cml}")
    return m.group(1)


# ---------------------------------------------------------------------------
# Packaging
# ---------------------------------------------------------------------------


def _fmt_mb(n: float) -> str:
    return f"{n / (1024 * 1024):.1f} MB"


def package(scope_dir: str, build_dir: pathlib.Path, out_name: str | None = None,
            staging_root: pathlib.Path | None = None) -> pathlib.Path:
    game_dir = (REPO_ROOT / scope_dir).resolve()
    cfg = releaseconfig.load(game_dir)

    exe_name = str(cfg["cmake_target"]) + ".exe"
    prefix = str(cfg.get("artifact_prefix") or cfg["cmake_target"])
    include = cfg.get("include")
    if not isinstance(include, list) or not include:
        raise PackageError(
            f"{scope_dir}/.release-config.yml declares no `include:` list. "
            "Packaging is inclusion-only; nothing ships until it is listed.")
    limit_mb = float(cfg.get("size_limit_mb") or DEFAULT_SIZE_LIMIT_MB)

    name = out_name or f"{prefix}-v{read_version(game_dir)}"
    staging_root = staging_root or REPO_ROOT
    stage = staging_root / name

    data = manifest_files(game_dir, include)
    if not data:
        raise PackageError(f"`include:` matched no files under {game_dir}")

    missing = uncovered_attribution(game_dir, data)
    if missing:
        raise PackageError(
            "attribution files are not covered by `include:` and would not "
            "ship:\n  " + "\n  ".join(str(p) for p in missing))

    required = cfg.get("cmake_args")
    verify_build_config(build_dir, required if isinstance(required, list) else [])

    exe = find_exe(build_dir, exe_name)
    dlls = dll_dependencies(exe)

    # The ceiling measures DECLARED DATA, not the executable. An exe is 165MB
    # in a Debug build and a fraction of that in Release, so counting it would
    # make the same tree pass or fail on build type -- a gate that moves for
    # reasons unrelated to what it is watching gets raised reflexively and then
    # watches nothing. What this is for is the manifest change that quietly
    # doubles the download.
    payload = sum(src.stat().st_size for src in data.values())
    binaries = exe.stat().st_size + sum(d.stat().st_size for d in dlls)
    if payload > limit_mb * 1024 * 1024:
        biggest = sorted(data.items(), key=lambda kv: kv[1].stat().st_size,
                         reverse=True)[:10]
        listing = "\n  ".join(f"{_fmt_mb(s.stat().st_size):>10}  {r}"
                              for r, s in biggest)
        raise PackageError(
            f"payload is {_fmt_mb(payload)}, over the {limit_mb:g} MB ceiling "
            f"in {scope_dir}/.release-config.yml.\nLargest included files:\n"
            f"  {listing}\nEither the manifest picked up something it should "
            "not, or raise the ceiling deliberately.")

    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    shutil.copy2(exe, stage / f"{name}.exe")
    for dll in dlls:
        shutil.copy2(dll, stage / dll.name)
    for rel, src in sorted(data.items()):
        dst = stage / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    archive = staging_root / f"{name}.zip"
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                zf.write(path, path.relative_to(staging_root))

    if not archive.is_file() or archive.stat().st_size == 0:
        raise PackageError(f"{archive.name} was not written; staging left at {stage}")
    shutil.rmtree(stage)

    print(f"{name}.zip  {_fmt_mb(archive.stat().st_size)} archived")
    print(f"  data     {_fmt_mb(payload)} in {len(data)} files "
          f"(ceiling {limit_mb:g} MB)")
    print(f"  binaries {_fmt_mb(binaries)}: {exe.name} + {len(dlls)} DLL(s)")
    return archive


def main() -> int:
    ap = argparse.ArgumentParser(description="Build a game's release archive.")
    ap.add_argument("--scope-dir", required=True, help="e.g. games/selva-oscura")
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--name", default=None, help="archive name without .zip")
    args = ap.parse_args()

    build = pathlib.Path(args.build_dir)
    if not build.is_absolute():
        build = REPO_ROOT / build
    try:
        package(args.scope_dir, build, args.name)
    except (PackageError, releaseconfig.ConfigError) as e:
        print(f"::error::{e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
