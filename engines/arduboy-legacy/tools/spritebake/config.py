"""TOML manifest — declarative spec of what a project bakes.

A manifest describes:
  - where the C output goes (file + layout + progmem qualifier)
  - pipelines by name
  - sprites: name, source image (possibly sliced), target size, pipeline

Example (games/rpg/sprites.toml):

    [output]
    cpp = "games/rpg/sprites.cpp"
    layout = "col-major-topbit0"
    progmem = true

    [pipelines.default_boss]
    stages = [
      "autocrop",
      { gaussian_blur = { radius_ratio = 0.5 } },
      { aspect_fit = { width = 16, height = 16 } },
      { downscale = { width = 16, height = 16, method = "lanczos" } },
      { threshold_otsu = {} },
      { morph_close_holes = {} },
      { vert_feature_boost = {} },
    ]

    [[sprites]]
    ident = "boss_lucifer_data"
    source = "art/lucifer.png"
    width = 20
    height = 24
    pipeline = "default_boss"

    [[sprites]]
    ident = "boss_charon_data"
    source = "art/bosses.png"
    slice = { cols = 4, rows = 2, index = 0, trim_top_ratio = 0.22 }
    width = 16
    height = 16
    pipeline = "default_boss"

Pipeline stages with per-sprite overrides can be written as a list in
the sprite entry itself (replaces the named pipeline).
"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

if sys.version_info >= (3, 11):
    import tomllib
else:
    try:
        import tomli as tomllib  # type: ignore
    except ImportError as e:  # pragma: no cover
        raise ImportError(
            "spritebake needs TOML: Python 3.11+ (stdlib tomllib) or "
            "`pip install tomli`"
        ) from e

from .pipeline import Pipeline, StageSpec


@dataclass
class OutputSpec:
    cpp: Path
    layout: str = "col-major-topbit0"
    progmem: bool = True


@dataclass
class SliceSpec:
    cols: int
    rows: int
    index: int
    trim_top_ratio: float = 0.0
    trim_bottom_ratio: float = 0.0
    trim_left_ratio: float = 0.0
    trim_right_ratio: float = 0.0
    autocrop_to_figure: bool = False
    autocrop_threshold: int = 96
    portrait_ratio: float | None = None


@dataclass
class BboxSpec:
    """Explicit pixel rectangle (x0, y0, x1, y1) into the source image,
    for sheets where the figures are NOT on a uniform grid (variable cell
    widths, label text encroaching, hand-laid layouts). Mutually exclusive
    with `slice`.
    """
    x0: int
    y0: int
    x1: int
    y1: int
    portrait_ratio: float | None = None


@dataclass
class SpriteSpec:
    ident: str
    source: Path
    width: int
    height: int
    pipeline: str | None = None
    stages: list[StageSpec] | None = None   # inline override
    slice: SliceSpec | None = None
    bbox: BboxSpec | None = None
    portrait_ratio: float | None = None   # for non-sliced sources (Lucifer)
    fade_bottom: bool = True   # append spirit_fade_bottom as a post-stage.
                               # Off for figures that end in a real hard edge
                               # (scythe shaft, flame base) — their silhouette
                               # should not dissolve into pitch.

    def resolve_pipeline(self, pipelines: dict[str, list[StageSpec]]) -> Pipeline:
        if self.stages is not None:
            specs = self.stages
            name = f"{self.ident}:inline"
        else:
            if not self.pipeline:
                raise ValueError(f"sprite {self.ident!r} has no pipeline and no inline stages")
            if self.pipeline not in pipelines:
                raise KeyError(f"sprite {self.ident!r} refers to missing pipeline {self.pipeline!r}")
            specs = pipelines[self.pipeline]
            name = self.pipeline
        # Bind target width/height to any stage that declares width/height
        # placeholders (marked with the sentinel value 0). This lets a pipeline
        # be shared across sprites of different sizes without duplicating
        # width/height in every stage.
        bound: list[StageSpec] = []
        for s in specs:
            new_kwargs = dict(s.kwargs)
            for k in list(new_kwargs.keys()):
                if new_kwargs[k] == "$width":
                    new_kwargs[k] = self.width
                elif new_kwargs[k] == "$height":
                    new_kwargs[k] = self.height
            bound.append(StageSpec(name=s.name, kwargs=new_kwargs))
        # Per-sprite bottom fade is a post-stage, not a pipeline concern —
        # the same pipeline can serve fading and non-fading sprites.
        if self.fade_bottom:
            bound.append(StageSpec(
                name="spirit_fade_bottom",
                kwargs={"fade_ratio": 0.28, "top_keep": 0.90, "bottom_keep": 0.15},
            ))
        return Pipeline(bound, name=f"{name}@{self.ident}")


@dataclass
class Manifest:
    output: OutputSpec
    pipelines: dict[str, list[StageSpec]] = field(default_factory=dict)
    sprites: list[SpriteSpec] = field(default_factory=list)
    manifest_path: Path | None = None

    @property
    def project_root(self) -> Path:
        """Paths in the manifest are relative to its location."""
        if self.manifest_path is None:
            return Path.cwd()
        return self.manifest_path.parent

    def resolve_source(self, rel_path: Path | str) -> Path:
        p = Path(rel_path)
        if p.is_absolute():
            return p
        # Relative paths resolve against the repo root (grandparent of
        # games/*/sprites.toml typically), which is passed in explicitly.
        return (self.project_root / p).resolve()


def load_manifest(path: Path | str, *, repo_root: Path | str | None = None) -> Manifest:
    path = Path(path)
    with open(path, "rb") as f:
        data = tomllib.load(f)

    out_raw = data.get("output", {})
    cpp_rel = out_raw.get("cpp")
    if not cpp_rel:
        raise ValueError("manifest: [output].cpp is required")
    cpp_path = Path(cpp_rel)
    if not cpp_path.is_absolute():
        root = Path(repo_root) if repo_root else path.parent
        cpp_path = (root / cpp_path).resolve()
    out = OutputSpec(
        cpp=cpp_path,
        layout=out_raw.get("layout", "col-major-topbit0"),
        progmem=bool(out_raw.get("progmem", True)),
    )

    pipelines: dict[str, list[StageSpec]] = {}
    for name, pdata in (data.get("pipelines") or {}).items():
        stages_raw = pdata.get("stages") or []
        pipelines[name] = [StageSpec.from_any(s) for s in stages_raw]

    sprites: list[SpriteSpec] = []
    for s in data.get("sprites") or []:
        source_rel = s["source"]
        root = Path(repo_root) if repo_root else path.parent
        source = (root / source_rel).resolve() if not Path(source_rel).is_absolute() else Path(source_rel)
        slc = None
        if "slice" in s:
            sl = s["slice"]
            slc = SliceSpec(
                cols=int(sl["cols"]),
                rows=int(sl["rows"]),
                index=int(sl["index"]),
                trim_top_ratio=float(sl.get("trim_top_ratio", 0.0)),
                trim_bottom_ratio=float(sl.get("trim_bottom_ratio", 0.0)),
                trim_left_ratio=float(sl.get("trim_left_ratio", 0.0)),
                trim_right_ratio=float(sl.get("trim_right_ratio", 0.0)),
                autocrop_to_figure=bool(sl.get("autocrop_to_figure", False)),
                autocrop_threshold=int(sl.get("autocrop_threshold", 96)),
                portrait_ratio=(float(sl["portrait_ratio"]) if "portrait_ratio" in sl else None),
            )
        stages = None
        if "stages" in s:
            stages = [StageSpec.from_any(x) for x in s["stages"]]
        bbox = None
        if "bbox" in s:
            bb = s["bbox"]
            bbox = BboxSpec(
                x0=int(bb["x0"]), y0=int(bb["y0"]),
                x1=int(bb["x1"]), y1=int(bb["y1"]),
                portrait_ratio=(float(bb["portrait_ratio"]) if "portrait_ratio" in bb else None),
            )
        if bbox is not None and slc is not None:
            raise ValueError(f"sprite {s['ident']!r}: cannot set both `slice` and `bbox`")
        sprites.append(SpriteSpec(
            ident=s["ident"],
            source=source,
            width=int(s["width"]),
            height=int(s["height"]),
            pipeline=s.get("pipeline"),
            stages=stages,
            slice=slc,
            bbox=bbox,
            portrait_ratio=(float(s["portrait_ratio"]) if "portrait_ratio" in s else None),
            fade_bottom=bool(s.get("fade_bottom", True)),
        ))

    return Manifest(
        output=out,
        pipelines=pipelines,
        sprites=sprites,
        manifest_path=path,
    )
