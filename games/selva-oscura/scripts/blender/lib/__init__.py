# Shared Blender authoring library for selva-oscura gen_*.py scripts.
#
# Import pattern (from any gen_*.py that lives in the parent
# scripts/blender directory):
#
#     import os
#     import sys
#     sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
#     from lib.mesh_primitives import (
#         current_collection, reset_scene, select_only, make_material,
#         add_box, add_merged_boxes, add_mesh_from_pydata,
#         boolean_difference,
#     )
