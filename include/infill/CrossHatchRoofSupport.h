// Copyright (c) 2026 CuraEngine contributors
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef INFILL_CROSSHATCHROOFSUPPORT_H
#define INFILL_CROSSHATCHROOFSUPPORT_H

#include <vector>

#include "geometry/OpenLinesSet.h"
#include "geometry/Shape.h"

namespace cura
{

class Settings;
class SliceMeshStorage;

/*!
 * Additional Lightning-style branches beneath local top skin. The ordinary
 * Cross Hatch paths are retained and act as internal grounding boundaries.
 */
class CrossHatchRoofSupport
{
public:
    struct Layer
    {
        Shape infill_area;
        Shape roof_area; //!< Skin on the immediately following layer, clipped to this infill area.
        OpenLinesSet base_paths; //!< The actual Cross Hatch paths that will be printed on this layer.
        coord_t line_distance = 0;
        AngleDegrees angle = 0;
        coord_t z = 0;
    };

    static bool isEnabled(const Settings& settings);
    static void generateForMesh(SliceMeshStorage& mesh);

    static std::vector<OpenLinesSet> generate(const std::vector<Layer>& layers, coord_t line_width, coord_t support_spacing, AngleDegrees support_angle);
};

} // namespace cura

#endif
