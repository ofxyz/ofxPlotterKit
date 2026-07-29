#pragma once

#include "DocumentJson.h"
#include "MachinePrefs.h"
#include "PlotPipeline.h"
#include "PlotterZones.h"
#include "ofColor.h"

#include <string>

namespace plotter {

/// Non-entity project state stored under the root `"plotter"` key in `.ofdoc`.
struct PlotterDocumentEnvelope {
    std::string            drawTargetZoneId;
    plotproc::PlotPipeline pipeline;
    float                  printOffsetX            = 0.f;
    float                  printOffsetY            = 0.f;
    float                  printScaleX             = 1.f;
    float                  printScaleY             = 1.f;
    int                    printRotateQuarterTurns = 0;
    bool                   liveTransformEnabled    = true; ///< legacy; prepare is manual now
    bool                   usePipeline             = true;
    bool                   useInjections           = true;
    /// Optional bed/envelope snapshot for preview (serial port stays app-global).
    grbl::Envelope         machineEnvelope;
    grbl::BedLayout        machineBed;
    bool                   hasMachineBed           = false;
    /// Per-document pen appearance (color + preview stroke scaling). These are
    /// artwork state, not machine config, so they round-trip with the project.
    /// hasPenAppearance stays false for legacy docs so the app-global pref wins.
    ofFloatColor           penColor{ 0.08f, 0.15f, 0.75f, 1.f };
    bool                   scaleStrokeToPenWidth   = false;
    bool                   hasPenAppearance        = false;
};

/// Write / read the root `"plotter"` block (for DocumentSerializer root extensions).
void writePlotterDocumentEnvelope(ofxDocumentKit::ordered_json& root,
                                  const PlotterDocumentEnvelope& env);
void readPlotterDocumentEnvelope(const ofxDocumentKit::ordered_json& root,
                                 PlotterDocumentEnvelope& env);

} // namespace plotter
