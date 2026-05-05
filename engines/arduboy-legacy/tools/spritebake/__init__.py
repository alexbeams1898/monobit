"""spritebake — 1-bit sprite tooling for the mono engine.

Converts source artwork into PROGMEM-packed byte arrays and patches them
into per-game C++ sprite tables. Designed to be the long-lived asset-bake
component for the engine, reusable across future monobit titles.

Public API:
    from tools.spritebake import SpriteData, Pipeline, bake_manifest
"""

from .core import SpriteData
from .pipeline import Pipeline, register_stage, STAGES
from .encode import encode, decode, LAYOUTS
from .config import Manifest, load_manifest
from . import patch

__all__ = [
    "SpriteData",
    "Pipeline",
    "register_stage",
    "STAGES",
    "encode",
    "decode",
    "LAYOUTS",
    "Manifest",
    "load_manifest",
    "patch",
]

__version__ = "0.1.0"