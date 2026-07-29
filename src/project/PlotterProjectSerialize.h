#pragma once

#include "PlotDoc.h"
#include "ofxPlotProcessors.h"
#include <entt.hpp>
#include <string>

namespace plotterProject {

ofJson pipelineToJson(const plotproc::PlotPipeline& pipeline);
void pipelineFromJson(const ofJson& j, plotproc::PlotPipeline& pipeline);

ofJson layersToJson(entt::registry& reg, const PlotDoc& doc);
void clearDocumentLayers(entt::registry& reg, PlotDoc& doc);
entt::entity loadSingleLayerFromJson(entt::registry& reg, PlotDoc& doc,
                                     const ofJson& lj,
                                     const std::vector<entt::entity>& created);
void finalizeLayerLoad(PlotDoc& doc);
void loadLayersFromJson(entt::registry& reg, PlotDoc& doc, const ofJson& layersJ);

} // namespace plotterProject
