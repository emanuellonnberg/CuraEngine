# Experimental Cross Hatch roof support

Cross Hatch remains the base infill. Optional Lightning-style branches grow up
from walls and stable points in that infill to support local top skin, including
roofs at different heights. Open tops do not seed branches. This reduces the
span the first solid top layer must bridge; it does not replace solid top layers.

The feature is disabled by default. Cura exposes these per-object settings:

| Setting | Default | Meaning |
| --- | --- | --- |
| `crosshatch_roof_support_enabled` | `false` | Enable additional roof branches |
| `crosshatch_roof_support_spacing` | 2 mm | Target spacing beneath top skin |
| `crosshatch_roof_support_angle` | 40 degrees | Branch growth angle from vertical |

Smaller spacing adds material near roofs. A lower angle extends the branches
farther down. Required support depth also depends on cell size and the distance
along branching paths to an anchor; a shallow part can need branches throughout
its height.

This prototype requires Cross Hatch, positive density, top layers, one infill
line, one-layer infill thickness, no gradual infill, no extra infill walls, and a
single infill angle. It takes precedence over ordinary Infill Skin Support.
Branches use the existing skin-support speed and flow settings and appear as
infill in the preview. Other infill patterns are unaffected.

The anchor lattice uses points shared by every Cross Hatch phase. Anchoring to
all moving infill lines would reconnect roots across cells at orientation
changes. Generation runs after final layer heights and angles are established.
Tree pruning propagates downward within the physical infill area with a small
wall overlap, while grounding uses the wall and invariant lattice boundaries.
This avoids repeatedly detaching and regrowing roots at internal anchors.

`CrossHatchRoofSupportTest` checks roof coverage, open tops, branch termination,
holes and concave parts, local stepped roofs, and support between consecutive
layers. Its geometry checks cover densities from 1% through 100%, rotations, and
0.12/0.40 mm layer heights. Physical print quality still needs print testing.
