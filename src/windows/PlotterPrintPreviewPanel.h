#pragma once

#include "ofMain.h"
#include "PlotterPrintPreview.h"
#include "PlotterPreviewDraw.h"
#include "PlotterBedCoords.h"
#include "View2DState.h"
#include "ImGridGuides.h"
#include <entt.hpp>
#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

struct ImDrawList;

namespace plotter::kit {

/// Self-contained G-code print preview panel: pan/zoom viewport, transport,
/// G0 travel-path overlay, zone grid/margins, and per-type color prefs.
///
/// Standalone usage (ImGui vector canvas — crisp at any zoom):
///   panel.update(ofGetLastFrameTime());
///   panel.draw("Print Preview###my.panel", visible);
///
/// Embedded in an ofxKit ViewportInstance (OF raster path):
///   vp->renderer2D = [&]{ panel.drawScene(vp->contentZoom()); };
///   vp->headerDraw = [&]{ return panel.drawHeader(); };
class PlotterPrintPreviewPanel {
public:
    // ── Window concept ────────────────────────────────────────────────────────
    std::string name()       const { return "Print Preview"; }
    bool        isVisible()  const { return false; }

    /// Full standalone ImGui::Begin/End window with vector canvas, transport, and companion buttons.
    void draw(bool& visible);

    /// Optional ImGui::Begin title (include ###id for ofxKit docking). Default: name().
    void setImGuiWindowTitle(std::string title) { m_imguiWindowTitle = std::move(title); }

    /// Toolbar row suitable for ViewportInstance::headerDraw.
    /// Shows companion-action buttons and a compact playback slider.
    /// Returns false (pass-through for the headerDraw bool convention).
    bool drawHeader();

    /// Pure OF drawing in content-space mm — wire to ViewportInstance::renderer2D.
    /// Caller must set up the view matrix (translate + scale) before calling.
    void drawScene(float pxPerMm = 1.f);

    // ── Data ─────────────────────────────────────────────────────────────────
    bool loadFromFile(const std::string& path, const PrintPreviewOptions& opts);
    bool loadFromText(const std::string& text, const PrintPreviewOptions& opts);
    void clear();
    bool               hasGeometry()  const;
    const std::string& sourceText()   const;
    const std::string& sourceLabel()  const;

    /// Content-space mm geometry (machine → preview bounds). Used by MainView2D.
    const std::vector<ofPolyline>& drawPaths()   const { return m_preview.paths(); }
    const std::vector<ofColor>&    drawColors()  const { return m_preview.pathColors(); }
    const std::vector<ofPolyline>& travelPaths() const { return m_travelPaths; }

    // ── Companion window buttons ──────────────────────────────────────────────
    /// Add a labelled button shown in the panel toolbar.
    /// Use this to open related panels (Bed Layout, Serial Console, etc.)
    /// without the panel depending on any specific window system.
    void addCompanionAction(const std::string& label, std::function<void()> cb);
    void clearCompanionActions();

    // ── Per-frame ─────────────────────────────────────────────────────────────
    void update(float dt);

    // ── Transport ─────────────────────────────────────────────────────────────
    float playback()     const { return m_playback; }
    int   maxPathIndex() const;
    void  setPlayback(float t);
    bool  playing()      const { return m_isPlaying; }
    void  setPlaying(bool p)   { m_isPlaying = p; }
    void  stepPath(int delta);
    void  fitView();

    /// Zone grid/margin overlays read from this registry (optional).
    void setRegistry(entt::registry* reg) { m_registry = reg; }

    // ── Visual prefs ──────────────────────────────────────────────────────────
    ofFloatColor envelopeColor { 0.22f, 0.24f, 0.32f, 1.f };
    ofFloatColor paperColor    { 0.96f, 0.95f, 0.92f, 1.f };
    ofFloatColor drawColor     { 0.08f, 0.15f, 0.75f, 1.f };
    ofFloatColor travelColor   { 0.55f, 0.55f, 0.55f, 0.35f };
    bool  showTravelPaths = true;
    bool  overrideColors  = true;
    bool  scaleStrokeToPenWidth = false;
    float penStrokeWidthMm      = 0.3f;
    float playbackSpeed   = 20.f;   ///< paths per second during auto-play
    float paperW          = 0.f;    ///< paper rectangle width mm (0 = skip paper rect)
    float paperH          = 0.f;    ///< paper rectangle height mm

private:
    void drawCanvasImGui(ImDrawList* dl, float ox, float oy, float zoom,
                         const ImVec2& clipMin, const ImVec2& clipMax);
    void parseTravelPaths();
    void drawTransportControls(); ///< compact inline transport (no Begin/End)

    PlotterPrintPreview     m_preview;
    PrintPreviewOptions     m_lastOpts;
    std::vector<ofPolyline> m_travelPaths;

    float m_playback  = 1.f;
    bool  m_isPlaying = false;

    /// Pan/zoom state in canvas-local pixels (canvasOrigin stays {0,0};
    /// callers pass canvas-local pivot coords).
    ofkitty::View2DState m_view;

    entt::registry* m_registry = nullptr;
    ImGridGuides      m_gridStyle;
    bool              m_yAxisUp = false;

    struct CompanionAction { std::string label; std::function<void()> cb; };
    std::vector<CompanionAction> m_companionActions;

    std::string m_imguiWindowTitle;
};

} // namespace plotter::kit
