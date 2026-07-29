#pragma once

#include "ofMain.h"
#include "ofxKit.h"
#include "PlotDoc.h"
#include "PlotterZones.h"
#include "PlotterBedCoords.h"
#include "MachinePrefs.h"
#include "windows/PlotterPrintPreviewPanel.h"

#include <cstddef>
#include <cstdint>
#include <functional>

struct PenSettings;

namespace plotter {
struct LandingSink;
struct PreviewBounds;
} // namespace plotter

namespace plotter::kit {

struct Toolpath3DInputs {
    PlotDoc* plotDoc = nullptr;
    plotter::PlotterZoneStore* zones = nullptr;
    entt::registry* registry = nullptr;
    grbl::MachinePrefs* machinePrefs = nullptr;
    PlotterPrintPreviewPanel* previewPanel = nullptr;
    plotter::LandingSink* landingPads = nullptr;
    std::function<plotter::PreviewBounds()> buildPreviewBounds;
    uint32_t* toolpathPreviewRev = nullptr;
    bool* showLeadBounds = nullptr;
    ofFloatColor* landingColor = nullptr;
    ofFloatColor* leadBoundsColor = nullptr;
};

/// Slicer-style 3D toolpath viewport: bed/paper ground plane, draw paths at
/// pen-down Z, travels at pen-up Z, smooth-approach landings as Z-coloured
/// polylines. Registers an ofxKit viewport window on setup().
class Toolpath3DView {
public:
    static constexpr const char* kWindowTitle = "Toolpath 3D";

    void setInputs(Toolpath3DInputs inputs);
    void setup();
    void update();
    void frame();
    void drawDisplayMenu();
    void drawScene();
    /// View → Preview submenu items (toggle panel, frame, Z exaggeration).
    void drawViewMenuItems();

    ofkitty::ViewportInstance* viewport() const { return m_viewport; }

    float zExaggeration() const { return m_zExagg; }
    void  setZExaggeration(float v) { m_zExagg = std::clamp(v, 1.f, 50.f); }

    bool showRibbon()  const { return m_showRibbon; }
    bool showMarks()   const { return m_showMarks; }
    bool showTravels() const { return m_showTravels; }
    bool showLeads()   const { return m_showLeads; }
    bool showMarkers() const { return m_showMarkers; }
    bool showZones()   const { return m_showZones; }

    void setShowRibbon(bool v)  { m_showRibbon = v; }
    void setShowMarks(bool v)   { m_showMarks = v; }
    void setShowTravels(bool v) { m_showTravels = v; }
    void setShowLeads(bool v)   { m_showLeads = v; }
    void setShowMarkers(bool v) { m_showMarkers = v; }
    void setShowZones(bool v)   { m_showZones = v; }

private:
    bool inputsReady() const;
    void updateScene();
    void rebuildMeshes();

    Toolpath3DInputs m_in;
    ofkitty::ViewportInstance* m_viewport = nullptr;
    bool  m_framed = false;
    float m_zExagg = 3.f;
    bool  m_showRibbon  = true;
    bool  m_showMarks   = true;
    bool  m_showTravels = true;
    bool  m_showLeads   = true;
    bool  m_showMarkers = true;
    bool  m_showZones   = true;
    ofVboMesh   m_ribbon;
    ofVboMesh   m_draws;
    ofVboMesh   m_travels;
    ofVboMesh   m_leads;
    ofVboMesh   m_markers;
    ofVboMesh   m_zones;
    ofVboMesh   m_zoneLines;
    std::size_t m_sceneHash = 0;
};

} // namespace plotter::kit
