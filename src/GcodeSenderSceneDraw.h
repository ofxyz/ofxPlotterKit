#pragma once

#include "PlotDoc.h"
#include "LeadBounds.h"
#include "PlotterBedCoords.h"
#include "PlotterPreviewDraw.h"
#include "PlotterZones.h"
#include "MachinePrefs.h"

#include <entt.hpp>

namespace plotter::kit {

struct GcodeSenderSceneOpts {
    bool showPaths = true;
    int  maxPathIndex = -1; ///< -1 = all flat paths
    /// When false, skip bed/paper/zone rects (host already drew the canvas).
    bool drawBedPaperZones = true;
    ofFloatColor bedColor { 0.12f, 0.12f, 0.16f, 1.f };
    ofColor paperColor { 245, 245, 240 };

    /// Artwork + Approach/Retract overshoot AABB (machine mm). Drawn after paper.
    bool         showLeadBounds = false;
    LeadBounds   leadBounds;
    ofFloatColor leadBoundsColor { 0.95f, 0.55f, 0.15f, 0.95f };

    /// Mark / toolpath preview layers (independent — either, both, or neither).
    bool         overrideColors         = true;
    ofFloatColor pathColor              { 0.08f, 0.15f, 0.75f, 1.f };
    /// Thick stroke footprint at physical pen width (the "Mark").
    bool         showMark               = false;
    float        penStrokeWidthMm       = 0.3f;
    float        contentZoomPxPerMm     = 1.f; ///< View2D zoom_ (px per mm)

    /// Thin machine-path centerline (the "Toolpath"). Drawn alone when Mark is
    /// off, or as a second pass on top of the Mark when both are on.
    bool         showToolpath           = true;
    ofFloatColor toolpathColor          { 0.f, 0.f, 0.f, 1.f };
    float        toolpathWidthPx        = 1.5f;

    /// Full toolpath in content mm (machine → PreviewBounds). When set, used
    /// instead of PlotDoc paper-local paths so G0 travels align with draws.
    const std::vector<ofPolyline>* contentDrawPaths   = nullptr;
    const std::vector<ofColor>*    contentDrawColors  = nullptr;
    bool                           showTravelPaths    = true;
    ofFloatColor                   travelColor        { 0.55f, 0.55f, 0.55f, 0.55f };
    const std::vector<ofPolyline>* contentTravelPaths = nullptr;

    /// Optional caller-owned VBO caches (see PreviewPathMeshCache). When set,
    /// steady-state frames reuse the uploaded mesh instead of rebuilding it.
    plotter::PreviewPathMeshCache* drawMeshCache       = nullptr;
    plotter::PreviewPathMeshCache* travelMeshCache     = nullptr;
    /// Separate cache for the thin toolpath pass (different thickness/colour than
    /// the mark fill, so it must not share drawMeshCache or it would rebuild each frame).
    plotter::PreviewPathMeshCache* centerlineMeshCache = nullptr;
};

/// Draw bed + PlotDoc flat paths in content mm (caller applies View2D matrix).
/// Does not use drawLayerContents — PlotDoc geometry lives on paths_component.
void drawGcodeSenderScene(PlotDoc& doc,
                          const plotter::PlotterZoneStore& zones,
                          entt::registry& reg,
                          const grbl::MachinePrefs& prefs,
                          const GcodeSenderSceneOpts& opts = {});

} // namespace plotter::kit
