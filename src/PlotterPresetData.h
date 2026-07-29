#pragma once

#include "PenSettings.h"
#include "PresetPicker.h"
#include "ofxPlotProcessors.h"

namespace plotter::kit {

/// Built-in ISO paper sizes (origin + width/height in mm).
void addBuiltinPaperPresets(ofkitty::PresetLibrary& lib);

/// Built-in machine bed envelopes (min/max in mm).
void addBuiltinEnvelopePresets(ofkitty::PresetLibrary& lib);

ofJson paperPresetJson(float originX, float originY, float paperW, float paperH);
bool   paperPresetEquals(const ofJson& a, const ofJson& b);

ofJson envelopePresetJson(float minX, float minY, float maxX, float maxY);
bool   envelopePresetEquals(const ofJson& a, const ofJson& b);

ofJson pipelinePresetJson(const plotproc::PlotPipeline& pipeline);
void   applyPipelinePreset(plotproc::PlotPipeline& pipeline, const ofJson& j);
bool   pipelinePresetEquals(const ofJson& a, const ofJson& b);
void   addBuiltinPipelinePresets(ofkitty::PresetLibrary& lib);

ofJson penPresetJson(const PenSettings& pen,
                     const ofFloatColor& color,
                     bool scaleStrokeToPenWidth);
void   applyPenPreset(const ofJson& j,
                      PenSettings& pen,
                      ofFloatColor& color,
                      bool& scaleStrokeToPenWidth);
bool   penPresetEquals(const ofJson& a, const ofJson& b);
void   addBuiltinPenPresets(ofkitty::PresetLibrary& lib);

} // namespace plotter::kit
