#pragma once

#include "PlotDoc.h"
#include "PlotterBedCoords.h"
#include "PlotterZoneComponents.h"
#include "PlotterZones.h"

#include <MachinePrefs.h>
#include <entt.hpp>
#include <string>

namespace plotter::kit {

/// Options for generating L-corner cropmarks into a PlotDoc layer.
struct CropmarksGenerateOpts {
    float         lengthMm     = 10.f;
    float         insetMm      = 5.f;   ///< signed: + inward, − outward from margin box
    ZoneMarginsMM margins;             ///< L/R/T/B used to build the margin box
    float         thicknessMm  = 0.3f;
    ofColor       color        { 0, 0, 0, 255 };
    /// Existing layer to overwrite, or entt::null to create @p newLayerName.
    entt::entity  targetLayer  { entt::null };
    std::string   newLayerName;        ///< used when targetLayer is null
    bool          pinFirst     = true; ///< move layer to bottom of stack (drawn first)
};

/// Build zone-local margin box (same convention as Generator cropmarks).
/// Returns false if the inner rect is empty / invalid.
bool zoneMarginBox(const machine_zone_component& zone,
                   const ZoneMarginsMM& margins,
                   float& x0, float& y0, float& x1, float& y1);

/// Generate L-corner cropmarks for @p zoneEntity into a PlotDoc layer.
/// Paths are stored in draw-target paper-local mm (zone-local → machine → paper)
/// so they land on the correct bed position when exported with the canvas.
///
/// @return layer entity, or entt::null on failure (optional @p errMsg).
entt::entity generateCropmarksForZone(PlotDoc& doc,
                                      PlotterZoneStore& zones,
                                      const grbl::MachinePrefs* prefs,
                                      entt::entity zoneEntity,
                                      const CropmarksGenerateOpts& opts,
                                      std::string* errMsg = nullptr);

} // namespace plotter::kit
