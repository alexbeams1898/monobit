"""
Generate config/characters/sliders.json entries for the primary +
secondary tier axes over the curated MPFB2 morph set.

Schema (matches the existing loader):
  bone_scale / bone_scale_pair / uniform_scale: keep existing entries
  morph_pair:           param_decr + param_incr (single side)
  morph_pair_mirrored:  param_decr = "l-*,r-*"  param_incr = "l-*,r-*"
  morph_single:         param (one directional morph, min=0 max=1)

Emits JSON in the same file order as the current sliders.json but
with the new axes appended after the existing entries.
"""

"""
Note: This script is invoked as `python3 scripts/regen_sliders.py`
from `games/selva-oscura/`. It writes the rendered JSON directly to
`config/characters/sliders.json` (relative to the script's parent
dir), so the next runtime load picks up any edits.
"""
import json
from pathlib import Path

SELVA = Path(__file__).resolve().parents[1]  # games/selva-oscura
SLIDERS = SELVA / "config/characters/sliders.json"

# ---- Existing bone-scale + macro entries (kept verbatim) ----
EXISTING = [
    {"id": "head_scale", "label": "Head", "category": "proportions",
     "min": 0.5, "max": 2.5, "default": 1.0,
     "applies_to": "bone_scale", "param": "mixamorig:Head", "player_visible": True},
    {"id": "torso_scale", "label": "Torso", "category": "proportions",
     "min": 0.5, "max": 2.0, "default": 1.0,
     "applies_to": "bone_scale", "param": "mixamorig:Spine", "player_visible": True},
    {"id": "arm_scale", "label": "Arms", "category": "proportions",
     "min": 0.5, "max": 2.0, "default": 1.0,
     "applies_to": "bone_scale_pair", "param": "mixamorig:LeftArm,mixamorig:RightArm",
     "player_visible": True},
    {"id": "leg_scale", "label": "Legs", "category": "proportions",
     "min": 0.5, "max": 2.0, "default": 1.0,
     "applies_to": "bone_scale_pair", "param": "mixamorig:LeftUpLeg,mixamorig:RightUpLeg",
     "player_visible": True},
    {"id": "body_scale", "label": "Body Scale", "category": "proportions",
     "min": 0.5, "max": 1.5, "default": 1.0,
     "applies_to": "uniform_scale", "param": "", "player_visible": False,
     "_comment": "Dev-only -- the player creator excludes size knobs; size emerges from gameplay state (class, stats, evolution) per the canon. Authoring + NPC config (larva_fresh body_scale=0.60) use it freely."},
]

# ---- Axis table ----
#
# Each entry:
#   (id, label, category, kind, params, player_visible)
#
# `kind` = pair | mirrored | single | one_way
# `params` = for `pair`:      ("morph-decr", "morph-incr")
#            for `mirrored`:  ("morph-base",)  -- expanded to l-/r-decr and l-/r-incr
#            for `single`:    ("morph-name",)  -- min=0, max=1
#            for `one_way`:   ("morph-name",)  -- min=0, max=1 (single directional target)

# ============ PRIMARY TIER ============
PRIMARY = [
    # ---- Face structure (head shape) ----
    ("face_width",        "Face width",         "face_structure", "pair",     ("head-scale-horiz-decr", "head-scale-horiz-incr")),
    ("face_length",       "Face length",        "face_structure", "pair",     ("head-scale-vert-decr",  "head-scale-vert-incr")),
    ("face_depth",        "Face depth",         "face_structure", "pair",     ("head-scale-depth-decr", "head-scale-depth-incr")),
    ("head_fat",          "Head fat",           "face_structure", "pair",     ("head-fat-decr",         "head-fat-incr")),
    ("head_age",          "Age",                "face_structure", "pair",     ("head-age-decr",         "head-age-incr")),
    ("head_round",        "Round shape",        "face_structure", "one_way",  ("head-round",)),
    ("head_square",       "Square shape",       "face_structure", "one_way",  ("head-square",)),
    ("head_oval",         "Oval shape",         "face_structure", "one_way",  ("head-oval",)),
    ("head_triangular",   "Triangular shape",   "face_structure", "one_way",  ("head-triangular",)),

    # ---- Eyes ----
    ("eye_size",          "Eye size",           "eyes",           "mirrored", ("eye-scale",)),
    ("eye_spacing",       "Eye spacing",        "eyes",           "mirrored", ("eye-trans-in-out",)),  # custom
    ("eye_height",        "Eye height",         "eyes",           "mirrored", ("eye-trans-up-down",)), # custom
    ("eye_eyefold_depth", "Eyefold depth",      "eyes",           "mirrored", ("eye-eyefold-concave-convex",)),  # custom
    ("eye_corner_tilt",   "Outer corner tilt",  "eyes",           "mirrored", ("eye-corner1-up-down",)),  # custom
    ("eye_epicanthus",    "Inner corner (epicanthus)", "eyes",    "mirrored", ("eye-epicanthus-in-out",)),  # custom
    ("eye_lid_height",    "Lid height",         "eyes",           "mirrored", ("eye-height2",)),  # custom

    # ---- Brow ----
    ("brow_height",       "Brow height",        "brow",           "pair",     ("eyebrows-trans-down",   "eyebrows-trans-up")),
    ("brow_angle",        "Brow angle",         "brow",           "pair",     ("eyebrows-angle-down",   "eyebrows-angle-up")),
    ("forehead_height",   "Forehead height",    "brow",           "pair",     ("forehead-scale-vert-decr", "forehead-scale-vert-incr")),

    # ---- Nose ----
    ("nose_size",         "Nose size",          "nose",           "pair",     ("nose-volume-decr",      "nose-volume-incr")),
    ("nose_width",        "Nose width",         "nose",           "pair",     ("nose-width2-decr",      "nose-width2-incr")),
    ("nose_length",       "Nose length",        "nose",           "pair",     ("nose-scale-vert-decr",  "nose-scale-vert-incr")),
    ("nose_scale_horiz",  "Nose scale (horizontal)", "nose",      "pair",     ("nose-scale-horiz-decr", "nose-scale-horiz-incr")),
    ("nose_hump",         "Nose hump",          "nose",           "pair",     ("nose-hump-decr",        "nose-hump-incr")),
    ("nose_curve",        "Nose curve",         "nose",           "pair",     ("nose-curve-concave",    "nose-curve-convex")),
    ("nose_flaring",      "Nostril flare",      "nose",           "pair",     ("nose-flaring-decr",     "nose-flaring-incr")),
    ("nose_point",        "Nose point tilt",    "nose",           "pair",     ("nose-point-down",       "nose-point-up")),

    # ---- Mouth ----
    ("mouth_width",       "Mouth width",        "mouth",          "pair",     ("mouth-scale-horiz-decr", "mouth-scale-horiz-incr")),
    ("mouth_height",      "Mouth height",       "mouth",          "pair",     ("mouth-trans-down",      "mouth-trans-up")),
    ("upperlip_volume",   "Upper lip volume",   "mouth",          "pair",     ("mouth-upperlip-volume-decr", "mouth-upperlip-volume-incr")),
    ("lowerlip_volume",   "Lower lip volume",   "mouth",          "pair",     ("mouth-lowerlip-volume-decr", "mouth-lowerlip-volume-incr")),
    ("mouth_cupidsbow",   "Cupid's bow",        "mouth",          "pair",     ("mouth-cupidsbow-decr",  "mouth-cupidsbow-incr")),
    ("mouth_corners",     "Mouth corners",      "mouth",          "pair",     ("mouth-angles-down",     "mouth-angles-up")),

    # ---- Cheek ----
    ("cheekbones",        "Cheekbones",         "cheek",          "mirrored", ("cheek-bones",)),
    ("cheek_volume",      "Cheek volume",       "cheek",          "mirrored", ("cheek-volume",)),
    ("cheek_hollow",      "Cheek hollow",       "cheek",          "mirrored", ("cheek-inner",)),

    # ---- Chin & jaw ----
    ("chin_prominence",   "Chin prominence",    "chin",           "pair",     ("chin-prominent-decr",   "chin-prominent-incr")),
    ("chin_width",        "Chin width",         "chin",           "pair",     ("chin-width-decr",       "chin-width-incr")),
    ("chin_height",       "Chin height",        "chin",           "pair",     ("chin-height-decr",      "chin-height-incr")),
    ("chin_bones",        "Jaw bones",          "chin",           "pair",     ("chin-bones-decr",       "chin-bones-incr")),

    # ---- Ears ----
    ("ear_scale",         "Ear size",           "ears",           "mirrored", ("ear-scale",)),
    ("ear_pointed",       "Pointed ears",       "ears",           "mirrored", ("ear-shape-pointed", "one_way")),

    # ---- Body (extends the existing bone_scale entries) ----
    # Nothing new here; existing bone_scale sliders cover the primary body axes.
]

# ============ SECONDARY TIER ============
# Fine-grain axes; player_visible=False.
SECONDARY = [
    # ---- Face structure (advanced head shape) ----
    ("head_diamond",     "Diamond shape",     "face_structure", "one_way",  ("head-diamond",)),
    ("head_inverted_tri","Inverted triangle", "face_structure", "one_way",  ("head-invertedtriangular",)),
    ("head_rectangular", "Rectangular",       "face_structure", "one_way",  ("head-rectangular",)),
    ("head_back_depth",  "Back-of-head depth","face_structure", "pair",     ("head-back-scale-depth-decr", "head-back-scale-depth-incr")),
    ("head_angle",       "Head yaw",          "face_structure", "pair",     ("head-angle-in",         "head-angle-out")),

    # ---- Eyes (advanced) ----
    ("eye_height1",      "Lid height 1",      "eyes",           "mirrored", ("eye-height1",)),
    ("eye_height3",      "Lid height 3",      "eyes",           "mirrored", ("eye-height3",)),
    ("eye_corner2",      "Inner corner tilt", "eyes",           "mirrored", ("eye-corner2-up-down",)),
    ("eye_bag",          "Eye bag depth",     "eyes",           "mirrored", ("eye-bag",)),
    ("eye_bag_height",   "Eye bag height",    "eyes",           "mirrored", ("eye-bag-height",)),
    ("eye_bag_horiz",    "Eye bag position",  "eyes",           "mirrored", ("eye-bag-in-out",)),
    ("eyefold_angle",    "Eyefold angle",     "eyes",           "mirrored", ("eye-eyefold-angle-up-down",)),
    ("eyefold_height",   "Eyefold height",    "eyes",           "mirrored", ("eye-eyefold-up-down",)),
    ("eye_push1",        "Eye push (upper)",  "eyes",           "mirrored", ("eye-push1-in-out",)),
    ("eye_push2",        "Eye push (lower)",  "eyes",           "mirrored", ("eye-push2-in-out",)),

    # ---- Brow (advanced) ----
    ("brow_depth",       "Brow depth",        "brow",           "pair",     ("eyebrows-trans-backward", "eyebrows-trans-forward")),
    ("forehead_temple",  "Temple width",      "brow",           "pair",     ("forehead-temple-decr",   "forehead-temple-incr")),
    ("forehead_nubian",  "Nubian forehead",   "brow",           "pair",     ("forehead-nubian-decr",   "forehead-nubian-incr")),
    ("forehead_depth",   "Forehead depth",    "brow",           "pair",     ("forehead-trans-backward","forehead-trans-forward")),

    # ---- Nose (advanced) ----
    ("nose_scale_depth", "Nose depth",        "nose",           "pair",     ("nose-scale-depth-decr", "nose-scale-depth-incr")),
    ("nose_base",        "Nose base height",  "nose",           "pair",     ("nose-base-down",        "nose-base-up")),
    ("nose_compression", "Nose compression",  "nose",           "pair",     ("nose-compression-uncompress","nose-compression-compress")),
    ("nose_greek",       "Greek profile",     "nose",           "pair",     ("nose-greek-decr",       "nose-greek-incr")),
    ("nose_septumangle", "Septum angle",      "nose",           "pair",     ("nose-septumangle-decr", "nose-septumangle-incr")),
    ("nose_nostrils_w",  "Nostril width",     "nose",           "pair",     ("nose-nostrils-width-decr","nose-nostrils-width-incr")),
    ("nose_nostrils_a",  "Nostril angle",     "nose",           "pair",     ("nose-nostrils-angle-down","nose-nostrils-angle-up")),
    ("nose_point_w",     "Nose point width",  "nose",           "pair",     ("nose-point-width-decr", "nose-point-width-incr")),
    ("nose_trans_vert",  "Nose vertical trans", "nose",         "pair",     ("nose-trans-down",       "nose-trans-up")),
    ("nose_trans_depth", "Nose depth trans",  "nose",           "pair",     ("nose-trans-backward",   "nose-trans-forward")),

    # ---- Mouth (advanced) ----
    ("mouth_scale_vert", "Mouth vertical scale", "mouth",       "pair",     ("mouth-scale-vert-decr", "mouth-scale-vert-incr")),
    ("mouth_scale_depth","Mouth depth",          "mouth",       "pair",     ("mouth-scale-depth-decr","mouth-scale-depth-incr")),
    ("mouth_trans_depth","Mouth depth trans",    "mouth",       "pair",     ("mouth-trans-backward",  "mouth-trans-forward")),
    ("upperlip_height",  "Upper lip height",     "mouth",       "pair",     ("mouth-upperlip-height-decr", "mouth-upperlip-height-incr")),
    ("upperlip_width",   "Upper lip width",      "mouth",       "pair",     ("mouth-upperlip-width-decr",  "mouth-upperlip-width-incr")),
    ("upperlip_middle",  "Upper lip middle",     "mouth",       "pair",     ("mouth-upperlip-middle-down", "mouth-upperlip-middle-up")),
    ("upperlip_ext",     "Upper lip extension",  "mouth",       "pair",     ("mouth-upperlip-ext-down",    "mouth-upperlip-ext-up")),
    ("lowerlip_height",  "Lower lip height",     "mouth",       "pair",     ("mouth-lowerlip-height-decr", "mouth-lowerlip-height-incr")),
    ("lowerlip_width",   "Lower lip width",      "mouth",       "pair",     ("mouth-lowerlip-width-decr",  "mouth-lowerlip-width-incr")),
    ("lowerlip_middle",  "Lower lip middle",     "mouth",       "pair",     ("mouth-lowerlip-middle-down", "mouth-lowerlip-middle-up")),
    ("lowerlip_ext",     "Lower lip extension",  "mouth",       "pair",     ("mouth-lowerlip-ext-down",    "mouth-lowerlip-ext-up")),
    ("cupidsbow_width",  "Cupid's bow width",    "mouth",       "pair",     ("mouth-cupidsbow-width-decr", "mouth-cupidsbow-width-incr")),
    ("mouth_dimples",    "Dimples",              "mouth",       "pair",     ("mouth-dimples-out",         "mouth-dimples-in")),
    ("mouth_laugh_lines","Laugh lines",          "mouth",       "pair",     ("mouth-laugh-lines-out",     "mouth-laugh-lines-in")),
    ("philtrum_volume",  "Philtrum volume",      "mouth",       "pair",     ("mouth-philtrum-volume-decr","mouth-philtrum-volume-incr")),

    # ---- Cheek (advanced) ----
    ("cheek_trans_vert", "Cheek height",         "cheek",       "mirrored", ("cheek-trans-up-down",)),

    # ---- Chin & jaw (advanced) ----
    ("chin_cleft",       "Chin cleft",           "chin",        "pair",     ("chin-cleft-decr",       "chin-cleft-incr")),
    ("chin_prognathism", "Prognathism",          "chin",        "pair",     ("chin-prognathism-decr", "chin-prognathism-incr")),
    ("chin_triangle",    "Triangular chin",      "chin",        "one_way",  ("chin-triangle",)),

    # ---- Ears (advanced) ----
    ("ear_shape_round",  "Round ears",           "ears",        "mirrored", ("ear-shape-round",       "one_way")),
    ("ear_shape_square", "Square ears",          "ears",        "mirrored", ("ear-shape-square",      "one_way")),
    ("ear_shape_triangle","Triangular ears",     "ears",        "mirrored", ("ear-shape-triangle",    "one_way")),
    ("ear_scale_depth",  "Ear depth",            "ears",        "mirrored", ("ear-scale-depth",)),
    ("ear_scale_vert",   "Ear height",           "ears",        "mirrored", ("ear-scale-vert",)),
    ("ear_flap",         "Ear flap",             "ears",        "mirrored", ("ear-flap",)),
    ("ear_lobe",         "Ear lobe",             "ears",        "mirrored", ("ear-lobe",)),
    ("ear_wing",         "Ear wing",             "ears",        "mirrored", ("ear-wing",)),
    ("ear_rot",          "Ear rotation",         "ears",        "mirrored", ("ear-rot-backward-forward",)),
    ("ear_trans_vert",   "Ear vertical trans",   "ears",        "mirrored", ("ear-trans-up-down",)),
    ("ear_trans_depth",  "Ear depth trans",      "ears",        "mirrored", ("ear-trans-backward-forward",)),

    # ---- Neck ----
    ("neck_width",       "Neck width",           "proportions", "pair",     ("neck-scale-horiz-decr", "neck-scale-horiz-incr")),
    ("neck_height",      "Neck height",          "proportions", "pair",     ("neck-scale-vert-decr",  "neck-scale-vert-incr")),
    ("neck_depth",       "Neck depth",           "proportions", "pair",     ("neck-scale-depth-decr", "neck-scale-depth-incr")),
    ("neck_double",      "Double chin",          "proportions", "pair",     ("neck-double-decr",      "neck-double-incr")),

    # ---- Body: arms (advanced) ----
    ("upperarm_muscle",  "Upper arm muscle",     "proportions", "mirrored", ("upperarm-muscle",)),
    ("upperarm_fat",     "Upper arm fat",        "proportions", "mirrored", ("upperarm-fat",)),
    ("upperarm_scale_h", "Upper arm width",      "proportions", "mirrored", ("upperarm-scale-horiz",)),
    ("upperarm_scale_v", "Upper arm length",     "proportions", "mirrored", ("upperarm-scale-vert",)),
    ("upperarm_scale_d", "Upper arm depth",      "proportions", "mirrored", ("upperarm-scale-depth",)),
    ("lowerarm_muscle",  "Forearm muscle",       "proportions", "mirrored", ("lowerarm-muscle",)),
    ("lowerarm_fat",     "Forearm fat",          "proportions", "mirrored", ("lowerarm-fat",)),
    ("lowerarm_scale_h", "Forearm width",        "proportions", "mirrored", ("lowerarm-scale-horiz",)),
    ("lowerarm_scale_v", "Forearm length",       "proportions", "mirrored", ("lowerarm-scale-vert",)),
    ("lowerarm_scale_d", "Forearm depth",        "proportions", "mirrored", ("lowerarm-scale-depth",)),

    # ---- Body: legs (advanced) ----
    ("upperleg_muscle",  "Thigh muscle",         "proportions", "mirrored", ("upperleg-muscle",)),
    ("upperleg_fat",     "Thigh fat",            "proportions", "mirrored", ("upperleg-fat",)),
    ("upperleg_scale_h", "Thigh width",          "proportions", "mirrored", ("upperleg-scale-horiz",)),
    ("upperleg_scale_v", "Thigh length",         "proportions", "mirrored", ("upperleg-scale-vert",)),
    ("upperleg_scale_d", "Thigh depth",          "proportions", "mirrored", ("upperleg-scale-depth",)),
    ("lowerleg_muscle",  "Calf muscle",          "proportions", "mirrored", ("lowerleg-muscle",)),
    ("lowerleg_fat",     "Calf fat",             "proportions", "mirrored", ("lowerleg-fat",)),
    ("lowerleg_scale_h", "Calf width",           "proportions", "mirrored", ("lowerleg-scale-horiz",)),
    ("lowerleg_scale_v", "Calf length",          "proportions", "mirrored", ("lowerleg-scale-vert",)),
    ("lowerleg_scale_d", "Calf depth",           "proportions", "mirrored", ("lowerleg-scale-depth",)),
    ("leg_valgus",       "Knee valgus",          "proportions", "mirrored", ("leg-valgus",)),

    # ---- Body: torso (muscle) ----
    ("torso_dorsi",      "Back muscle",          "proportions", "pair",     ("torso-muscle-dorsi-decr",     "torso-muscle-dorsi-incr")),
    ("torso_pectoral",   "Chest muscle",         "proportions", "pair",     ("torso-muscle-pectoral-decr",  "torso-muscle-pectoral-incr")),

    # ---- Body: hip ----
    ("hip_width",        "Hip width",            "proportions", "pair",     ("hip-scale-horiz-decr",  "hip-scale-horiz-incr")),
    ("hip_depth",        "Hip depth",            "proportions", "pair",     ("hip-scale-depth-decr",  "hip-scale-depth-incr")),
    ("hip_height",       "Hip height",           "proportions", "pair",     ("hip-scale-vert-decr",   "hip-scale-vert-incr")),
    ("hip_waist",        "Waist height",         "proportions", "pair",     ("hip-waist-down",        "hip-waist-up")),

    # ---- Body: stomach ----
    ("stomach_navel_v",  "Navel height",         "proportions", "pair",     ("stomach-navel-down",    "stomach-navel-up")),
    ("stomach_navel_d",  "Navel depth",          "proportions", "pair",     ("stomach-navel-in",      "stomach-navel-out")),

    # ---- Body: hands ----
    ("hand_scale",       "Hand size",            "proportions", "mirrored", ("hand-scale",)),
    ("finger_length",    "Finger length",        "proportions", "mirrored", ("hand-fingers-length",)),
    ("finger_diameter",  "Finger diameter",      "proportions", "mirrored", ("hand-fingers-diameter",)),
    ("finger_distance",  "Finger spacing",       "proportions", "mirrored", ("hand-fingers-distance",)),

    # ---- Body: feet ----
    ("foot_scale",       "Foot size",            "proportions", "mirrored", ("foot-scale",)),
    ("foot_scale_h",     "Foot width",           "proportions", "mirrored", ("foot-scale-horiz",)),
    ("foot_scale_v",     "Foot height",          "proportions", "mirrored", ("foot-scale-vert",)),
    ("foot_scale_d",     "Foot depth",           "proportions", "mirrored", ("foot-scale-depth",)),
]


def resolve_params(kind, params):
    """
    Returns dict of param_decr / param_incr / param plus min/max
    depending on kind. Handles the 'mirrored' abbreviation.
    """
    if kind == "pair":
        return {
            "applies_to": "morph_pair",
            "param_decr": params[0],
            "param_incr": params[1],
            "min": -1.0, "max": 1.0, "default": 0.0,
        }
    if kind == "mirrored":
        # 'mirrored' has a single base name. Two subcases:
        #  (a) "cheek-bones" -> l-cheek-bones-decr, l-cheek-bones-incr, r-cheek-bones-decr, r-cheek-bones-incr
        #  (b) "eye-trans-in-out" (a shorthand for the two-direction morphs) -> l-eye-trans-in, r-eye-trans-in / l-eye-trans-out, r-eye-trans-out
        base = params[0]
        one_way = len(params) > 1 and params[1] == "one_way"
        if one_way:
            # One-directional mirrored morph (e.g. ear-shape-pointed):
            # no "un-round" opposite. Emit as morph_target with a
            # comma-separated param -- the AppearanceEditor's
            # morph_target branch splits on the comma and mirrors the
            # slider value onto both sides so a single visible knob
            # drives L and R symmetrically.
            return {
                "applies_to": "morph_target",
                "param": f"l-{base},r-{base}",
                "min": 0.0, "max": 1.0, "default": 0.0,
            }
        # Determine whether base already contains a direction pair
        # like "in-out" / "up-down" / "backward-forward" / "concave-convex".
        DIRECTION_PAIRS = [
            ("in", "out"), ("up", "down"), ("down", "up"),
            ("backward", "forward"), ("concave", "convex"),
            ("up-down",), ("in-out",), ("backward-forward",),
            ("concave-convex",), ("angle-up-down",),
        ]
        # Look for a suffix pattern "-A-B" where A/B are directions.
        # If found, the two directions become decr/incr; if not, use
        # -decr / -incr suffix convention.
        suffix_pairs = [
            ("-down", "-up"),
            ("-in", "-out"),
            ("-backward", "-forward"),
            ("-concave", "-convex"),
            ("-decr", "-incr"),
        ]
        # Handle explicit multi-direction naming in `base`: e.g.
        # "eye-trans-in-out", "eye-corner1-up-down", "eyefold-angle-up-down",
        # "ear-trans-up-down", "cheek-trans-up-down", "eye-trans-up-down",
        # "eye-eyefold-concave-convex", "ear-rot-backward-forward".
        for a_b in [("in", "out"), ("up", "down"), ("backward", "forward"),
                     ("concave", "convex")]:
            a, b = a_b
            suffix_a_b = f"-{a}-{b}"
            suffix_b_a = f"-{b}-{a}"
            if base.endswith(suffix_a_b):
                stem = base[: -len(suffix_a_b)]
                return {
                    "applies_to": "morph_pair_mirrored",
                    "param_decr": f"l-{stem}-{a},r-{stem}-{a}",
                    "param_incr": f"l-{stem}-{b},r-{stem}-{b}",
                    "min": -1.0, "max": 1.0, "default": 0.0,
                }
            if base.endswith(suffix_b_a):
                stem = base[: -len(suffix_b_a)]
                return {
                    "applies_to": "morph_pair_mirrored",
                    "param_decr": f"l-{stem}-{b},r-{stem}-{b}",
                    "param_incr": f"l-{stem}-{a},r-{stem}-{a}",
                    "min": -1.0, "max": 1.0, "default": 0.0,
                }
        # Default: base name + "-decr" / "-incr" suffix
        return {
            "applies_to": "morph_pair_mirrored",
            "param_decr": f"l-{base}-decr,r-{base}-decr",
            "param_incr": f"l-{base}-incr,r-{base}-incr",
            "min": -1.0, "max": 1.0, "default": 0.0,
        }
    if kind == "one_way":
        return {
            "applies_to": "morph_target",
            "param": params[0],
            "min": 0.0, "max": 1.0, "default": 0.0,
        }
    if kind == "single":
        return {
            "applies_to": "morph_target",
            "param": params[0],
            "min": 0.0, "max": 1.0, "default": 0.0,
        }
    raise ValueError(f"unknown kind {kind}")


def build_entry(id_, label, category, kind, params, player_visible):
    entry = {
        "id": id_,
        "label": label,
        "category": category,
    }
    entry.update(resolve_params(kind, params))
    entry["player_visible"] = player_visible
    return entry


def build_all():
    entries = list(EXISTING)
    for row in PRIMARY:
        entries.append(build_entry(*row, player_visible=True))
    for row in SECONDARY:
        entries.append(build_entry(*row, player_visible=False))
    return entries


if __name__ == "__main__":
    entries = build_all()
    out = {
        "_comment": "Slider registry for the player character-creation UI + Appearance deformation runtime. ONE source of truth for what knobs exist, their ranges, defaults, and how they apply to the rendered actor. Adding a new slider = one entry here; both the creator UI and the deformation pass pick it up automatically.",
        "_applies_to": "Single-target kinds (use 'param'): bone_scale (one bone), bone_scale_pair (left,right bones), uniform_scale (model matrix), morph_target (one glTF morph key), macro (MPFB2 macro property; bake-time only). Bipolar pair kinds (use 'param_decr' + 'param_incr'): morph_pair (single -decr/-incr pair), morph_pair_mirrored (comma-separated 'left,right' on each side; one slider drives BOTH sides symmetrically).",
        "_category": "UI section grouping: identity / face_structure / eyes / brow / nose / mouth / cheek / chin / ears / proportions / skin / hair / eye_color / misc. Drives the Souls-style section navigator. Default 'misc' if unset.",
        "_tiers": "player_visible=true => shown in runtime CharacterCreationScreen (~40 axes). player_visible=false => shown in the Effigie standalone designer only (with the Show-designer-only toggle on). Promoting a secondary slider to the runtime creator: flip player_visible=true, no code change.",
        "version": 3,
        "sliders": entries,
    }
    SLIDERS.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(f"[regen_sliders] wrote {len(entries)} slider entries to {SLIDERS}")
