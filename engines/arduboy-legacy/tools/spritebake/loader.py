"""Source loading — bring images into SpriteData for the pipeline.

Handles the "art/ → SpriteData" edge. Supports:
  - whole-image load (one file = one sprite)
  - grid slicing (one file = NxM sprites, picked by index)
  - raw-array paste (for editor round-tripping)
  - existing-sprite.cpp decoding (for re-editing baked sprites)
"""

from __future__ import annotations

from pathlib import Path
from typing import Iterable

import numpy as np

try:
    from PIL import Image
except ImportError as e:  # pragma: no cover
    raise ImportError("spritebake requires Pillow") from e

from .core import SpriteData


def load_image(path: Path | str, *,
               portrait_ratio: float | None = None) -> SpriteData:
    """Load a whole image file as a grayscale-decorated SpriteData.

    portrait_ratio: if set, keeps only the top `portrait_ratio` of the
    image (applied before any pipeline stage runs). Same semantic as in
    load_image_cell.
    """
    img = Image.open(path).convert("L")
    gray = np.array(img, dtype=np.float32)
    if portrait_ratio is not None and 0.0 < portrait_ratio < 1.0:
        gh = gray.shape[0]
        new_h = max(1, int(gh * portrait_ratio))
        gray = gray[:new_h, :]
    return SpriteData(pixels=gray >= 128, meta={"gray": gray, "source": str(path)})


def load_image_cell(path: Path | str, *,
                    cols: int, rows: int, index: int,
                    trim_top_ratio: float = 0.0,
                    trim_bottom_ratio: float = 0.0,
                    trim_left_ratio: float = 0.0,
                    trim_right_ratio: float = 0.0,
                    autocrop_to_figure: bool = False,
                    autocrop_threshold: int = 96,
                    portrait_ratio: float | None = None) -> SpriteData:
    """Load one cell from a regular NxM grid image.

    Cells are numbered row-major, 0 = top-left. trim_*_ratio chops that
    fraction off each side of the cell before returning, useful for
    removing label text above/beside figures in spritesheets.
    """
    img = Image.open(path).convert("L")
    W, H = img.size
    cw, ch = W // cols, H // rows
    r = index // cols
    c = index % cols
    # When autocrop_to_figure is on, extend the slice horizontally into
    # neighbour cells by ~25% of cell width on each side. Figures in
    # bosses.png aren't perfectly centered — several spill past their
    # nominal cell boundary (Cerberus's left head, Plutus's arm, etc.).
    # Autocrop then isolates the correct figure from the widened frame.
    # Text-band rejection + biggest-component selection keeps it from
    # latching onto a neighbour.
    margin = int(cw * 0.25) if autocrop_to_figure else 0
    x0 = max(0, c * cw - margin)
    x1 = min(W, (c + 1) * cw + margin)
    cell = img.crop((x0, r * ch, x1, (r + 1) * ch))
    cw2, ch2 = cell.size
    top = int(ch2 * trim_top_ratio)
    bottom = ch2 - int(ch2 * trim_bottom_ratio)
    left = int(cw2 * trim_left_ratio)
    right = cw2 - int(cw2 * trim_right_ratio)
    cell = cell.crop((left, top, right, bottom))
    gray = np.array(cell, dtype=np.float32)
    if autocrop_to_figure:
        # Isolate the figure's silhouette, then crop to its bbox.
        #
        # Two failure modes to defeat:
        #   (a) bosses with dithered/hatched interiors — lit pixels form
        #       many small components, so "largest connected component"
        #       picks one interior fragment instead of the whole body.
        #   (b) label text above the figure — thin but wide, can win a
        #       pixel-count or bbox-area contest if the figure is small.
        #
        # Approach: dilate the lit mask so near-touching interior pieces
        # merge into one silhouette blob, then score components by
        # (bbox area × tallness), and — because the figure is always
        # below the label — reject any component whose bbox sits entirely
        # in the top 25% of the cell (that's the text band).
        lit = gray >= autocrop_threshold
        if lit.any():
            h, w = lit.shape
            # Morphological dilation — Chebyshev distance radius, 3x3 kernel,
            # repeated to close ~6px gaps between hatched strokes. numpy-only
            # so no SciPy dependency.
            dilated = lit.copy()
            for _ in range(5):
                padded = np.pad(dilated, 1, mode="constant", constant_values=False)
                grown = padded[:-2, :-2] | padded[:-2, 1:-1] | padded[:-2, 2:] | \
                        padded[1:-1, :-2] | padded[1:-1, 1:-1] | padded[1:-1, 2:] | \
                        padded[2:, :-2] | padded[2:, 1:-1] | padded[2:, 2:]
                dilated = grown
            visited = np.zeros_like(dilated)
            best_bbox: tuple[int, int, int, int] | None = None
            best_score = -1.0
            text_band_max_y = int(h * 0.25)
            # Nominal cell-center in the (possibly widened) frame. When
            # slicing widened the frame by `margin` px on each side, the
            # figure we want has its x-center at `margin + cw/2`.
            nominal_cx = (cw // 2) + (margin if autocrop_to_figure else 0)
            for sy in range(h):
                for sx in range(w):
                    if not dilated[sy, sx] or visited[sy, sx]:
                        continue
                    stack = [(sy, sx)]
                    ymin = ymax = sy
                    xmin = xmax = sx
                    while stack:
                        cy, cx = stack.pop()
                        if cy < 0 or cx < 0 or cy >= h or cx >= w:
                            continue
                        if visited[cy, cx] or not dilated[cy, cx]:
                            continue
                        visited[cy, cx] = True
                        if cy < ymin: ymin = cy
                        if cy > ymax: ymax = cy
                        if cx < xmin: xmin = cx
                        if cx > xmax: xmax = cx
                        stack.extend([(cy+1, cx), (cy-1, cx),
                                       (cy, cx+1), (cy, cx-1)])
                    bh = ymax - ymin + 1
                    bw2 = xmax - xmin + 1
                    # Reject components that live entirely in the top
                    # text band — those are the "19 BOSS_MINOS" labels.
                    if ymax < text_band_max_y:
                        continue
                    # Reject tiny specks (<1% of cell area).
                    if bh * bw2 < (h * w) * 0.01:
                        continue
                    aspect_penalty = max(0.3, min(1.0, bh / max(1, bw2)))
                    # Centrality bonus: figures in the correct cell have
                    # their x-center near nominal_cx. Penalise components
                    # whose center is far from the nominal cell-center,
                    # dropping to 0.2× at a full cell-width offset.
                    comp_cx = (xmin + xmax) / 2
                    offset = abs(comp_cx - nominal_cx) / max(1, cw)
                    centrality = max(0.2, 1.0 - offset)
                    score = bh * bw2 * aspect_penalty * centrality
                    if score > best_score:
                        best_score = score
                        best_bbox = (ymin, ymax + 1, xmin, xmax + 1)
            if best_bbox is not None:
                y0, y1, x0, x1 = best_bbox
                gray = gray[y0:y1, x0:x1]
    # Portrait crop — keep only the top `portrait_ratio` of the figure
    # (e.g. 0.60 = head + shoulders, drop waist/legs). Applied AFTER
    # autocrop-to-figure so the ratio is measured against the silhouette,
    # not the raw cell. The spirit-fade stage later dissolves the bottom
    # edge so the crop doesn't look amputated.
    if portrait_ratio is not None and 0.0 < portrait_ratio < 1.0:
        gh = gray.shape[0]
        new_h = max(1, int(gh * portrait_ratio))
        gray = gray[:new_h, :]
    return SpriteData(pixels=gray >= 128,
                      meta={"gray": gray, "source": f"{path}#[{c},{r}]"})


def load_image_bbox(path: Path | str, *,
                    x0: int, y0: int, x1: int, y1: int,
                    portrait_ratio: float | None = None) -> SpriteData:
    """Load a fixed pixel rectangle from `path`. Use when a sprite sheet's
    figures are NOT on a uniform grid (variable cell widths, label text
    encroaching on cell boundaries) — `load_image_cell` assumes uniform
    cells and `autocrop_to_figure` can't always recover from that. With
    explicit bbox the caller has named the figure region directly.
    """
    img = Image.open(path).convert("L")
    cell = img.crop((x0, y0, x1, y1))
    gray = np.array(cell, dtype=np.float32)
    if portrait_ratio is not None and 0.0 < portrait_ratio < 1.0:
        gh = gray.shape[0]
        new_h = max(1, int(gh * portrait_ratio))
        gray = gray[:new_h, :]
    return SpriteData(pixels=gray >= 128,
                      meta={"gray": gray,
                            "source": f"{path}#bbox[{x0},{y0},{x1},{y1}]"})


def load_bytes(data: Iterable[int], width: int, height: int,
               layout: str = "col-major-topbit0") -> SpriteData:
    """Decode a packed byte list back into a SpriteData (e.g. from the
    editor's "Export C array")."""
    from .encode import decode
    return decode(list(data), width, height, layout)