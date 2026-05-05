"""Pipeline engine — pure-function stages applied in order to a SpriteData.

A stage is a callable with signature `(sprite, **kwargs) -> SpriteData`.
Stages are registered in STAGES by name and referenced declaratively
from the sprite manifest (TOML).

The pipeline's source-of-truth lives in the manifest; this module is
just composition + registry. Individual stage implementations live in
`tools.spritebake.stages.*`.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable

from .core import SpriteData

Stage = Callable[..., SpriteData]
STAGES: dict[str, Stage] = {}


def register_stage(name: str) -> Callable[[Stage], Stage]:
    """Decorator: register a stage function under the given name."""
    def deco(fn: Stage) -> Stage:
        if name in STAGES:
            raise RuntimeError(f"stage {name!r} already registered "
                               f"(by {STAGES[name].__module__}.{STAGES[name].__name__})")
        STAGES[name] = fn
        return fn
    return deco


@dataclass
class StageSpec:
    """A single stage invocation from a manifest."""
    name: str
    kwargs: dict[str, Any]

    @classmethod
    def from_any(cls, item: Any) -> "StageSpec":
        """Parse a manifest entry into a StageSpec.

        Accepts:
          - "stage_name"                 → StageSpec(name, {})
          - {"stage_name": {kw: val}}    → StageSpec(name, {kw: val})
          - {"name": "stage", "kw": val} → StageSpec("stage", {"kw": val})
        """
        if isinstance(item, str):
            return cls(name=item, kwargs={})
        if isinstance(item, dict):
            if "name" in item:
                d = dict(item)
                name = d.pop("name")
                return cls(name=name, kwargs=d)
            if len(item) == 1:
                name, kwargs = next(iter(item.items()))
                kwargs = dict(kwargs) if kwargs else {}
                return cls(name=name, kwargs=kwargs)
            raise ValueError(f"bad stage entry {item!r}")
        raise TypeError(f"unsupported stage entry type {type(item).__name__}: {item!r}")


class Pipeline:
    """An ordered list of stages. Each stage is pure; the pipeline is composable."""

    def __init__(self, specs: list[StageSpec] | None = None, *, name: str = "<anon>"):
        self.specs = list(specs or [])
        self.name = name

    @classmethod
    def from_list(cls, items: list[Any], *, name: str = "<anon>") -> "Pipeline":
        specs = [StageSpec.from_any(x) for x in items]
        return cls(specs, name=name)

    def append(self, stage: str | StageSpec, **kwargs) -> "Pipeline":
        if isinstance(stage, StageSpec):
            self.specs.append(stage)
        else:
            self.specs.append(StageSpec(name=stage, kwargs=kwargs))
        return self

    def run(self, sprite: SpriteData, *,
            trace: list[SpriteData] | None = None) -> SpriteData:
        """Execute every stage in order. If `trace` is provided, each
        intermediate SpriteData is appended (useful for preview/debug)."""
        current = sprite
        for spec in self.specs:
            if spec.name not in STAGES:
                raise KeyError(
                    f"unknown pipeline stage {spec.name!r} — known: {sorted(STAGES)}"
                )
            fn = STAGES[spec.name]
            current = fn(current, **spec.kwargs)
            if trace is not None:
                trace.append(current)
        return current

    def __repr__(self) -> str:
        stage_names = ", ".join(s.name for s in self.specs)
        return f"Pipeline({self.name!r}: [{stage_names}])"


def list_stages() -> list[tuple[str, str]]:
    """Return (name, docstring_first_line) for all registered stages."""
    out = []
    for n, fn in sorted(STAGES.items()):
        doc = (fn.__doc__ or "").strip().splitlines()
        first = doc[0] if doc else ""
        out.append((n, first))
    return out
