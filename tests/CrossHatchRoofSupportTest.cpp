// Copyright (c) 2026 CuraEngine contributors
// CuraEngine is released under the terms of the AGPLv3 or higher.

#include "infill/CrossHatchRoofSupport.h"

#include <algorithm>
#include <cstdlib>

#include <gtest/gtest.h>

#include "geometry/SingleShape.h"
#include "infill/CrossHatchInfill.h"

namespace cura
{
namespace
{

Shape rectangle(const coord_t left, const coord_t bottom, const coord_t right, const coord_t top)
{
    return Shape(Polygon{ { { left, bottom }, { right, bottom }, { right, top }, { left, top } }, false });
}

std::vector<CrossHatchRoofSupport::Layer>
    makeStack(const coord_t distance = 8000, const bool roof = true, const Shape& outline = rectangle(-10000, -10000, 10000, 10000), const size_t count = 60)
{
    std::vector<CrossHatchRoofSupport::Layer> layers(count);
    for (size_t i = 0; i < layers.size(); ++i)
    {
        auto& layer = layers[i];
        layer.infill_area = outline;
        layer.z = (i + 1) * 200;
        layer.line_distance = distance;
        Shape polygons;
        CrossHatchInfill().generateInfill(layer.base_paths, polygons, false, distance, outline, layer.z, 400, AngleDegrees(0));
        for (const Polygon& polygon : polygons)
        {
            layer.base_paths.push_back(polygon.toPseudoOpenPolyline());
        }
    }
    if (roof)
    {
        layers.back().roof_area = outline;
    }
    return layers;
}

std::vector<OpenLinesSet> support(const std::vector<CrossHatchRoofSupport::Layer>& layers)
{
    std::srand(0);
    return CrossHatchRoofSupport::generate(layers, 400, 2000, AngleDegrees(40));
}

} // namespace

TEST(CrossHatchRoofSupportTest, HandlesEmptyAndInvalidInputs)
{
    EXPECT_TRUE(support({}).empty());
    const auto layers = makeStack();
    for (const auto& result : CrossHatchRoofSupport::generate(layers, 0, 2000, AngleDegrees(40)))
    {
        EXPECT_TRUE(result.empty());
    }
    for (const auto& result : CrossHatchRoofSupport::generate(layers, 400, 0, AngleDegrees(40)))
    {
        EXPECT_TRUE(result.empty());
    }
}

TEST(CrossHatchRoofSupportTest, AnchorRegionsStayOnPrintedPathsAcrossDensitiesAndPhases)
{
    const Shape outline = rectangle(-25000, -25000, 25000, 25000);
    for (const int density : { 1, 2, 5, 10, 20, 50, 100 })
    {
        const coord_t distance = 40000 / density;
        for (const int angle : { 0, 30, 90 })
        {
            const Shape anchors = CrossHatchInfill::generateAnchorRegions(distance, outline, 400, AngleDegrees(angle));
            for (coord_t z = 120; z <= 20000; z += 730)
            {
                SCOPED_TRACE(testing::Message() << density << "% angle " << angle << " z " << z);
                OpenLinesSet paths;
                Shape polygons;
                CrossHatchInfill().generateInfill(paths, polygons, false, distance, outline, z, 400, AngleDegrees(angle));
                for (const Polygon& polygon : polygons)
                {
                    paths.push_back(polygon.toPseudoOpenPolyline());
                }
                EXPECT_LE(anchors.difference(paths.offset(205)).area(), 100);
            }
        }
    }
}

TEST(CrossHatchRoofSupportTest, OpenTopDoesNotSeedBranches)
{
    const auto result = support(makeStack(8000, false));
    ASSERT_EQ(result.size(), 60);
    for (const auto& paths : result)
    {
        EXPECT_TRUE(paths.empty());
    }
}

TEST(CrossHatchRoofSupportTest, AddsBranchesUnderRoofWithoutFillingTheWholeBody)
{
    // The available height must exceed the longest root-to-leaf branch path,
    // which can be longer than the direct distance to the nearest anchor.
    const auto result = support(makeStack(8000, true, rectangle(-10000, -10000, 10000, 10000), 180));
    ASSERT_FALSE(result.back().empty());
    EXPECT_TRUE(result.front().empty()) << "Bottom length " << result.front().length() << ", top length " << result.back().length();
    const size_t active = std::count_if(
        result.begin(),
        result.end(),
        [](const auto& paths)
        {
            return ! paths.empty();
        });
    EXPECT_GT(active, 1);
    EXPECT_LT(active, result.size());
}

TEST(CrossHatchRoofSupportTest, RoofCoverageImprovesAtTargetSpacing)
{
    const auto layers = makeStack();
    const auto result = support(layers);
    const auto& top = layers.back();
    OpenLinesSet combined = top.base_paths;
    combined.push_back(result.back());
    const Shape target = top.roof_area.offset(-1500);
    const double before = target.difference(top.base_paths.offset(1250)).area();
    const double after = target.difference(combined.offset(1250)).area();
    ASSERT_GT(before, 0);
    EXPECT_LT(after, before * 0.05);
}

TEST(CrossHatchRoofSupportTest, BranchComponentsTouchPrintedInfillOrWalls)
{
    const auto layers = makeStack();
    const auto result = support(layers);
    for (size_t i = 0; i < result.size(); ++i)
    {
        SCOPED_TRACE(i);
        const Shape walls = layers[i].infill_area.difference(layers[i].infill_area.offset(-400));
        const Shape anchors = walls.unionPolygons(layers[i].base_paths.offset(205));
        for (const auto& component : result[i].offset(205).splitIntoParts())
        {
            EXPECT_FALSE(component.intersection(anchors).empty());
        }
    }
}

TEST(CrossHatchRoofSupportTest, ConsecutiveLayersSupportTheBranchesAbove)
{
    const auto layers = makeStack();
    const auto result = support(layers);
    for (size_t i = 1; i < result.size(); ++i)
    {
        if (result[i].empty())
        {
            continue;
        }
        SCOPED_TRACE(i);
        OpenLinesSet below = layers[i - 1].base_paths;
        below.push_back(result[i - 1]);
        const Shape walls = layers[i].infill_area.difference(layers[i].infill_area.offset(-600));
        const Shape supported = below.offset(600).unionPolygons(walls);
        const double unsupported = result[i].difference(supported).length();
        EXPECT_LT(unsupported, result[i].length() * 0.02 + 20);
    }
}

TEST(CrossHatchRoofSupportTest, BranchesStayInsideHoledAndConcaveParts)
{
    Shape outline = rectangle(-10000, -10000, 10000, 10000);
    outline = outline.difference(rectangle(-2000, -2000, 2000, 2000));
    outline = outline.difference(rectangle(5000, -6000, 11000, -1000));
    const auto result = support(makeStack(8000, true, outline));
    ASSERT_FALSE(result.back().empty());
    for (const auto& paths : result)
    {
        EXPECT_LE(paths.difference(outline.offset(5)).length(), 10);
    }
}

TEST(CrossHatchRoofSupportTest, BranchSupportFollowsDensityRotationAndLayerHeight)
{
    for (const int density : { 1, 2, 5, 10, 25, 50 })
    {
        for (const int angle : { 0, 30 })
        {
            for (const coord_t height : { 120, 400 })
            {
                auto layers = makeStack(40000 / density);
                for (size_t i = 0; i < layers.size(); ++i)
                {
                    auto& layer = layers[i];
                    layer.z = (i + 1) * height;
                    layer.angle = AngleDegrees(angle);
                    layer.base_paths.clear();
                    Shape polygons;
                    CrossHatchInfill().generateInfill(layer.base_paths, polygons, false, layer.line_distance, layer.infill_area, layer.z, 400, layer.angle);
                }
                const auto result = support(layers);
                for (size_t i = 1; i < layers.size(); ++i)
                {
                    SCOPED_TRACE(testing::Message() << density << "% angle " << angle << " height " << height << " layer " << i);
                    OpenLinesSet below = layers[i - 1].base_paths;
                    below.push_back(result[i - 1]);
                    const Shape walls = layers[i].infill_area.difference(layers[i].infill_area.offset(-600));
                    EXPECT_LT(result[i].difference(below.offset(600).unionPolygons(walls)).length(), result[i].length() * 0.02 + 20);
                }
            }
        }
    }
}

TEST(CrossHatchRoofSupportTest, DenseInfillAlreadySupportsTheRoof)
{
    const auto result = support(makeStack(400));
    for (const auto& paths : result)
    {
        EXPECT_TRUE(paths.empty());
    }
}

TEST(CrossHatchRoofSupportTest, SeparateRoofHeightsGetLocalSupport)
{
    auto layers = makeStack(8000, false);
    const Shape left = rectangle(-10000, -10000, 0, 10000);
    const Shape right = rectangle(0, -10000, 10000, 10000);
    layers[29].roof_area = left;
    for (size_t i = 30; i < layers.size(); ++i)
    {
        layers[i].infill_area = right;
        layers[i].base_paths = right.intersection(layers[i].base_paths, false, 0, false);
    }
    layers.back().roof_area = right;
    const auto result = support(layers);
    EXPECT_GT(left.intersection(result[29], false).length(), 0);
    EXPECT_FALSE(result.back().empty());
    for (size_t i = 30; i < layers.size(); ++i)
    {
        EXPECT_LE(result[i].difference(right.offset(5)).length(), 10);
    }
}

} // namespace cura
