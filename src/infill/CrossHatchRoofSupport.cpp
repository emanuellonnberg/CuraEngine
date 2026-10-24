// Copyright (c) 2026 CuraEngine contributors
// CuraEngine is released under the terms of the AGPLv3 or higher.

#include "infill/CrossHatchRoofSupport.h"

#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

#include "infill.h"
#include "infill/CrossHatchInfill.h"
#include "infill/LightningLayer.h"
#include "infill/LightningTreeNode.h"
#include "sliceDataStorage.h"
#include "utils/polygonUtils.h"

namespace cura
{

bool CrossHatchRoofSupport::isEnabled(const Settings& settings)
{
    return settings.get<EFillMethod>("infill_pattern") == EFillMethod::CROSSHATCH && settings.get<bool>("crosshatch_roof_support_enabled")
        && settings.get<coord_t>("infill_line_distance") > 0 && settings.get<size_t>("top_layers") > 0 && settings.get<size_t>("gradual_infill_steps") == 0
        && settings.get<size_t>("infill_multiplier") == 1 && settings.get<size_t>("infill_wall_line_count") == 0
        && settings.get<coord_t>("infill_sparse_thickness") == settings.get<coord_t>("layer_height") && settings.get<std::vector<AngleDegrees>>("infill_angles").size() <= 1;
}

void CrossHatchRoofSupport::generateForMesh(SliceMeshStorage& mesh)
{
    mesh.crosshatch_roof_support.clear();
    if (! isEnabled(mesh.settings))
    {
        return;
    }

    const Settings& settings = mesh.settings;
    const coord_t line_width = settings.get<coord_t>("infill_line_width");
    const coord_t line_distance = settings.get<coord_t>("infill_line_distance");
    const coord_t overlap = settings.get<coord_t>("infill_overlap_mm");
    const coord_t max_resolution = settings.get<coord_t>("meshfix_maximum_resolution");
    const coord_t max_deviation = settings.get<coord_t>("meshfix_maximum_deviation");
    auto angles = settings.get<std::vector<AngleDegrees>>("infill_angles");
    if (angles.empty())
    {
        angles.emplace_back(45); // Same fallback as FffGcodeWriter::setInfillAndSkinAngles.
    }

    std::vector<Layer> layers(mesh.layers.size());
    for (size_t layer_nr = 0; layer_nr < mesh.layers.size(); ++layer_nr)
    {
        Layer& target = layers[layer_nr];
        const SliceLayer& source = mesh.layers[layer_nr];
        target.z = source.printZ;
        target.line_distance = line_distance;
        target.angle = angles[layer_nr % angles.size()];
        for (const SliceLayerPart& part : source.parts)
        {
            if (part.infill_area_per_combine_per_density.empty() || part.infill_area_per_combine_per_density.back().empty())
            {
                continue;
            }
            const Shape& area = part.infill_area_per_combine_per_density.back().front();
            target.infill_area.push_back(area);
            if (area.empty())
            {
                continue;
            }
            Infill infill(
                EFillMethod::CROSSHATCH,
                settings.get<bool>("zig_zaggify_infill"),
                settings.get<bool>("connect_infill_polygons"),
                area,
                line_width,
                line_distance,
                overlap,
                1,
                angles[layer_nr % angles.size()],
                source.printZ,
                0,
                max_resolution,
                max_deviation);
            std::vector<VariableWidthLines> wall_paths;
            Shape polygons;
            infill.generate(
                wall_paths,
                polygons,
                target.base_paths,
                settings,
                layer_nr,
                SectionType::INFILL,
                nullptr,
                nullptr,
                &mesh,
                Shape(),
                settings.get<coord_t>("minimum_infill_line_length"));
            for (const Polygon& polygon : polygons)
            {
                target.base_paths.push_back(polygon.toPseudoOpenPolyline());
            }
        }
        target.infill_area = target.infill_area.unionPolygons();
        if (layer_nr + 1 < mesh.layers.size())
        {
            for (const SliceLayerPart& above_part : mesh.layers[layer_nr + 1].parts)
            {
                for (const SkinPart& skin : above_part.skin_parts)
                {
                    target.roof_area.push_back(skin.outline);
                }
            }
            target.roof_area = target.roof_area.unionPolygons().intersection(target.infill_area);
        }
    }

    mesh.crosshatch_roof_support
        = generate(layers, line_width, settings.get<coord_t>("crosshatch_roof_support_spacing"), settings.get<AngleDegrees>("crosshatch_roof_support_angle"));
    size_t supported_layers = 0;
    for (const OpenLinesSet& paths : mesh.crosshatch_roof_support)
    {
        supported_layers += ! paths.empty();
    }
    spdlog::info("Cross Hatch roof support generated on {} layers", supported_layers);
}

std::vector<OpenLinesSet>
    CrossHatchRoofSupport::generate(const std::vector<Layer>& layers, const coord_t line_width, const coord_t support_spacing, const AngleDegrees support_angle)
{
    std::vector<OpenLinesSet> result(layers.size());
    if (layers.empty() || line_width <= 0 || support_spacing <= 0)
    {
        return result;
    }

    const coord_t supporting_radius = std::max({ coord_t{ 6 }, line_width, support_spacing / 2 });
    const AngleDegrees bounded_angle(std::clamp(static_cast<double>(support_angle), 0.0, 75.0));
    const double tangent = std::tan(static_cast<double>(AngleRadians(bounded_angle)));
    std::vector<Shape> voids(layers.size());
    std::vector<Shape> roofs(layers.size());
    bool has_roofs = false;
    for (size_t layer_nr = 0; layer_nr < layers.size(); ++layer_nr)
    {
        const Layer& layer = layers[layer_nr];
        if (! layer.roof_area.empty())
        {
            roofs[layer_nr] = layer.roof_area.intersection(layer.infill_area.offset(-supporting_radius)).difference(layer.base_paths.offset(supporting_radius));
            has_roofs |= ! roofs[layer_nr].empty();
        }
    }
    if (! has_roofs)
    {
        return result;
    }
    for (size_t layer_nr = 0; layer_nr < layers.size(); ++layer_nr)
    {
        const Layer& layer = layers[layer_nr];
        // Anchor only at the phase-invariant columns. Using every moving line
        // as a wall would reconnect roots across cells and regrow pruned trees.
        const Shape anchors
            = CrossHatchInfill::generateAnchorRegions(layer.line_distance, layer.infill_area, line_width, layer.angle).intersection(layer.base_paths.offset(line_width / 2));
        voids[layer_nr] = layer.infill_area.difference(anchors);
        roofs[layer_nr] = roofs[layer_nr].intersection(voids[layer_nr]);
    }

    std::vector<LightningLayer> trees(layers.size());
    for (size_t reverse_index = layers.size(); reverse_index > 0; --reverse_index)
    {
        const size_t layer_nr = reverse_index - 1;
        if (voids[layer_nr].empty())
        {
            continue;
        }
        auto locator = PolygonUtils::createLocToLineGrid(voids[layer_nr], locator_cell_size);
        const coord_t layer_height = layer_nr > 0 ? std::max(coord_t{ 1 }, layers[layer_nr].z - layers[layer_nr - 1].z) : std::max(coord_t{ 1 }, layers[layer_nr].z);
        const coord_t growth = std::max(coord_t{ 1 }, static_cast<coord_t>(std::llrint(layer_height * tangent)));
        LightningLayer& current = trees[layer_nr];
        // Pruning can leave a root without any printable branches. Regrounding
        // such a root would grow a fresh connector forever below the roof.
        auto& roots = current.tree_roots;
        roots.erase(
            std::remove_if(
                roots.begin(),
                roots.end(),
                [line_width](const LightningTreeNodeSPtr& tree)
                {
                    coord_t length = 0;
                    tree->visitBranches(
                        [&length](const Point2LL& a, const Point2LL& b)
                        {
                            length += vSize(b - a);
                        });
                    return length <= line_width / 2;
                }),
            roots.end());
        auto reconnect = current.tree_roots;
        reconnect.erase(
            std::remove_if(
                reconnect.begin(),
                reconnect.end(),
                [&](const LightningTreeNodeSPtr& root)
                {
                    // Integer clipping may leave a root just off its boundary.
                    // It still overlaps the printed anchor; reconnecting it can
                    // repeatedly add a connector after each pruning step.
                    const auto closest = PolygonUtils::findClosest(root->getLocation(), voids[layer_nr]);
                    return vSize(root->getLocation() - closest.p()) <= line_width / 4;
                }),
            reconnect.end());
        current.generateNewTrees(roofs[layer_nr], voids[layer_nr], *locator, supporting_radius, growth);
        current.reconnectRoots(reconnect, voids[layer_nr], *locator, supporting_radius, growth);
        result[layer_nr] = layers[layer_nr].infill_area.intersection(current.convertToLines(layers[layer_nr].infill_area, line_width), false, 0, false);

        if (layer_nr > 0 && ! voids[layer_nr - 1].empty())
        {
            // Include the wall overlap so integer-rounded boundary roots stay
            // inside during realignment instead of being detached every layer.
            const Shape below_outline = layers[layer_nr - 1].infill_area.offset(line_width / 4);
            auto below_locator = PolygonUtils::createLocToLineGrid(below_outline, locator_cell_size);
            for (const LightningTreeNodeSPtr& tree : current.tree_roots)
            {
                // Keep inherited branches in place; shortening the tips controls
                // their growth upward. Reconnection below finds printed anchors.
                // Internal anchors are printed material, not physical holes.
                // Realigning against them would detach an anchored root on
                // every layer and undo pruning when that root is reconnected.
                tree->propagateToNextLayer(trees[layer_nr - 1].tree_roots, below_outline, *below_locator, growth, 0, 0);
            }
        }
    }
    return result;
}

} // namespace cura
