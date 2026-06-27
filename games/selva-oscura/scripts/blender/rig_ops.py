"""Shared rig manipulation helpers for the humanoid pipeline.

Used by export_humanoid_glb.py and retarget_mixamo.py to keep the rig
preparation invariants (DEF-armature discovery, face reparent) in one
place. Both consumers MUST apply the same transformations or the
exported skeleton and clip .ozz files will disagree at runtime.
"""

import bpy


def find_def_armature(blend_objects=None):
    """Return the first ARMATURE with at least one DEF- bone, or None.

    First-armature heuristic would pick CharMorph's metarig or other
    leftovers; the DEF- prefix disambiguates the Rigify deform set.
    """
    objs = blend_objects if blend_objects is not None else bpy.data.objects
    return next(
        (o for o in objs
         if o.type == "ARMATURE" and any(b.name.startswith("DEF-") for b in o.data.bones)),
        None)


def reparent_face_to_skull(armature) -> bool:
    """Reparent ORG-face -> DEF-spine.006. Returns True if reparented.

    Rigify ties ORG-face to the static ORG-spine.006; we drive the
    parallel DEF- chain directly, so without this the face mesh stays
    frozen while the skull rotates (elongated-head bug).
    """
    bones = armature.data.bones
    if not (bones.get("ORG-face") and bones.get("DEF-spine.006")):
        return False
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode="EDIT")
    armature.data.edit_bones["ORG-face"].parent = armature.data.edit_bones["DEF-spine.006"]
    bpy.ops.object.mode_set(mode="OBJECT")
    return True
