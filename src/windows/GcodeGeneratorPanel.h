#pragma once

#include "ofMain.h"
#include "ofxGrbl.h"
#include "PlotDoc.h"
#include "PlotterZones.h"
#include "PlotterEffectGraphECS.h"
#include "PlotterEffectGraph.h"
#include "ofxPlotProcessors.h"
#include "PlotGeneratorTypes.h"
#include "GeneratorJobRunner.h"
#include <ofxKit.h>

#include <functional>
#include <string>
#include <vector>

namespace plotter::kit {

struct PlotterGenerateBindings {

    PlotDoc*                engine = nullptr;
    std::atomic<bool>*          generating = nullptr;
    std::atomic<float>*         progress = nullptr;
    std::string*                progressMsg = nullptr;
    std::string*                imageName = nullptr;
    PlotDoc::SvgImportMode* svgImportMode = nullptr;
    PlotDoc::SvgScaleMode*  svgScaleMode = nullptr;

    std::function<void(unsigned)> markCanvasDirty;
    std::function<void()>         onSourceChanged;
    std::function<void()>         startGenerate;
    std::function<void()>         sendToPlotter;
    std::function<void()>         loadImageDialog;
    std::function<void()>         loadSvgDialog;
    std::function<void()>         loadAnySourceDialog;

    /// Returns all non-generated resources available in the Resources panel.
    std::function<std::vector<ofkitty::Resource>()> getAllResources;

    /// Places / loads a resource as the active plot source.
    std::function<void(const ofkitty::Resource&)> placeSourceResource;
    std::function<void()>      scaleActual;
    std::function<void()>      scaleFit;
    std::function<void()>      onEffectGraphChanged;
    std::function<bool()>      senderConnected;
    grbl::GrblSender*          sender = nullptr;
    std::function<float()>     referenceFeedMmMin;

    /// Returns true while a background G-code export is running.
    std::function<bool()>      isExportBusy;

    /// Returns the natural mm dimensions of the currently placed source
    /// (SVG layout drawW×drawH, or image draw area).  Returns {0,0} when
    /// no source is loaded.  Used by the "Fit to Source" zone size preset.
    std::function<glm::vec2()> getSourceSizeMM;

    /// Invalidate cached SVG overlay layouts after a path transform so the
    /// rotated ECS layer paths are shown instead of the stale SVG source.
    std::function<void()>      invalidateSvgLayouts;
    std::function<void()>      refreshFilterPreview;
    /// Reload the active plot source from disk (returns false when none loaded).
    std::function<bool()>      reloadSource;

    /// Re-split all placed SVG/DXF sources with the current import mode
    /// (called when the "Import mode" combo changes).
    std::function<void()>      reapplyImportMode;

};



/// Zones, injection, plot pipeline, and generation controls.

class GcodeGeneratorPanel {

public:
    void setEngine(PlotDoc* engine) { m_engine = engine; }
    void setZoneStore(plotter::PlotterZoneStore* zones) { m_zones = zones; }
    void setPrefs(grbl::MachinePrefs* prefs) { m_prefs = prefs; }
    void setPipeline(plotproc::PlotPipeline* pipeline) { m_pipeline = pipeline; }
    void setGenerateBindings(PlotterGenerateBindings bindings) { m_gen = std::move(bindings); }
    void setEffectGraphECS(plotter::PlotterEffectGraphECS* ecs) { m_effectGraphECS = ecs; }
    void setSnippetResources(std::function<std::vector<std::pair<std::string, entt::entity>>()> get) {
        m_getSnippetResources = std::move(get);
    }

    /// Called when the user picks "New…" in the zone snippet dropdown.
    /// Should create + register a new empty G-code snippet resource and return its entity.
    void setOnCreateSnippet(std::function<entt::entity(const std::string& suggestedName)> cb) {
        m_onCreateSnippet = std::move(cb);
    }

    void setOnRegenerateGcode(std::function<void()> cb) { m_onRegenerate = std::move(cb); }

    /// Called when the user clicks "Export per layer…" — app generates G-code
    /// for each visible layer and pushes the results to Resources / Code Editor.
    void setOnExportLayersPerFile(std::function<void()> cb) { m_onExportLayersPerFile = std::move(cb); }

    /// Called when the user clicks "Export per layer to folder…" — app writes
    /// one .gcode file per visible layer into a user-chosen folder.
    void setOnExportLayersToFolder(std::function<void()> cb) { m_onExportLayersToFolder = std::move(cb); }

    /// Capture drawable ECS content into a new plot layer (renderer hook).
    void setOnCaptureScene(std::function<void(bool outlineFillsOnly)> cb) {
        m_onCaptureScene = std::move(cb);
    }

    /// Re-import entities tagged plotter.plottable.
    void setOnSyncPlottables(std::function<void()> cb) { m_onSyncPlottables = std::move(cb); }

    /// Called when Target zone selection or draw-target geometry changes.
    void setOnDrawTargetChanged(std::function<void()> cb) { m_onDrawTargetChanged = std::move(cb); }

    /// Callback to read the current text of a G-code snippet by entity.
    void setGetSnippetText(std::function<std::string(entt::entity)> fn) {
        m_getSnippetText = std::move(fn);
    }

    const std::string& drawTargetZoneId() const;

    bool runPipelineOnExport() const { return m_runPipelineOnExport; }
    bool writeBackToPathsOnExport() const { return m_writeBackToPaths; }

    std::string preamble() const {
        if (m_preambleEntity != entt::null && m_getSnippetText)
            return m_getSnippetText(m_preambleEntity);
        return m_preamble;
    }
    std::string postamble() const {
        if (m_postambleEntity != entt::null && m_getSnippetText)
            return m_getSnippetText(m_postambleEntity);
        return m_postamble;
    }
    bool allowRotationWhenFitting() const { return m_allowRotationWhenFitting; }

    struct ProjectState {
        bool runPipelineOnExport = true;
        bool writeBackToPaths = true;
        bool allowRotationWhenFitting = false;
        std::string preambleResourceName;
        std::string postambleResourceName;
    };

    void captureProjectState(ProjectState& out) const;
    void applyProjectState(const ProjectState& state);
    void resolveProjectSnippetEntities(const std::vector<std::pair<std::string, entt::entity>>& snippets);

    const std::vector<glm::vec2>& injectionMarkersContent() const { return m_injectionMarkers; }

    enum GridGeneratorStyle {
        GridStyleLines = 0,
        GridStyleDots = 1,
        GridStyleCrosses = 2,
    };

    int   gridDivX() const { return m_gridDivX; }
    int   gridDivY() const { return m_gridDivY; }
    float gridMargin() const { return m_gridMargins.left; }
    const plotter::ZoneMarginsMM& gridMargins() const { return m_gridMargins; }
    bool  gridSquare() const { return m_gridSquare; }
    int   gridStyle() const { return m_gridStyle; }
    float gridMarkSize() const { return m_gridMarkSize; }
    void  setGridSettings(int divX, int divY, float margin, bool square,
                          int style = GridStyleLines, float markSize = 2.f);
    void  setGridSettings(int divX, int divY, const plotter::ZoneMarginsMM& margins, bool square,
                          int style = GridStyleLines, float markSize = 2.f);

    int   fractalTreeDepth() const { return m_fractalTreeDepth; }
    float fractalBranchAngleDeg() const { return m_fractalBranchAngleDeg; }
    float fractalLengthScale() const { return m_fractalLengthScale; }
    float fractalTrunkRatio() const { return m_fractalTrunkRatio; }
    const plotter::ZoneMarginsMM& fractalTreeMargins() const { return m_fractalTreeMargins; }
    void  setFractalTreeSettings(int depth, float branchAngleDeg, float lengthScale,
                                 float trunkRatio, const plotter::ZoneMarginsMM& margins);

    int   lSystemPreset() const { return m_lsystemPreset; }
    int   lSystemSteps() const { return m_lsystemSteps; }
    float lSystemAngleDeg() const { return m_lsystemAngleDeg; }
    float lSystemConstantR() const { return m_lsystemConstantR; }
    bool  lSystemDrawFlowers() const { return m_lsystemDrawFlowers; }
    unsigned int lSystemSeed() const { return m_lsystemSeed; }
    const plotter::ZoneMarginsMM& lSystemMargins() const { return m_lsystemMargins; }
    void setLSystemSettings(int preset, int steps, float angleDeg, float constantR,
                            const plotter::ZoneMarginsMM& margins,
                            bool drawFlowers = true, unsigned int seed = 1);
    void setLSystemCustomText(const std::string& axiom, const std::string& rules);
    std::string lSystemAxiomText() const { return m_lsystemAxiomBuf; }
    std::string lSystemRulesText() const { return m_lsystemRulesBuf; }

    int   ofxLSystemPreset() const { return m_ofxLsystemPreset; }
    int   ofxLSystemDepth() const { return m_ofxLsystemDepth; }
    float ofxLSystemTheta() const { return m_ofxLsystemTheta; }
    float ofxLSystemStepLength() const { return m_ofxLsystemStepLength; }
    bool  ofxLSystemRandomYRotation() const { return m_ofxLsystemRandomYRotation; }
    int   ofxLSystemProjection() const { return m_ofxLsystemProjection; }
    const plotter::ZoneMarginsMM& ofxLSystemMargins() const { return m_ofxLsystemMargins; }
    void setOfxLSystemSettings(int preset, int depth, float theta, float stepLength,
                               bool randomYRotation, int projection,
                               const plotter::ZoneMarginsMM& margins);
    void setOfxLSystemCustomText(const std::string& axiom, const std::string& rules);
    void setOfxLSystemConstantsText(const std::string& constants);
    std::string ofxLSystemAxiomText() const { return m_ofxLsystemAxiomBuf; }
    std::string ofxLSystemRulesText() const { return m_ofxLsystemRulesBuf; }
    std::string ofxLSystemConstantsText() const { return m_ofxLsystemConstantsBuf; }

    int   borderStyle() const { return m_borderStyle; }
    float borderInset() const { return m_borderInset; }
    float borderCornerRadius() const { return m_borderCornerRadius; }
    int   borderLoopCount() const { return m_borderLoopCount; }
    float borderLoopSpacing() const { return m_borderLoopSpacing; }
    const plotter::ZoneMarginsMM& borderMargins() const { return m_borderMargins; }
    void setBorderSettings(int style, float inset, float cornerRadius,
                           int loopCount, float loopSpacing,
                           const plotter::ZoneMarginsMM& margins);

    float cropmarksLength() const { return m_cropmarksLength; }
    float cropmarksInset() const { return m_cropmarksInset; }
    const plotter::ZoneMarginsMM& cropmarksMargins() const { return m_cropmarksMargins; }
    void setCropmarksSettings(float length, float inset,
                              const plotter::ZoneMarginsMM& margins);

    void rebuildInjectionMarkers();
    void markInjectionMarkersDirty() { m_injectionMarkersDirty = true; }
    void ensureInjectionMarkersFresh();
    void draw(const char* title, bool& visible);

    /// Poll background generator jobs (L-system). Call once per frame from ofApp::update().
    void updateBackgroundJobs();
    bool isBackgroundBusy() const;
    void shutdownBackgroundJobs();



private:

    /// One slot in the Generators chain. Layer entity + ofJson settings live here
    /// so any registered path generator works without dedicated panel members.
    struct GeneratorChainEntry {
        plotgen::PlotGeneratorId id;
        bool                     enabled = true;
        ofJson                   settings; ///< from IPlotGenerator::defaultSettings(); generic UI edits this
        entt::entity             layerEnt { entt::null };
    };

    void drawSourceTab();
    void drawToolpathsTab();
    void drawPenMachineSection(PlotDoc* eng);
    void drawGcodeTab();
    void drawEffectChainUI(plotter::EffectPhase phase, const char* sectionTitle,
                           const char* payloadTag, const char* addLabel);
    void drawEffectStepPropertiesLink(plotter::EffectPhase phase, int stepIndex);
    void drawGeneratorsUI();
    void ensureGeneratorInChain(const plotgen::PlotGeneratorId& id);
    void drawGeneratorStepBody(int chainIndex);
    void drawGridGeneratorBody();
    void drawFractalTreeGeneratorBody();
    void drawLSystemGeneratorBody();
    void drawOfxLSystemGeneratorBody();
    void drawBorderGeneratorBody();
    void drawCropmarksGeneratorBody();
    /// Fallback UI for any registered path generator without a specialized body
    /// (edits ofJson settings from defaultSettings(), Generate/Clear via registry).
    void drawGenericGeneratorBody(const plotgen::PlotGeneratorId& id);
    void generateGrid();
    void generateFractalTree();
    void generateLSystemPlant();
    void generateOfxLSystem();
    void generateBorder();
    void generateCropmarks();
    void generateGeneric(const plotgen::PlotGeneratorId& id);
    void clearGridLayer();
    void clearFractalTreeLayer();
    void clearLSystemPlantLayer();
    void clearOfxLSystemLayer();
    void clearBorderLayer();
    void clearCropmarksLayer();
    void clearGenericLayer(const plotgen::PlotGeneratorId& id);
    void pinLayerFirst(entt::entity layerEnt);
    void applyLSystemPresetToBuffers(int presetIndex);
    void applyOfxLSystemPresetToBuffers(int presetIndex);
    GeneratorChainEntry* findGeneratorEntry(const plotgen::PlotGeneratorId& id);
    const GeneratorChainEntry* findGeneratorEntry(const plotgen::PlotGeneratorId& id) const;
    entt::entity& generatorLayerEnt(const plotgen::PlotGeneratorId& id);
    entt::entity prepareGeneratorLayer(entt::entity& cachedLayer, const std::string& layerName);
    void clearGeneratorLayerImpl(entt::entity& cachedLayer, const std::string& layerName);
    void notifyGeneratorPathsChanged();
    void onFilterChainChanged();
    plotgen::GeneratorProgressHooks generatorProgressHooks() const;

    PlotDoc*              m_engine = nullptr;

    plotter::PlotterZoneStore* m_zones  = nullptr;
    plotter::PlotterEffectGraphECS* m_effectGraphECS = nullptr;

    grbl::MachinePrefs*       m_prefs  = nullptr;

    plotproc::PlotPipeline*   m_pipeline = nullptr;

    PlotterGenerateBindings   m_gen;

    std::function<std::vector<std::pair<std::string, entt::entity>>()> m_getSnippetResources;
    std::function<entt::entity(const std::string&)>                  m_onCreateSnippet;
    std::function<std::string(entt::entity)>                         m_getSnippetText;
    std::function<void()>     m_onRegenerate;
    std::function<void()>     m_onExportLayersPerFile;
    std::function<void()>     m_onExportLayersToFolder;
    std::function<void(bool)> m_onCaptureScene;
    std::function<void()>     m_onSyncPlottables;
    std::function<void()>     m_onDrawTargetChanged;

    int                       m_tab = 0;
    std::vector<glm::vec2>    m_injectionMarkers;

    bool m_runPipelineOnExport = true;
    bool m_writeBackToPaths = true;

    std::string m_preamble;   ///< fallback text when no snippet entity is selected
    std::string m_postamble;  ///< fallback text when no snippet entity is selected
    entt::entity m_preambleEntity  = entt::null;
    std::string  m_preambleResourceName;
    entt::entity m_postambleEntity = entt::null;
    std::string  m_postambleResourceName;
    bool        m_allowRotationWhenFitting = false;
    bool        m_captureOutlineFillsOnly  = true;

    char        m_penPresetSaveBuf[128] = "New preset";

    ofkitty::ChainEditor m_imageChainEditor;
    ofkitty::ChainEditor m_drawChainEditor;
    ofkitty::ChainEditor m_generatorChainEditor;

    std::vector<GeneratorChainEntry> m_generatorChain {{{"path", "grid"}, true}};

    int   m_gridDivX   {10};
    int   m_gridDivY   {10};
    plotter::ZoneMarginsMM m_gridMargins;
    bool  m_gridSquare {false};
    int   m_gridStyle {GridStyleLines};
    float m_gridMarkSize {2.f};
    bool  m_injectionMarkersDirty {true};

    int   m_fractalTreeDepth {8};
    float m_fractalBranchAngleDeg {28.f};
    float m_fractalLengthScale {0.67f};
    float m_fractalTrunkRatio {0.45f};
    plotter::ZoneMarginsMM m_fractalTreeMargins;

    int   m_lsystemPreset {0};
    int   m_lsystemSteps {5};
    float m_lsystemAngleDeg {25.f};
    float m_lsystemConstantR {1.456f};
    plotter::ZoneMarginsMM m_lsystemMargins;
    char  m_lsystemAxiomBuf[256] {"F"};
    char  m_lsystemRulesBuf[2048] {"F -> F[+F]F[-F][F]"};
    bool  m_lsystemDrawFlowers {true};
    unsigned int m_lsystemSeed {1};

    int   m_ofxLsystemPreset {0};
    int   m_ofxLsystemDepth {2};
    float m_ofxLsystemTheta {25.f};
    float m_ofxLsystemStepLength {10.f};
    bool  m_ofxLsystemRandomYRotation {false};
    int   m_ofxLsystemProjection {0};
    plotter::ZoneMarginsMM m_ofxLsystemMargins;
    char  m_ofxLsystemAxiomBuf[256] {"F"};
    char  m_ofxLsystemRulesBuf[2048] {"F -> FF+[+F-F-F]-[-F+F+F]"};
    char  m_ofxLsystemConstantsBuf[128] {};

    int   m_borderStyle {0};
    float m_borderInset {5.f};
    float m_borderCornerRadius {8.f};
    int   m_borderLoopCount {3};
    float m_borderLoopSpacing {4.f};
    plotter::ZoneMarginsMM m_borderMargins;

    float m_cropmarksLength {10.f};
    float m_cropmarksInset {5.f};
    plotter::ZoneMarginsMM m_cropmarksMargins;

    plotgen::PathGeneratorJob m_lsystemPlantJob;
    plotgen::PathGeneratorJob m_lsystem3dJob;

};

} // namespace plotter::kit
