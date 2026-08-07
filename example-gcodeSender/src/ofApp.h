#pragma once

#include "ofMain.h"
#include "ofxKit.h"
#include "ofxDocumentKit.h"
#include "ofxPlotter.h"
#include "ofxPlotterKit.h"
#include "PlotterExporter.h"
#include "ofxImGuiTextEdit.h"
#include "TextEditorView.h"
#include "IconsFontAwesome5.h"

#include <atomic>
#include <cstddef>
#include <string>

/// G-code sender (ofxKit) — .ofdoc project + Import G-code + MainView2D preview.
/// G-code File → Save As / Find live on the G-code window (not the app File menu).
class ofApp : public ofBaseApp {
public:
    ofApp() = default;

    void setup()  override;
    void update() override;
    void draw()   override;
    void exit()   override;

    void keyPressed(int key)         override;
    void dragEvent(ofDragInfo info)  override;
    void windowResized(int w, int h) override;

    entt::registry& registry() { return m_registry; }

private:
    void importGcodeFile(const std::string& path);
    void openImportGcodeDialog();
    void saveEditorToFile(bool showSaveDialog = false);
    void parseEditorText();

    std::string buildOutputGcode(bool* sawIncremental = nullptr);
    std::string transformAbsoluteXYInGcode(const std::string& input,
                                           bool* sawIncremental = nullptr) const;
    std::string transformedSourceText(bool* sawIncremental = nullptr) const;
    void markPrepareDirty(const std::string& reason = {});
    void requestPrepare(); ///< Queue Update/Generate — runs on next update()
    void sendPreparedGcodeToPrinter();
    void savePreparedGcode();
    void syncExportSessionFlags();

    void syncDrawTarget(bool markSettingsDirty = true);
    void ensureDefaultDrawTargetZone();

    void wireDocumentKit();
    void applyPlotterEnvelope(const plotter::PlotterDocumentEnvelope& env);
    plotter::PlotterDocumentEnvelope capturePlotterEnvelope() const;
    void onDocumentLoaded();
    void markProjectDirty();

    std::string pipelineSourceText() const;
    std::size_t hashExportSettings() const;

    plotter::BedView       buildBedView()       const;
    plotter::PreviewBounds buildPreviewBounds() const;

    void setupUi();
    void drawControlPanel(bool& visible);
    void drawCodePanel(bool& visible);
    void drawPrintPreviewPreferencePage();
    void drawFileDropzone();
    void drawPreviewViewMenuItems();

    void syncEditorToPlayback();
    void pushPreparedGcodeToEditor();
    void syncGcodeEditorToView();
    void syncMainViewContentSize();
    /// Parse G-code into content-space draw + G0 travel for MainView2D toolpath.
    void syncToolpathPreview(const std::string& gcode);
    /// Overlay parabolic landing pads (touchdown + lead-in / lift-off) on the
    /// toolpath. Drawn in content mm; @p zoom is View2D px-per-mm for marker sizing.
    void drawLandingPads(float zoom);

    ofkitty::PresetLibrary envelopePresets;
    ofkitty::PresetLibrary pipelinePresets;
    ofkitty::PresetLibrary penPresets;
    ofkitty::PresetLibrary injectionPresets;
    plotter::kit::PlotterSnippetCatalog m_snippetCatalog;

    float paperOriginX = 0.f;
    float paperOriginY = 0.f;
    float printOffsetX = 0.f;
    float printOffsetY = 0.f;
    float printScaleX = 1.f;
    float printScaleY = 1.f;
    int   printRotateQuarterTurns = 0;
    bool  usePipeline    = true;
    bool  useInjections  = true;
    bool  prepareDirty   = true;
    bool  prepareRequested = false; ///< set by Update/Generate; drained in update()
    std::string liveSourceGcode;
    bool        liveSourceValid = false;

    entt::registry                    m_registry;
    PlotDoc                           m_plotDoc{m_registry};
    plotter::PlotterZoneStore         m_zones;
    plotproc::PlotPipeline            m_pipeline;
    plotter::kit::PlotterZonesPanel   m_zonesPanel;
    plotter::kit::PlotterPipelinePanel m_pipelinePanel;
    plotter::kit::PlotterInjectionsPanel m_injectionsPanel;
    plotter::kit::PlotterEnvelopePanel m_envelopePanel;
    ofkitty::LayersPanel m_layersPanel;

    ofxDocumentKit::ofxDocumentKit m_docKit;
    ofkitty::MainView2D*           m_view = nullptr;
    ofkitty::GuideSet              m_guides;
    plotter::kit::GcodeExportSession m_exportSession;

    /// Optional transport / prefs helpers (not the primary canvas).
    plotter::kit::PlotterPrintPreviewPanel m_panel;
    /// View → Preview → Show Mark: thick pen-width stroke footprint.
    bool m_showMark = true;
    /// View → Preview → Show Toolpath: thin machine-path centerline (+ travels).
    bool m_showToolpath = true;
    /// Distinct colour for the toolpath (kept separate from the mark fill so
    /// the machine path stays legible over a translucent mark).
    ofFloatColor m_toolpathColor { 0.f, 0.f, 0.f, 1.f };
    /// View → Preview → Show Parabolic Landing Pads: mark each stroke's landing
    /// touchdown + lead-in (and lift-off) so the smooth approach is visible.
    bool m_showLandingPads = false;
    ofFloatColor m_landingColor { 1.f, 0.35f, 0.1f, 0.9f };
    /// View → Preview → Show Lead bounds: art AABB expanded by Approach/Retract.
    bool m_showLeadBounds = true;
    ofFloatColor m_leadBoundsColor { 0.95f, 0.55f, 0.15f, 0.95f };
    uint32_t m_toolpathPreviewRev = 0; ///< bumped when preview G-code reparses
    plotter::kit::Toolpath3DView m_toolpath3D;
    /// Exact per-stroke touchdown / lift-off geometry (machine mm) captured from
    /// the export session on prepare, converted to content space at draw time.
    plotter::LandingSink m_landingPads;

    /// VBO caches for the main-view toolpath (rebuilt only when geometry,
    /// colors, or stroke parameters change — zoom/pan reuse the upload).
    plotter::PreviewPathMeshCache m_drawMeshCache;
    plotter::PreviewPathMeshCache m_travelMeshCache;
    plotter::PreviewPathMeshCache m_centerlineMeshCache;

    std::string sourceGcodePath;
    std::string statusMessage;
    bool        settingsDirty = false;

    grbl::GrblSender sender;
    grbl::MachinePrefs machinePrefs;
    plotter::kit::PlotterSerialWindow serialWindow;

    enum class GcodeEditorView { Prepared, Source };
    GcodeEditorView gcodeEditorView = GcodeEditorView::Prepared;
    TextEditor codeEditor;
    TextEditorView m_codeEditor;
    bool       syncCursorToPlayback = true;
    int        lastEditorLine       = 0;
    int        lastUndoIndex        = 0;
    bool       editorDirty          = false;
};
