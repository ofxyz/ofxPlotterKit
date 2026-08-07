#pragma once

#include "ofMain.h"
#include "ofxAssimpModelLoader.h"
#include "ofxKit.h"
#include "PlotDoc.h"
#include "PlotterZones.h"
#include "PlotterBedCoords.h"
#include "MachinePrefs.h"
#include "windows/PlotterPrintPreviewPanel.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

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
    /// When non-null, Detour snippet overlays respect Use Injections.
    bool* useInjections = nullptr;

    /// Toolpath 3D–local File → Import menu (Model / From Resources / Canvas).
    /// Drawn only in this viewport's menu bar — not the app main File menu.
    std::function<void()> drawImportMenu;
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
    bool showModels()  const { return m_showModels; }

    void setShowRibbon(bool v)  { m_showRibbon = v; }
    void setShowMarks(bool v)   { m_showMarks = v; }
    void setShowTravels(bool v) { m_showTravels = v; }
    void setShowLeads(bool v)   { m_showLeads = v; }
    void setShowMarkers(bool v) { m_showMarkers = v; }
    void setShowZones(bool v)   { m_showZones = v; }
    void setShowModels(bool v)  { m_showModels = v; }

private:
    bool inputsReady() const;
    void updateScene();
    void rebuildMeshes();
    void drawZoneModels(const plotter::PreviewBounds& pb);
    void drawSceneModels();
    ofxAssimpModelLoader* ensureZoneModel(const std::string& dataRelativePath);

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
    bool  m_showModels  = true;
    /// Toolpath 3D–local centerline preview (not shared with 2D pen colour).
    ofFloatColor m_toolpathColor { 1.f, 1.f, 1.f, 1.f };
    bool         m_useToolpathColor = true; ///< when false, keep per-path / layer colours
    float        m_toolpathWidthPx  = 2.f;  ///< GL line width for centerline
    float        m_travelWidthPx    = 1.5f; ///< GL line width for travels
    ofVboMesh   m_ribbon;
    ofVboMesh   m_draws;
    ofVboMesh   m_travels;
    ofVboMesh   m_leads;
    ofVboMesh   m_markers;
    ofVboMesh   m_zones;
    ofVboMesh   m_zoneLines;
    std::size_t m_sceneHash = 0;
    /// Placed (non-zone) model TRS — separate from toolpath mesh hash so gizmo
    /// edits force an FBO refresh without rebuilding path meshes.
    std::size_t m_sceneModelHash = 0;

    struct ZoneModelEntry {
        std::string           key;
        ofxAssimpModelLoader  model;
        bool                  ok = false;
    };
    std::unordered_map<std::string, ZoneModelEntry> m_zoneModels;
};

} // namespace plotter::kit
