#include "GcodeGeneratorPanel.h"
#include "PlotDoc.h"
#include "PlottableDefaults.h"
#include "PenSettingsPresets.h"
#include "PlotterBedCoords.h"
#include "kit/PlotterGeneratorLayerNames.h"
#include "windows/PlotterFeedRateUi.h"
#include "PlotterFilterStepUi.h"
#include "PlotterGCodeInjector.h"
#include "PlotterZoneComponents.h"
#include "ProgressWindow.h"
#include "ofxGCode.hpp"
#include "ofxPlotGenerators.h"
#include "ofxPlotGeneratorsLSystem.h"
#include "GeneratorLayerHelper.h"
#include "components/base_components.h"
#include "imgui.h"

#include <MachinePrefs.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace plotter::kit {

namespace {

/// Path generators currently registered (built-ins + linked addons such as L-System).
std::vector<plotgen::PlotGeneratorId> kitPathGeneratorIds()
{
    return plotgen::GeneratorRegistry::instance()
        .listIds(plotgen::GeneratorOutputKind::Path);
}

/// Combo labels for ChainEditor add-menus: "(add …)" + registry/API ids.
std::vector<std::string> effectAddTypeLabels(plotter::EffectPhase phase)
{
    std::vector<std::string> labels;
    labels.emplace_back("(add filter)");
    const auto ids = (phase == plotter::EffectPhase::Image)
        ? plotter::imageEffectIds()
        : plotter::drawEffectIds();
    labels.insert(labels.end(), ids.begin(), ids.end());
    return labels;
}

const char* generatorDisplayName(const plotgen::PlotGeneratorId& id)
{
    if (const char* name = generatorLayerName(id); name && name[0] != '\0')
        return name;
    return "Generator";
}

/// Stable ids for the panel's per-generator layer entity slots.
const plotgen::PlotGeneratorId kGridId         { "path", "grid" };
const plotgen::PlotGeneratorId kFractalTreeId  { "path", "fractal_tree" };
const plotgen::PlotGeneratorId kLSystemPlantId { "path", "lsystem_plant" };
const plotgen::PlotGeneratorId kLSystem3dId    { "path", "lsystem_3d" };
const plotgen::PlotGeneratorId kBorderId       { "path", "border" };
const plotgen::PlotGeneratorId kCropmarksId    { "path", "cropmarks" };

ofJson lSystemPlantSettingsJson(int preset, int steps, float angleDeg, float constantR,
                                bool drawFlowers, unsigned int seed,
                                const char* axiomBuf, const char* rulesBuf)
{
    return ofJson{
        {"preset", preset},
        {"steps", steps},
        {"angleDeg", angleDeg},
        {"constantR", constantR},
        {"drawFlowers", drawFlowers},
        {"seed", seed},
        {"axiom", axiomBuf ? std::string(axiomBuf) : std::string()},
        {"rules", rulesBuf ? std::string(rulesBuf) : std::string()},
    };
}

ofJson ofxLSystemPanelSettingsJson(int preset, int depth, float theta, float stepLength,
                                   bool randomYRotation, int projection,
                                   const char* axiomBuf, const char* rulesBuf,
                                   const char* constantsBuf)
{
    return ofJson{
        {"preset", preset},
        {"depth", depth},
        {"theta", theta},
        {"stepLength", stepLength},
        {"randomYRotation", randomYRotation},
        {"projection", projection},
        {"axiom", axiomBuf ? std::string(axiomBuf) : std::string()},
        {"rules", rulesBuf ? std::string(rulesBuf) : std::string()},
        {"constants", constantsBuf ? std::string(constantsBuf) : std::string()},
    };
}

struct DrawTargetInnerRect {
    float x0, y0, x1, y1;
    float innerW, innerH;
};

bool queryDrawTargetInnerRect(plotter::PlotterZoneStore* zones,
                              const plotter::ZoneMarginsMM& margins,
                              DrawTargetInnerRect& out)
{
    float zoneW = 210.f, zoneH = 297.f;
    if (zones) {
        auto& reg = ofkitty::runtime().registry();
        const entt::entity target = zones->findDrawTargetZone(reg);
        if (target != entt::null && reg.all_of<plotter::machine_zone_component>(target)) {
            const auto& z = reg.get<plotter::machine_zone_component>(target);
            zoneW = z.w;
            zoneH = z.h;
        }
    }

    out.innerW = zoneW - margins.left - margins.right;
    out.innerH = zoneH - margins.top - margins.bottom;
    if (out.innerW <= 0.f || out.innerH <= 0.f) return false;

    out.x0 = margins.left;
    out.y0 = margins.top;
    out.x1 = zoneW - margins.right;
    out.y1 = zoneH - margins.bottom;
    return true;
}

/// Classic binary fractal tree (Fractal Foundation style):
/// recurse with left/right branches scaled and rotated from each tip.
// (implementation moved to ofxPlotGenerators FractalTreeGenerator)

/// DragFloat4 width for L/R/T/B margin fields (four digits per side).
inline float generatorMarginDragWidth()
{
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fieldW = ImGui::CalcTextSize("9999").x + st.FramePadding.x * 2.f + 8.f;
    return fieldW * 4.f + st.ItemInnerSpacing.x * 3.f;
}

enum class BorderRectStyle {
    Rectangle = 0,
    Rounded,
    Loops,
};

const char* kBorderStyleLabels[] = {
    "Rectangle", "Rounded corners", "Concentric loops"
};

} // namespace


// -------------------------------------------------------------------------------------------------------

const std::string& GcodeGeneratorPanel::drawTargetZoneId() const
{
    static const std::string empty;
    return m_zones ? m_zones->drawTargetZoneId : empty;
}


void GcodeGeneratorPanel::rebuildInjectionMarkers()

{

    m_injectionMarkers.clear();
    m_injectionMarkersDirty = false;

    if (!m_engine || !m_prefs) return;
    auto& reg = ofkitty::runtime().registry();
    const auto rules = plotter::collectInjectionRules(reg);
    if (rules.empty()) return;



    ofxGCode g;

    g.setup(1.0f);

    for (const auto& outline : m_engine->getPaths()) {

        if (outline.size() < 2) continue;

        std::vector<ofVec2f> pts;

        for (const auto& v : outline.getVertices())

            pts.push_back({v.x, v.y});

        g.polygon(pts, outline.isClosed());

    }



    plotter::BedView bed = plotter::BedView::fromPrefs(*m_prefs);
    const plotter::PreviewBounds preview = plotter::PreviewBounds::fromEnvelope(bed);

    const float ox = bed.bed.paperOriginX;

    const float oy = bed.bed.paperOriginY;

    for (auto& ln : g.lines) {

        ln.a.x += ox;

        ln.a.y += oy;

        ln.b.x += ox;

        ln.b.y += oy;

    }



    const auto breaks = plotter::computeInjectionBreaks(

        g.lines, rules, false);

    m_injectionMarkers = plotter::injectionMarkersContent(breaks, preview);

}

void GcodeGeneratorPanel::ensureInjectionMarkersFresh()
{
    if (!m_injectionMarkersDirty) return;
    rebuildInjectionMarkers();
}


void GcodeGeneratorPanel::setGridSettings(int divX, int divY, float margin, bool square,
                                            int style, float markSize)
{
    m_gridDivX = divX;
    m_gridDivY = divY;
    m_gridMargins.setUniform(margin);
    m_gridSquare = square;
    m_gridStyle = std::clamp(style, static_cast<int>(GridStyleLines), static_cast<int>(GridStyleCrosses));
    m_gridMarkSize = std::max(0.1f, markSize);
    ensureGeneratorInChain({"path", "grid"});
}

void GcodeGeneratorPanel::setGridSettings(int divX, int divY,
                                          const plotter::ZoneMarginsMM& margins, bool square,
                                          int style, float markSize)
{
    m_gridDivX = divX;
    m_gridDivY = divY;
    m_gridMargins = margins;
    m_gridSquare = square;
    m_gridStyle = std::clamp(style, static_cast<int>(GridStyleLines), static_cast<int>(GridStyleCrosses));
    m_gridMarkSize = std::max(0.1f, markSize);
    ensureGeneratorInChain({"path", "grid"});
}

void GcodeGeneratorPanel::setFractalTreeSettings(int depth, float branchAngleDeg,
                                                 float lengthScale, float trunkRatio,
                                                 const plotter::ZoneMarginsMM& margins)
{
    m_fractalTreeDepth       = depth;
    m_fractalBranchAngleDeg  = branchAngleDeg;
    m_fractalLengthScale     = lengthScale;
    m_fractalTrunkRatio      = trunkRatio;
    m_fractalTreeMargins     = margins;
    ensureGeneratorInChain({"path", "fractal_tree"});
}

void GcodeGeneratorPanel::setLSystemSettings(int preset, int steps, float angleDeg,
                                               float constantR,
                                               const plotter::ZoneMarginsMM& margins,
                                               bool drawFlowers, unsigned int seed)
{
    m_lsystemPreset       = preset;
    m_lsystemSteps        = steps;
    m_lsystemAngleDeg     = angleDeg;
    m_lsystemConstantR    = constantR;
    m_lsystemMargins      = margins;
    m_lsystemDrawFlowers  = drawFlowers;
    m_lsystemSeed         = seed;
    ensureGeneratorInChain({"path", "lsystem_plant"});
    if (preset >= 0 && preset < (int)plotter::lSystemPresets().size())
        applyLSystemPresetToBuffers(preset);
}

void GcodeGeneratorPanel::setLSystemCustomText(const std::string& axiom,
                                               const std::string& rules)
{
    std::strncpy(m_lsystemAxiomBuf, axiom.c_str(), sizeof(m_lsystemAxiomBuf) - 1);
    m_lsystemAxiomBuf[sizeof(m_lsystemAxiomBuf) - 1] = '\0';
    std::strncpy(m_lsystemRulesBuf, rules.c_str(), sizeof(m_lsystemRulesBuf) - 1);
    m_lsystemRulesBuf[sizeof(m_lsystemRulesBuf) - 1] = '\0';
}

void GcodeGeneratorPanel::setOfxLSystemSettings(int preset, int depth, float theta,
                                                float stepLength, bool randomYRotation,
                                                int projection,
                                                const plotter::ZoneMarginsMM& margins)
{
    m_ofxLsystemPreset          = preset;
    m_ofxLsystemDepth           = depth;
    m_ofxLsystemTheta           = theta;
    m_ofxLsystemStepLength      = stepLength;
    m_ofxLsystemRandomYRotation = randomYRotation;
    m_ofxLsystemProjection      = projection;
    m_ofxLsystemMargins         = margins;
    ensureGeneratorInChain({"path", "lsystem_3d"});
    if (preset >= 0 && preset < (int)plotter::ofxLSystemPresets().size())
        applyOfxLSystemPresetToBuffers(preset);
}

void GcodeGeneratorPanel::setOfxLSystemCustomText(const std::string& axiom,
                                                  const std::string& rules)
{
    std::strncpy(m_ofxLsystemAxiomBuf, axiom.c_str(), sizeof(m_ofxLsystemAxiomBuf) - 1);
    m_ofxLsystemAxiomBuf[sizeof(m_ofxLsystemAxiomBuf) - 1] = '\0';
    std::strncpy(m_ofxLsystemRulesBuf, rules.c_str(), sizeof(m_ofxLsystemRulesBuf) - 1);
    m_ofxLsystemRulesBuf[sizeof(m_ofxLsystemRulesBuf) - 1] = '\0';
}

void GcodeGeneratorPanel::setOfxLSystemConstantsText(const std::string& constants)
{
    std::strncpy(m_ofxLsystemConstantsBuf, constants.c_str(),
                 sizeof(m_ofxLsystemConstantsBuf) - 1);
    m_ofxLsystemConstantsBuf[sizeof(m_ofxLsystemConstantsBuf) - 1] = '\0';
}

void GcodeGeneratorPanel::setBorderSettings(int style, float inset, float cornerRadius,
                                            int loopCount, float loopSpacing,
                                            const plotter::ZoneMarginsMM& margins)
{
    m_borderStyle         = style;
    m_borderInset         = inset;
    m_borderCornerRadius  = cornerRadius;
    m_borderLoopCount     = loopCount;
    m_borderLoopSpacing   = loopSpacing;
    m_borderMargins       = margins;
    ensureGeneratorInChain({"path", "border"});
}

void GcodeGeneratorPanel::setCropmarksSettings(float length, float inset,
                                               const plotter::ZoneMarginsMM& margins)
{
    m_cropmarksLength  = length;
    m_cropmarksInset   = inset;
    m_cropmarksMargins = margins;
    ensureGeneratorInChain({"path", "cropmarks"});
}

void GcodeGeneratorPanel::applyLSystemPresetToBuffers(int presetIndex)
{
    const auto& presets = plotter::lSystemPresets();
    if (presetIndex < 0 || presetIndex >= (int)presets.size()) return;

    const auto& p = presets[presetIndex];
    std::strncpy(m_lsystemAxiomBuf, p.axiom, sizeof(m_lsystemAxiomBuf) - 1);
    m_lsystemAxiomBuf[sizeof(m_lsystemAxiomBuf) - 1] = '\0';
    if (presetIndex == plotter::kLSystemStochasticTreePreset) {
        const std::string rules = plotter::stochasticTreeRulesDisplayText(m_lsystemDrawFlowers);
        std::strncpy(m_lsystemRulesBuf, rules.c_str(), sizeof(m_lsystemRulesBuf) - 1);
    } else {
        std::strncpy(m_lsystemRulesBuf, p.rulesText, sizeof(m_lsystemRulesBuf) - 1);
    }
    m_lsystemRulesBuf[sizeof(m_lsystemRulesBuf) - 1] = '\0';
    m_lsystemAngleDeg = p.angleDeg;
    if (p.constantR > 0.f)
        m_lsystemConstantR = p.constantR;
}

void GcodeGeneratorPanel::applyOfxLSystemPresetToBuffers(int presetIndex)
{
    const auto& presets = plotter::ofxLSystemPresets();
    if (presetIndex < 0 || presetIndex >= (int)presets.size()) return;

    const auto& p = presets[presetIndex];
    std::strncpy(m_ofxLsystemAxiomBuf, p.axiom, sizeof(m_ofxLsystemAxiomBuf) - 1);
    m_ofxLsystemAxiomBuf[sizeof(m_ofxLsystemAxiomBuf) - 1] = '\0';
    std::strncpy(m_ofxLsystemRulesBuf, p.rulesText, sizeof(m_ofxLsystemRulesBuf) - 1);
    m_ofxLsystemRulesBuf[sizeof(m_ofxLsystemRulesBuf) - 1] = '\0';
    m_ofxLsystemTheta           = p.theta;
    m_ofxLsystemRandomYRotation = p.randomYRotation;
    if (p.constants && p.constants[0] != '\0') {
        std::strncpy(m_ofxLsystemConstantsBuf, p.constants,
                     sizeof(m_ofxLsystemConstantsBuf) - 1);
        m_ofxLsystemConstantsBuf[sizeof(m_ofxLsystemConstantsBuf) - 1] = '\0';
    } else {
        m_ofxLsystemConstantsBuf[0] = '\0';
    }
}

void GcodeGeneratorPanel::captureProjectState(ProjectState& out) const
{
    out.runPipelineOnExport      = m_runPipelineOnExport;
    out.writeBackToPaths         = m_writeBackToPaths;
    out.allowRotationWhenFitting = m_allowRotationWhenFitting;
    out.preambleResourceName     = m_preambleResourceName;
    out.postambleResourceName    = m_postambleResourceName;
}

void GcodeGeneratorPanel::applyProjectState(const ProjectState& state)
{
    m_runPipelineOnExport      = state.runPipelineOnExport;
    m_writeBackToPaths         = state.writeBackToPaths;
    m_allowRotationWhenFitting = state.allowRotationWhenFitting;
    m_preambleResourceName     = state.preambleResourceName;
    m_postambleResourceName    = state.postambleResourceName;
    m_preambleEntity           = entt::null;
    m_postambleEntity          = entt::null;
}

void GcodeGeneratorPanel::resolveProjectSnippetEntities(
    const std::vector<std::pair<std::string, entt::entity>>& snippets)
{
    auto resolve = [&](const std::string& name) -> entt::entity {
        if (name.empty()) return entt::null;
        for (const auto& s : snippets)
            if (s.first == name) return s.second;
        return entt::null;
    };
    m_preambleEntity  = resolve(m_preambleResourceName);
    m_postambleEntity = resolve(m_postambleResourceName);
}

void GcodeGeneratorPanel::onFilterChainChanged()
{
    if (m_gen.onEffectGraphChanged)
        m_gen.onEffectGraphChanged();
    if (m_gen.refreshFilterPreview)
        m_gen.refreshFilterPreview();
    else if (m_gen.markCanvasDirty)
        m_gen.markCanvasDirty((unsigned)plotter::CanvasCacheMask::ToolpathPreview);
}

void GcodeGeneratorPanel::drawEffectStepPropertiesLink(plotter::EffectPhase phase, int stepIndex)
{
    if (!m_effectGraphECS || !m_engine) return;

    auto& reg = ofkitty::runtime().registry();
    const entt::entity stepEntity =
        m_effectGraphECS->chainEntityAt(reg, phase, stepIndex);
    if (stepEntity == entt::null) {
        ImGui::TextDisabled("(no filter entity)");
        return;
    }

    if (drawFilterStepProperties(reg, stepEntity))
        onFilterChainChanged();
}



void GcodeGeneratorPanel::drawEffectChainUI(plotter::EffectPhase phase,
                                            const char* sectionTitle,
                                            const char* payloadTag,
                                            const char* addLabel)
{

    if (!m_engine || !m_effectGraphECS) return;

    ofkitty::ChainEditor& ed = (phase == plotter::EffectPhase::Image)
        ? m_imageChainEditor : m_drawChainEditor;

    ed.setPayloadTag(payloadTag);

    ed.setSectionTitle(sectionTitle);

    ed.setShowDragHandle(false);

    ed.setStepCount(m_effectGraphECS->stepCount(ofkitty::runtime().registry(), phase));

    ed.setStepLabel([this, phase](int i) {

        return m_effectGraphECS->stepEffectId(ofkitty::runtime().registry(), phase, i);

    });

    ed.setIsEnabled([this, phase](int i) {

        return m_effectGraphECS->stepEnabled(ofkitty::runtime().registry(), phase, i);

    });

    ed.setSetEnabled([this, phase](int i, bool on) {

        m_effectGraphECS->setStepEnabled(ofkitty::runtime().registry(), phase, i, on);

        onFilterChainChanged();

    });

    ed.setDrawStepBody([this, phase](int i) {
        drawEffectStepPropertiesLink(phase, i);
    });

    ed.setOnMove([this, phase](int from, int to) {

        m_effectGraphECS->moveStep(ofkitty::runtime().registry(), phase, from, to);

        onFilterChainChanged();

    });

    ed.setOnRemove([this, phase](int i) {

        m_effectGraphECS->removeStepAt(ofkitty::runtime().registry(), phase, i);

        onFilterChainChanged();

    });



    const std::vector<std::string> addTypes = effectAddTypeLabels(phase);
    ed.setAddTypes(addTypes);

    ed.setOnAdd([this, phase, addTypes](int typeIndex) {
        if (typeIndex <= 0 || typeIndex >= (int)addTypes.size()) return;
        m_effectGraphECS->addStep(ofkitty::runtime().registry(), phase, addTypes[(size_t)typeIndex]);
        onFilterChainChanged();
    });

    ed.setFooterHint(addLabel);

    ed.draw();
}



void GcodeGeneratorPanel::drawSourceTab()

{

    PlotDoc* eng = m_gen.engine ? m_gen.engine : m_engine;

    if (!eng) {

        ImGui::TextDisabled("No engine attached.");

        return;

    }

    if (ImGui::CollapsingHeader("Source", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto allRes = m_gen.getAllResources ? m_gen.getAllResources()
                                                  : std::vector<ofkitty::Resource>{};

        const char* preview = "Select source...";
        static int activeResIdx = -1;
        if (activeResIdx >= 0 && activeResIdx < (int)allRes.size()) {
            preview = allRes[activeResIdx].name.c_str();
        } else if (m_gen.imageName && !m_gen.imageName->empty()) {
            preview = m_gen.imageName->c_str();
        }

        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::BeginCombo("##source_picker", preview)) {
            if (ImGui::Selectable("Load...")) {
                if (m_gen.loadAnySourceDialog) m_gen.loadAnySourceDialog();
                ImGui::CloseCurrentPopup();
            }
            if (!allRes.empty())
                ImGui::Separator();
            for (int i = 0; i < (int)allRes.size(); ++i) {
                const auto& r = allRes[i];
                std::string badge;
                if      (r.type == ofkitty::ResourceType::VectorSVG)   badge = "[SVG] ";
                else if (r.type == ofkitty::ResourceType::VectorDXF)   badge = "[DXF] ";
                else if (r.type == ofkitty::ResourceType::Image)        badge = "[IMG] ";
                else if (r.type == ofkitty::ResourceType::GCodeSnippet) badge = "[TXT] ";
                const std::string label = badge + r.name;
                if (ImGui::Selectable(label.c_str(), activeResIdx == i)) {
                    activeResIdx = i;
                    if (m_gen.placeSourceResource) m_gen.placeSourceResource(r);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();

        if (m_gen.scaleActual && ImGui::Button("100%")) m_gen.scaleActual();

        ImGui::SameLine();

        if (m_gen.scaleFit && ImGui::Button("Scale to Fit")) m_gen.scaleFit();

        ImGui::Checkbox("Allow rotation when fitting", &m_allowRotationWhenFitting);

        if (m_gen.svgImportMode) {
            static const char* kModes[] = { "Groups as Layers", "Colours as Layers", "Single Layer" };
            int modeIdx = (int)*m_gen.svgImportMode;
            ImGui::SetNextItemWidth(160.f);
            if (ImGui::Combo("Import mode##svgimport", &modeIdx, kModes, IM_ARRAYSIZE(kModes))) {
                *m_gen.svgImportMode = (PlotDoc::SvgImportMode)modeIdx;
                if (m_gen.reapplyImportMode) m_gen.reapplyImportMode();
                if (m_gen.onSourceChanged) m_gen.onSourceChanged();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How SVG/DXF sources split into layers.\n"
                                  "Changing it re-splits everything already placed.");
        }

        if (m_gen.svgScaleMode) {
            static const char* kScaleModes[] = {
                "1:1 Actual Size",
                "Fit to Zone",
                "Fit to Zone Margins"
            };
            int scaleIdx;
            switch (*m_gen.svgScaleMode) {
                case PlotDoc::SvgScaleMode::FitToZone:   scaleIdx = 1; break;
                case PlotDoc::SvgScaleMode::FitToCanvas: scaleIdx = 2; break;
                default:                                  scaleIdx = 0; break;
            }
            ImGui::SetNextItemWidth(160.f);
            if (ImGui::Combo("Scale##svgscale", &scaleIdx, kScaleModes, IM_ARRAYSIZE(kScaleModes))) {
                switch (scaleIdx) {
                    case 1:  *m_gen.svgScaleMode = PlotDoc::SvgScaleMode::FitToZone;   break;
                    case 2:  *m_gen.svgScaleMode = PlotDoc::SvgScaleMode::FitToCanvas; break;
                    default: *m_gen.svgScaleMode = PlotDoc::SvgScaleMode::ActualSize;  break;
                }
            }
        }

        if (m_gen.imageName && !m_gen.imageName->empty())
            ImGui::TextDisabled("%s", m_gen.imageName->c_str());
    }

    if (ImGui::CollapsingHeader("Scene Capture", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled(
            "Capture ECS 2D drawables via the plotter renderer\n"
            "(same hook as Cairo vector export).");
        ImGui::Checkbox("Outline fills only", &m_captureOutlineFillsOnly);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Filled shapes become closed outlines.\n"
                              "Uncheck to mark layers for fill rasterization.");

        const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Capture scene", ImVec2(btnW, 0))) {
            if (m_onCaptureScene) m_onCaptureScene(m_captureOutlineFillsOnly);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Draw all visible 2D ECS shapes into a new layer.");
        ImGui::SameLine();
        if (ImGui::Button("Sync tagged", ImVec2(btnW, 0))) {
            if (m_onSyncPlottables) m_onSyncPlottables();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Import entities tagged plotter.plottable.");
    }

    bool hasPaths = !eng->getPaths().empty();

    if (ImGui::CollapsingHeader("Transform")) {

        if (!hasPaths) ImGui::BeginDisabled();

        // ---- Rotation -------------------------------------------------------
        // Non-destructive — stored on the doc, applied at preview and export.
        float rotDeg = eng->pathRotationDeg;
        ImGui::SetNextItemWidth(80.f);
        const bool rotChanged = ImGui::DragFloat("Rotation##rot", &rotDeg, 1.f, -360.f, 360.f,
                                                 "%.0f\xc2\xb0");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Any angle — 45\xc2\xb0, 30\xc2\xb0, whatever you need.\n"
                "Non-destructive: reset to 0 to restore original orientation.\n"
                "Drag live to preview; G-code regenerates on commit (Enter / click away).");
        if (rotChanged) {
            eng->pathRotationDeg = rotDeg;
            eng->markCanvasDirty((unsigned)plotter::CanvasCacheMask::FlatPaths
                               | (unsigned)plotter::CanvasCacheMask::ToolpathPreview);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (m_gen.onSourceChanged) m_gen.onSourceChanged();
        }

        // ---- Scale ----------------------------------------------------------
        float pct = eng->pathScale * 100.f;
        ImGui::SetNextItemWidth(80.f);
        const bool scaleChanged = ImGui::DragFloat("Scale##scl", &pct, 1.f, 10.f, 1000.f, "%.0f%%");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Non-destructive — stored on the document, applied at preview and export.\n"
                "Drag live to preview; G-code regenerates when you commit (Enter / click away).\n"
                "Reset to 100%% to restore original size.");
        if (scaleChanged) {
            eng->pathScale = pct / 100.f;
            eng->markCanvasDirty((unsigned)plotter::CanvasCacheMask::FlatPaths
                               | (unsigned)plotter::CanvasCacheMask::ToolpathPreview);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (m_gen.onSourceChanged) m_gen.onSourceChanged(); // re-run G-code export
        }

        // ---- Flip -----------------------------------------------------------
        const float btnW =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        if (ImGui::Button("Flip H", ImVec2(btnW, 0))) {
            eng->flipHorizontal();
            if (m_gen.invalidateSvgLayouts) m_gen.invalidateSvgLayouts();
            if (m_gen.onSourceChanged)      m_gen.onSourceChanged();
            if (m_gen.markCanvasDirty)
                m_gen.markCanvasDirty((unsigned)plotter::CanvasCacheMask::ToolpathPreview);
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip V", ImVec2(btnW, 0))) {
            eng->flipVertical();
            if (m_gen.invalidateSvgLayouts) m_gen.invalidateSvgLayouts();
            if (m_gen.onSourceChanged)      m_gen.onSourceChanged();
            if (m_gen.markCanvasDirty)
                m_gen.markCanvasDirty((unsigned)plotter::CanvasCacheMask::ToolpathPreview);
        }

        if (!hasPaths) ImGui::EndDisabled();

        if (!hasPaths) ImGui::TextDisabled("Load an image or SVG first.");

    }

    drawEffectChainUI(plotter::EffectPhase::Image, "Image filters",
                      "PLOTTER_IMG_FX", "Pixel filters run before plot finder.");

    drawEffectChainUI(plotter::EffectPhase::Draw, "Draw filters",
                      "PLOTTER_DRAW_FX", "GL redraw filters at canvas resolution.");

    drawGeneratorsUI();
}

// -----------------------------------------------------------------------
// Generators
// -----------------------------------------------------------------------

GcodeGeneratorPanel::GeneratorChainEntry*
GcodeGeneratorPanel::findGeneratorEntry(const plotgen::PlotGeneratorId& id)
{
    for (auto& entry : m_generatorChain) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

const GcodeGeneratorPanel::GeneratorChainEntry*
GcodeGeneratorPanel::findGeneratorEntry(const plotgen::PlotGeneratorId& id) const
{
    for (const auto& entry : m_generatorChain) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

entt::entity& GcodeGeneratorPanel::generatorLayerEnt(const plotgen::PlotGeneratorId& id)
{
    ensureGeneratorInChain(id);
    if (auto* entry = findGeneratorEntry(id))
        return entry->layerEnt;
    // Unreachable for valid ids; keep a stable reference so callers never null-deref.
    static entt::entity s_dummy = entt::null;
    ofLogError("GcodeGeneratorPanel") << "generatorLayerEnt: missing chain entry for " << id.key();
    return s_dummy;
}

void GcodeGeneratorPanel::ensureGeneratorInChain(const plotgen::PlotGeneratorId& id)
{
    if (id.family.empty() || id.name.empty()) return;
    if (findGeneratorEntry(id)) return;

    GeneratorChainEntry entry;
    entry.id = id;
    entry.enabled = true;
    if (const plotgen::IPlotGenerator* gen = plotgen::GeneratorRegistry::instance().get(id))
        entry.settings = gen->defaultSettings();
    m_generatorChain.push_back(std::move(entry));
}

void GcodeGeneratorPanel::drawGeneratorStepBody(int chainIndex)
{
    if (chainIndex < 0 || chainIndex >= (int)m_generatorChain.size()) return;

    const plotgen::PlotGeneratorId& id = m_generatorChain[(size_t)chainIndex].id;
    if (id.family == "path" && id.name == "grid") drawGridGeneratorBody();
    else if (id.family == "path" && id.name == "fractal_tree") drawFractalTreeGeneratorBody();
    else if (id.family == "path" && id.name == "lsystem_plant") drawLSystemGeneratorBody();
    else if (id.family == "path" && id.name == "lsystem_3d") drawOfxLSystemGeneratorBody();
    else if (id.family == "path" && id.name == "border") drawBorderGeneratorBody();
    else if (id.family == "path" && id.name == "cropmarks") drawCropmarksGeneratorBody();
    else drawGenericGeneratorBody(id);
}

void GcodeGeneratorPanel::drawGeneratorsUI()
{
    ofkitty::ChainEditor& ed = m_generatorChainEditor;

    ed.setPayloadTag("PLOTTER_GENERATORS");
    ed.setSectionTitle("Generators");
    ed.setShowDragHandle(false);
    ed.setFooterHint(
        "Each generator writes to one layer. Hide unused layers in the Layers panel.\n"
        "Use Print Preview for G-code / plot output.");
    ed.setStepCount((int)m_generatorChain.size());

    ed.setStepLabel([this](int i) {
        return std::string(generatorDisplayName(m_generatorChain[(size_t)i].id));
    });

    ed.setIsEnabled([this](int i) {
        return m_generatorChain[(size_t)i].enabled;
    });

    ed.setSetEnabled([this](int i, bool on) {
        m_generatorChain[(size_t)i].enabled = on;
    });

    ed.setDrawStepBody([this](int i) {
        drawGeneratorStepBody(i);
    });

    ed.setOnMove([this](int from, int toInsert) {
        if (from < 0 || from >= (int)m_generatorChain.size()) return;
        toInsert = std::clamp(toInsert, 0, (int)m_generatorChain.size());
        auto entry = m_generatorChain[(size_t)from];
        m_generatorChain.erase(m_generatorChain.begin() + from);
        if (toInsert > from) --toInsert;
        m_generatorChain.insert(m_generatorChain.begin() + toInsert, entry);
    });

    ed.setOnRemove([this](int i) {
        if (i < 0 || i >= (int)m_generatorChain.size()) return;
        m_generatorChain.erase(m_generatorChain.begin() + i);
    });

    const std::vector<plotgen::PlotGeneratorId> addIds = kitPathGeneratorIds();
    std::vector<std::string> addTypes;
    addTypes.reserve(addIds.size() + 1);
    addTypes.emplace_back("(add generator)");
    for (const auto& id : addIds)
        addTypes.emplace_back(generatorDisplayName(id));
    ed.setAddTypes(std::move(addTypes));

    ed.setOnAdd([this, addIds](int typeIndex) {
        if (typeIndex <= 0 || typeIndex > (int)addIds.size()) return;
        ensureGeneratorInChain(addIds[(size_t)typeIndex - 1]);
    });

    ed.draw();
}

void GcodeGeneratorPanel::drawGridGeneratorBody()
{
    ImGui::PushID("GridBody");
    ImGui::SetNextItemWidth(80.f);
    ImGui::DragInt("Div X", &m_gridDivX, 1, 1, 200);
    ImGui::SetNextItemWidth(80.f);
    ImGui::DragInt("Div Y", &m_gridDivY, 1, 1, 200);
    ImGui::SetNextItemWidth(generatorMarginDragWidth());
    ImGui::DragFloat4("Margin L/R/T/B", &m_gridMargins.left, 0.5f, 0.f, 9999.f, "%.1f");
    if (ImGui::Button("Use document margin")) {
        if (m_zones) {
            auto& reg = ofkitty::runtime().registry();
            const entt::entity target = m_zones->findDrawTargetZone(reg);
            if (target != entt::null && reg.all_of<plotter::machine_zone_component>(target))
                m_gridMargins = reg.get<plotter::machine_zone_component>(target).margins;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy margin from the draw-target zone grid settings.");
    ImGui::Checkbox("Square grid", &m_gridSquare);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Equal cell size; grid is centred in the drawable area.");

    const char* gridStyles[] = {"Lines", "Dots", "Crosses"};
    ImGui::SetNextItemWidth(120.f);
    ImGui::Combo("Style", &m_gridStyle, gridStyles, IM_ARRAYSIZE(gridStyles));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lines: full grid. Dots/Crosses: marks at intersections only.");
    if (m_gridStyle != GridStyleLines) {
        ImGui::SetNextItemWidth(80.f);
        const char* sizeLabel = (m_gridStyle == GridStyleDots) ? "Dot diameter (mm)" : "Cross size (mm)";
        ImGui::DragFloat(sizeLabel, &m_gridMarkSize, 0.1f, 0.1f, 50.f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Dot diameter or cross arm length in millimetres.");
    }

    const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Generate Grid", ImVec2(btnW, 0)))
        generateGrid();
    ImGui::SameLine();
    if (ImGui::Button("Clear##grid", ImVec2(btnW, 0)))
        clearGridLayer();
    ImGui::PopID();
}

void GcodeGeneratorPanel::drawFractalTreeGeneratorBody()
{
    ImGui::PushID("FractalTreeBody");
    ImGui::TextDisabled(
        "Recursive binary tree: each tip splits into two shorter branches.\n"
        "Classic fractal-tree activity (Fractal Foundation).");

    ImGui::SetNextItemWidth(80.f);
    ImGui::DragInt("Depth", &m_fractalTreeDepth, 1, 1, 14);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Recursion levels (more = more branches). 8–10 is a good start.");

    ImGui::SetNextItemWidth(80.f);
    ImGui::DragFloat("Branch angle", &m_fractalBranchAngleDeg, 0.5f, 5.f, 75.f, "%.0f°");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Angle each child branch deviates from its parent.");

    ImGui::SetNextItemWidth(80.f);
    ImGui::DragFloat("Length scale", &m_fractalLengthScale, 0.01f, 0.35f, 0.9f, "%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Child branch length as a fraction of the parent (≈0.67 is classic).");

    ImGui::SetNextItemWidth(80.f);
    float trunkPct = m_fractalTrunkRatio * 100.f;
    if (ImGui::DragFloat("Trunk height", &trunkPct, 1.f, 10.f, 95.f, "%.0f%%"))
        m_fractalTrunkRatio = trunkPct / 100.f;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Trunk length as a fraction of the drawable area height.");

    ImGui::SetNextItemWidth(generatorMarginDragWidth());
    ImGui::DragFloat4("Margin L/R/T/B", &m_fractalTreeMargins.left, 0.5f, 0.f, 9999.f, "%.1f");
    if (ImGui::Button("Use document margin")) {
        if (m_zones) {
            auto& reg = ofkitty::runtime().registry();
            const entt::entity target = m_zones->findDrawTargetZone(reg);
            if (target != entt::null && reg.all_of<plotter::machine_zone_component>(target))
                m_fractalTreeMargins = reg.get<plotter::machine_zone_component>(target).margins;
        }
    }

    const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Generate Fractal Tree", ImVec2(btnW, 0)))
        generateFractalTree();
    ImGui::SameLine();
    if (ImGui::Button("Clear##fractalTree", ImVec2(btnW, 0)))
        clearFractalTreeLayer();
    ImGui::PopID();
}

void GcodeGeneratorPanel::drawLSystemGeneratorBody()
{
    ImGui::PushID("LSystemBody");
    ImGui::TextDisabled(
        "L-system grammars from ofxLSystemGrammar (ABOP).\n"
        "F = draw forward, +/- = turn, [ ] = branch. Output is scaled to the draw area.");

    const auto& presets = plotter::lSystemPresets();
    const int customIndex = (int)presets.size();

    std::vector<const char*> presetLabels;
    presetLabels.reserve(presets.size() + 1);
    for (const auto& p : presets)
        presetLabels.push_back(p.name);
    presetLabels.push_back("Custom");

    int preset = std::clamp(m_lsystemPreset, 0, customIndex);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::Combo("Preset", &preset, presetLabels.data(), (int)presetLabels.size())) {
        m_lsystemPreset = preset;
        if (preset < customIndex) {
            applyLSystemPresetToBuffers(preset);
            m_lsystemSteps = presets[preset].defaultSteps;
        }
    }

    ImGui::SetNextItemWidth(80.f);
    ImGui::DragInt("Steps", &m_lsystemSteps, 1, 1, 0);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Grammar rewrite iterations (higher = more detail; very high values can be slow).");

    ImGui::SetNextItemWidth(80.f);
    ImGui::DragFloat("Turn angle", &m_lsystemAngleDeg, 0.5f, 5.f, 90.f, "%.0f°");

    const bool showConstant = (preset == 3 || preset == 4 || preset == customIndex);
    if (showConstant) {
        ImGui::SetNextItemWidth(80.f);
        ImGui::DragFloat("Constant R", &m_lsystemConstantR, 0.01f, 0.5f, 3.f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Used in parametric rules (A(s/R) or A(s*R)).");
    }

    const bool isStochasticTree = (preset == plotter::kLSystemStochasticTreePreset);
    if (isStochasticTree) {
        if (ImGui::Checkbox("Draw flowers", &m_lsystemDrawFlowers)) {
            const std::string rules = plotter::stochasticTreeRulesDisplayText(m_lsystemDrawFlowers);
            std::strncpy(m_lsystemRulesBuf, rules.c_str(), sizeof(m_lsystemRulesBuf) - 1);
            m_lsystemRulesBuf[sizeof(m_lsystemRulesBuf) - 1] = '\0';
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A/B symbols in the grammar become small circles at branch tips.");

        ImGui::SetNextItemWidth(80.f);
        int seed = (int)m_lsystemSeed;
        if (ImGui::DragInt("Seed", &seed, 1, 0, 999999999))
            m_lsystemSeed = (unsigned int)std::max(0, seed);
        ImGui::SameLine();
        if (ImGui::Button("Randomize##lsystemSeed"))
            m_lsystemSeed = (unsigned int)ofGetElapsedTimeMillis();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Same seed reproduces the same stochastic tree.");
    }

    if (preset == customIndex) {
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("Axiom", m_lsystemAxiomBuf, sizeof(m_lsystemAxiomBuf));
        ImGui::InputTextMultiline(
            "Rules", m_lsystemRulesBuf, sizeof(m_lsystemRulesBuf),
            ImVec2(-1.f, ImGui::GetTextLineHeight() * 4.f));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("One rule per line. Examples:\nF -> F[+F]F[-F][F]\n0.33 -> F -> F[+F]F");
    } else if (ImGui::TreeNode("Grammar")) {
        ImGui::TextDisabled("Axiom: %s", m_lsystemAxiomBuf);
        ImGui::TextWrapped("%s", m_lsystemRulesBuf);
        ImGui::TreePop();
    }

    ImGui::SetNextItemWidth(generatorMarginDragWidth());
    ImGui::DragFloat4("Margin L/R/T/B", &m_lsystemMargins.left, 0.5f, 0.f, 9999.f, "%.1f");
    if (ImGui::Button("Use document margin")) {
        if (m_zones) {
            auto& reg = ofkitty::runtime().registry();
            const entt::entity target = m_zones->findDrawTargetZone(reg);
            if (target != entt::null && reg.all_of<plotter::machine_zone_component>(target))
                m_lsystemMargins = reg.get<plotter::machine_zone_component>(target).margins;
        }
    }

    const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const bool lsystemBusy = isBackgroundBusy();
    if (lsystemBusy) ImGui::BeginDisabled();
    if (ImGui::Button("Generate L-System Plant", ImVec2(btnW, 0)))
        generateLSystemPlant();
    ImGui::SameLine();
    if (ImGui::Button("Clear##lsystem", ImVec2(btnW, 0)))
        clearLSystemPlantLayer();
    if (lsystemBusy) ImGui::EndDisabled();
    if (lsystemBusy)
        ImGui::TextDisabled("Generating L-system… (Cancel in the status bar)");
    ImGui::PopID();
}

void GcodeGeneratorPanel::drawOfxLSystemGeneratorBody()
{
    ImGui::PushID("OfxLSystemBody");
    ImGui::TextDisabled(
        "3D L-systems via ofxLSystem (lines mode).\n"
        "Supports 3D turtle symbols (+/-, &, ^, \\, /, |). Projected to paper for plotting.");

    const auto& presets = plotter::ofxLSystemPresets();
    const int customIndex = (int)presets.size();

    std::vector<const char*> presetLabels;
    presetLabels.reserve(presets.size() + 1);
    for (const auto& p : presets)
        presetLabels.push_back(p.name);
    presetLabels.push_back("Custom");

    int preset = std::clamp(m_ofxLsystemPreset, 0, customIndex);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::Combo("Preset", &preset, presetLabels.data(), (int)presetLabels.size())) {
        m_ofxLsystemPreset = preset;
        if (preset < customIndex) {
            applyOfxLSystemPresetToBuffers(preset);
            m_ofxLsystemDepth = presets[preset].defaultDepth;
        }
    }

    ImGui::SetNextItemWidth(80.f);
    ImGui::DragInt("Depth", &m_ofxLsystemDepth, 1, 1, 12);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Grammar rewrite depth (ofxLSystem step).\n"
            "Depth 3+ on Bush can take a long time; use Cancel in the status bar.");

    ImGui::SetNextItemWidth(80.f);
    ImGui::DragFloat("Theta", &m_ofxLsystemTheta, 0.5f, 5.f, 180.f, "%.1f°");

    ImGui::SetNextItemWidth(80.f);
    ImGui::DragFloat("Step length", &m_ofxLsystemStepLength, 0.5f, 1.f, 200.f, "%.1f");

    ImGui::Checkbox("Random Y rotation", &m_ofxLsystemRandomYRotation);

    const char* kProjectionLabels[] = { "Side view (X-Y)", "Top view (X-Z)" };
    int projection = std::clamp(m_ofxLsystemProjection, 0, 1);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::Combo("Projection", &projection, kProjectionLabels, 2))
        m_ofxLsystemProjection = projection;

    if (preset == customIndex || preset == 4) {
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("Constants", m_ofxLsystemConstantsBuf, sizeof(m_ofxLsystemConstantsBuf));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Semicolon-separated, e.g. R=1.456");
    }

    if (preset == customIndex) {
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("Axiom", m_ofxLsystemAxiomBuf, sizeof(m_ofxLsystemAxiomBuf));
        ImGui::InputTextMultiline(
            "Rules", m_ofxLsystemRulesBuf, sizeof(m_ofxLsystemRulesBuf),
            ImVec2(-1.f, ImGui::GetTextLineHeight() * 4.f));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("One rule per line, or semicolon-separated.");
    } else if (ImGui::TreeNode("Grammar")) {
        ImGui::TextDisabled("Axiom: %s", m_ofxLsystemAxiomBuf);
        ImGui::TextWrapped("%s", m_ofxLsystemRulesBuf);
        if (m_ofxLsystemConstantsBuf[0] != '\0')
            ImGui::TextDisabled("Constants: %s", m_ofxLsystemConstantsBuf);
        ImGui::TreePop();
    }

    ImGui::SetNextItemWidth(generatorMarginDragWidth());
    ImGui::DragFloat4("Margin L/R/T/B", &m_ofxLsystemMargins.left, 0.5f, 0.f, 9999.f, "%.1f");
    if (ImGui::Button("Use document margin")) {
        if (m_zones) {
            auto& reg = ofkitty::runtime().registry();
            const entt::entity target = m_zones->findDrawTargetZone(reg);
            if (target != entt::null && reg.all_of<plotter::machine_zone_component>(target))
                m_ofxLsystemMargins = reg.get<plotter::machine_zone_component>(target).margins;
        }
    }

    const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const bool busy = isBackgroundBusy();
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Generate L-System 3D", ImVec2(btnW, 0)))
        generateOfxLSystem();
    ImGui::SameLine();
    if (ImGui::Button("Clear##ofxlsystem", ImVec2(btnW, 0)))
        clearOfxLSystemLayer();
    if (busy) ImGui::EndDisabled();
    if (busy)
        ImGui::TextDisabled("Generating L-system 3D… (Cancel in the status bar)");
    ImGui::PopID();
}

void GcodeGeneratorPanel::drawBorderGeneratorBody()
{
    ImGui::PushID("BorderBody");
    ImGui::TextDisabled(
        "Closed border inside the margin box.\n"
        "Positive inset shrinks inward; negative expands toward the zone edge.");

    int style = std::clamp(m_borderStyle, 0, 2);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::Combo("Style", &style, kBorderStyleLabels, IM_ARRAYSIZE(kBorderStyleLabels));
    m_borderStyle = style;

    ImGui::SetNextItemWidth(80.f);
    ImGui::DragFloat("Inset from margin", &m_borderInset, 0.5f, -100.f, 100.f, "%.1f mm");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Extra offset applied after margins.\n"
                          "Positive = inset from margin box; negative = expand outward.");

    if (style == (int)BorderRectStyle::Rounded || style == (int)BorderRectStyle::Loops) {
        ImGui::SetNextItemWidth(80.f);
        ImGui::DragFloat("Corner radius", &m_borderCornerRadius, 0.5f, 0.f, 200.f, "%.1f mm");
    }

    if (style == (int)BorderRectStyle::Loops) {
        ImGui::SetNextItemWidth(80.f);
        ImGui::DragInt("Loop count", &m_borderLoopCount, 1, 1, 32);
        ImGui::SetNextItemWidth(80.f);
        ImGui::DragFloat("Loop spacing", &m_borderLoopSpacing, 0.5f, 0.5f, 50.f, "%.1f mm");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Distance between each concentric border.");
    }

    ImGui::SetNextItemWidth(generatorMarginDragWidth());
    ImGui::DragFloat4("Margin L/R/T/B", &m_borderMargins.left, 0.5f, 0.f, 9999.f, "%.1f");
    if (ImGui::Button("Use document margin")) {
        if (m_zones) {
            auto& reg = ofkitty::runtime().registry();
            const entt::entity target = m_zones->findDrawTargetZone(reg);
            if (target != entt::null && reg.all_of<plotter::machine_zone_component>(target))
                m_borderMargins = reg.get<plotter::machine_zone_component>(target).margins;
        }
    }

    const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Generate Border", ImVec2(btnW, 0)))
        generateBorder();
    ImGui::SameLine();
    if (ImGui::Button("Clear##border", ImVec2(btnW, 0)))
        clearBorderLayer();
    ImGui::PopID();
}

void GcodeGeneratorPanel::drawCropmarksGeneratorBody()
{
    ImGui::PushID("CropmarksBody");
    ImGui::TextDisabled(
        "L-corner ticks on the draw-target margin box for canvas alignment.\n"
        "Exports as its own layer — run Cropmarks G-code first, then content.");

    ImGui::SetNextItemWidth(80.f);
    ImGui::DragFloat("Mark length", &m_cropmarksLength, 0.5f, 1.f, 100.f, "%.1f mm");
    ImGui::SetNextItemWidth(80.f);
    ImGui::DragFloat("Inset from margin", &m_cropmarksInset, 0.5f, 0.f, 100.f, "%.1f mm");

    ImGui::SetNextItemWidth(generatorMarginDragWidth());
    ImGui::DragFloat4("Margin L/R/T/B", &m_cropmarksMargins.left, 0.5f, 0.f, 9999.f, "%.1f");
    if (ImGui::Button("Use document margin")) {
        if (m_zones) {
            auto& reg = ofkitty::runtime().registry();
            const entt::entity target = m_zones->findDrawTargetZone(reg);
            if (target != entt::null && reg.all_of<plotter::machine_zone_component>(target))
                m_cropmarksMargins = reg.get<plotter::machine_zone_component>(target).margins;
        }
    }

    const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Generate Cropmarks", ImVec2(btnW, 0)))
        generateCropmarks();
    ImGui::SameLine();
    if (ImGui::Button("Clear##cropmarks", ImVec2(btnW, 0)))
        clearCropmarksLayer();
    ImGui::PopID();
}

void GcodeGeneratorPanel::drawGenericGeneratorBody(const plotgen::PlotGeneratorId& id)
{
    ImGui::PushID(id.key().c_str());

    const plotgen::IPlotGenerator* gen = plotgen::GeneratorRegistry::instance().get(id);
    if (!gen) {
        ImGui::TextDisabled("Generator '%s' is not registered.", id.key().c_str());
        ImGui::PopID();
        return;
    }

    GeneratorChainEntry* entry = findGeneratorEntry(id);
    if (!entry) {
        ImGui::TextDisabled("Generator is not in the chain.");
        ImGui::PopID();
        return;
    }
    if (entry->settings.is_null() || !entry->settings.is_object())
        entry->settings = gen->defaultSettings();

    ImGui::TextDisabled("%s  (%s)", gen->displayName(), id.key().c_str());
    ImGui::TextWrapped("Settings from the registered generator. Add a specialized UI later if needed.");

    // Simple ofJson object editor — covers Lines / Pen Pump and any future
    // path generator that ships flat number/bool/string defaults.
    if (entry->settings.is_object()) {
        for (auto it = entry->settings.begin(); it != entry->settings.end(); ++it) {
            const std::string key = it.key();
            ofJson& val = it.value();
            ImGui::PushID(key.c_str());
            ImGui::SetNextItemWidth(120.f);
            if (val.is_boolean()) {
                bool b = val.get<bool>();
                if (ImGui::Checkbox(key.c_str(), &b)) val = b;
            } else if (val.is_number_integer()) {
                int n = val.get<int>();
                if (ImGui::DragInt(key.c_str(), &n, 1)) val = n;
            } else if (val.is_number_float() || val.is_number()) {
                float f = val.get<float>();
                if (ImGui::DragFloat(key.c_str(), &f, 0.1f, 0.f, 0.f, "%.3f")) val = f;
            } else if (val.is_string()) {
                char buf[256];
                const std::string s = val.get<std::string>();
                std::snprintf(buf, sizeof(buf), "%s", s.c_str());
                if (ImGui::InputText(key.c_str(), buf, sizeof(buf)))
                    val = std::string(buf);
            } else {
                ImGui::TextDisabled("%s: (unsupported JSON type)", key.c_str());
            }
            ImGui::PopID();
        }
    }

    if (ImGui::Button("Reset to defaults"))
        entry->settings = gen->defaultSettings();

    const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Generate", ImVec2(btnW, 0)))
        generateGeneric(id);
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(btnW, 0)))
        clearGenericLayer(id);

    ImGui::PopID();
}

void GcodeGeneratorPanel::generateGeneric(const plotgen::PlotGeneratorId& id)
{
    if (!m_engine) return;
    const plotgen::IPlotGenerator* gen = plotgen::GeneratorRegistry::instance().get(id);
    GeneratorChainEntry* entry = findGeneratorEntry(id);
    if (!gen || !entry) return;

    if (entry->settings.is_null() || !entry->settings.is_object())
        entry->settings = gen->defaultSettings();

    plotter::ZoneMarginsMM margins;
    if (entry->settings.contains("margins") && entry->settings["margins"].is_object()) {
        const auto& m = entry->settings["margins"];
        margins.left   = m.value("left", 0.f);
        margins.right  = m.value("right", 0.f);
        margins.top    = m.value("top", 0.f);
        margins.bottom = m.value("bottom", 0.f);
    }

    DrawTargetInnerRect area;
    if (!queryDrawTargetInnerRect(m_zones, margins, area)) {
        // Fall back to zero margins so gens without a margin field still run.
        if (!queryDrawTargetInnerRect(m_zones, {}, area)) return;
    }

    const entt::entity layerEnt = prepareGeneratorLayer(entry->layerEnt, gen->displayName());
    auto& reg = m_engine->getRegistry();
    auto& pc  = reg.get_or_emplace<plotter::paths_component>(layerEnt);

    plotgen::GeneratorPathContext ctx;
    ctx.paths = &pc;
    ctx.x0 = area.x0;
    ctx.y0 = area.y0;
    ctx.x1 = area.x1;
    ctx.y1 = area.y1;

    plotgen::GeneratorRegistry::instance().generatePaths(id, ctx, entry->settings);
    notifyGeneratorPathsChanged();
}

void GcodeGeneratorPanel::notifyGeneratorPathsChanged()
{
    if (m_engine && m_engine->activeLayer != entt::null) {
        auto& reg = m_engine->getRegistry();
        plotter::tagEntityForKind(reg, m_engine->activeLayer, plotter::PlottableKind::Generated);
        m_engine->ensureLayerPlotDocRef(m_engine->activeLayer);
    }
    if (m_gen.markCanvasDirty)
        m_gen.markCanvasDirty((unsigned)plotter::CanvasCacheMask::FlatPaths
                           | (unsigned)plotter::CanvasCacheMask::ToolpathPreview);
    if (m_gen.onSourceChanged) m_gen.onSourceChanged();
}

entt::entity GcodeGeneratorPanel::prepareGeneratorLayer(entt::entity& cachedLayer,
                                                        const std::string& layerName)
{
    return plotgen::prepareGeneratorLayer(*m_engine, cachedLayer, layerName);
}

void GcodeGeneratorPanel::clearGeneratorLayerImpl(entt::entity& cachedLayer,
                                                    const std::string& layerName)
{
    if (!m_engine) return;
    plotgen::clearGeneratorLayer(*m_engine, cachedLayer, layerName);
    notifyGeneratorPathsChanged();
}

void GcodeGeneratorPanel::clearGridLayer()
{
    clearGeneratorLayerImpl(generatorLayerEnt(kGridId), generatorLayerName(kGridId));
}

void GcodeGeneratorPanel::clearFractalTreeLayer()
{
    clearGeneratorLayerImpl(generatorLayerEnt(kFractalTreeId), generatorLayerName(kFractalTreeId));
}

void GcodeGeneratorPanel::clearLSystemPlantLayer()
{
    clearGeneratorLayerImpl(generatorLayerEnt(kLSystemPlantId), generatorLayerName(kLSystemPlantId));
}

void GcodeGeneratorPanel::clearOfxLSystemLayer()
{
    clearGeneratorLayerImpl(generatorLayerEnt(kLSystem3dId), generatorLayerName(kLSystem3dId));
}

void GcodeGeneratorPanel::clearBorderLayer()
{
    clearGeneratorLayerImpl(generatorLayerEnt(kBorderId), generatorLayerName(kBorderId));
}

void GcodeGeneratorPanel::clearCropmarksLayer()
{
    clearGeneratorLayerImpl(generatorLayerEnt(kCropmarksId), generatorLayerName(kCropmarksId));
}

void GcodeGeneratorPanel::clearGenericLayer(const plotgen::PlotGeneratorId& id)
{
    clearGeneratorLayerImpl(generatorLayerEnt(id), generatorLayerName(id));
}

void GcodeGeneratorPanel::pinLayerFirst(entt::entity layerEnt)
{
    if (!m_engine || layerEnt == entt::null) return;
    auto& order = m_engine->layerOrder;
    auto it = std::find(order.begin(), order.end(), layerEnt);
    if (it == order.end() || it == order.begin()) return;
    order.erase(it);
    order.insert(order.begin(), layerEnt);
    auto& reg = m_engine->getRegistry();
    for (int i = 0; i < (int)order.size(); ++i) {
        if (reg.valid(order[i]) && reg.all_of<ecs::layer_component>(order[i]))
            reg.get<ecs::layer_component>(order[i]).index = i;
    }
}

bool GcodeGeneratorPanel::isBackgroundBusy() const
{
    return m_lsystemPlantJob.isRunning() || m_lsystem3dJob.isRunning();
}

plotgen::GeneratorProgressHooks GcodeGeneratorPanel::generatorProgressHooks() const
{
    plotgen::GeneratorProgressHooks hooks;
    hooks.onBegin = [](const std::string& title, int totalSteps) {
        ofkitty::progress().setCancelable(true);
        ofkitty::progress().begin(title, totalSteps);
    };
    hooks.onTick = [](const std::string& message) {
        ofkitty::progress().tick(message);
    };
    hooks.onFinish = [](const std::string& message) {
        ofkitty::progress().finish(message);
    };
    hooks.onHide = []() {
        ofkitty::progress().hide();
    };
    hooks.onSetCancelable = [](bool cancelable) {
        ofkitty::progress().setCancelable(cancelable);
    };
    hooks.cancelRequested = []() {
        return ofkitty::progress().cancelRequested();
    };
    return hooks;
}

void GcodeGeneratorPanel::shutdownBackgroundJobs()
{
    ofkitty::progress().requestCancel();
    m_lsystemPlantJob.shutdown();
    m_lsystem3dJob.shutdown();
}

void GcodeGeneratorPanel::updateBackgroundJobs()
{
    if (!m_engine) return;

    const auto progress = generatorProgressHooks();
    if (m_lsystemPlantJob.tryFinish(*m_engine, generatorLayerEnt(kLSystemPlantId),
                                    generatorLayerName(kLSystemPlantId), progress,
                                    "L-System ready",
                                    "L-system plant produced no drawable segments.")) {
        notifyGeneratorPathsChanged();
    }
    if (m_lsystem3dJob.tryFinish(*m_engine, generatorLayerEnt(kLSystem3dId),
                                 generatorLayerName(kLSystem3dId), progress,
                                 "L-System 3D ready",
                                 "ofxLSystem produced no drawable segments.")) {
        notifyGeneratorPathsChanged();
    }
}

void GcodeGeneratorPanel::generateBorder()
{
    if (!m_engine) return;

    DrawTargetInnerRect area;
    if (!queryDrawTargetInnerRect(m_zones, m_borderMargins, area)) return;

    const entt::entity layerEnt = prepareGeneratorLayer(generatorLayerEnt(kBorderId), generatorLayerName(kBorderId));
    auto& reg = m_engine->getRegistry();
    auto& pc  = reg.get_or_emplace<plotter::paths_component>(layerEnt);

    plotgen::BorderGeneratorSettings gs;
    gs.style        = static_cast<plotgen::BorderGeneratorStyle>(m_borderStyle);
    gs.inset        = m_borderInset;
    gs.cornerRadius = m_borderCornerRadius;
    gs.loopCount    = m_borderLoopCount;
    gs.loopSpacing  = m_borderLoopSpacing;
    gs.margins.left   = m_borderMargins.left;
    gs.margins.right  = m_borderMargins.right;
    gs.margins.top    = m_borderMargins.top;
    gs.margins.bottom = m_borderMargins.bottom;

    plotgen::GeneratorPathContext ctx;
    ctx.paths = &pc;
    ctx.x0 = area.x0;
    ctx.y0 = area.y0;
    ctx.x1 = area.x1;
    ctx.y1 = area.y1;

    plotgen::GeneratorRegistry::instance().generatePaths({"path", "border"}, ctx, gs.toJson());
    if (pc.paths.empty())
        ofLogWarning("GcodeGeneratorPanel") << "Border generator produced no drawable paths.";
    notifyGeneratorPathsChanged();
}

void GcodeGeneratorPanel::generateCropmarks()
{
    if (!m_engine) return;

    DrawTargetInnerRect area;
    if (!queryDrawTargetInnerRect(m_zones, m_cropmarksMargins, area)) return;

    const entt::entity layerEnt = prepareGeneratorLayer(generatorLayerEnt(kCropmarksId), generatorLayerName(kCropmarksId));
    auto& reg = m_engine->getRegistry();
    auto& pc  = reg.get_or_emplace<plotter::paths_component>(layerEnt);

    plotgen::GeneratorPathContext ctx;
    ctx.paths = &pc;
    ctx.x0 = area.x0;
    ctx.y0 = area.y0;
    ctx.x1 = area.x1;
    ctx.y1 = area.y1;

    const ofJson settings = {
        {"length", m_cropmarksLength},
        {"inset", m_cropmarksInset},
    };
    plotgen::GeneratorRegistry::instance().generatePaths({"path", "cropmarks"}, ctx, settings);
    if (pc.paths.empty())
        ofLogWarning("GcodeGeneratorPanel") << "Cropmarks generator produced no drawable paths.";
    pinLayerFirst(layerEnt);
    notifyGeneratorPathsChanged();
}

void GcodeGeneratorPanel::generateLSystemPlant()
{
    if (!m_engine) return;
    if (isBackgroundBusy()) return;

    DrawTargetInnerRect area;
    if (!queryDrawTargetInnerRect(m_zones, m_lsystemMargins, area)) return;

    m_lsystemPlantJob.shutdown();
    const ofJson settings = lSystemPlantSettingsJson(
        m_lsystemPreset, m_lsystemSteps, m_lsystemAngleDeg, m_lsystemConstantR,
        m_lsystemDrawFlowers, m_lsystemSeed, m_lsystemAxiomBuf, m_lsystemRulesBuf);

    m_lsystemPlantJob = plotgen::startPathGenerator(
        {"path", "lsystem_plant"},
        settings,
        area.x0, area.y0, area.x1, area.y1,
        "L-System plant",
        std::max(1, m_lsystemSteps) + 1,
        generatorProgressHooks());
}

void GcodeGeneratorPanel::generateOfxLSystem()
{
    if (!m_engine) return;
    if (isBackgroundBusy()) return;

    DrawTargetInnerRect area;
    if (!queryDrawTargetInnerRect(m_zones, m_ofxLsystemMargins, area)) return;

    const int customIndex = (int)plotter::ofxLSystemPresets().size();
    const int preset = std::clamp(m_ofxLsystemPreset, 0, customIndex);
    if (preset == customIndex
        && (m_ofxLsystemAxiomBuf[0] == '\0' || m_ofxLsystemRulesBuf[0] == '\0')) {
        ofLogWarning("GcodeGeneratorPanel") << "ofxLSystem: axiom or rules are empty.";
        return;
    }

    m_lsystem3dJob.shutdown();
    const ofJson settings = ofxLSystemPanelSettingsJson(
        m_ofxLsystemPreset,
        m_ofxLsystemDepth,
        m_ofxLsystemTheta,
        m_ofxLsystemStepLength,
        m_ofxLsystemRandomYRotation,
        m_ofxLsystemProjection,
        m_ofxLsystemAxiomBuf,
        m_ofxLsystemRulesBuf,
        m_ofxLsystemConstantsBuf);

    m_lsystem3dJob = plotgen::startPathGenerator(
        {"path", "lsystem_3d"},
        settings,
        area.x0, area.y0, area.x1, area.y1,
        "L-System 3D",
        std::max(1, m_ofxLsystemDepth) + 1,
        generatorProgressHooks());
}

void GcodeGeneratorPanel::generateGrid()
{
    if (!m_engine) return;

    DrawTargetInnerRect area;
    if (!queryDrawTargetInnerRect(m_zones, m_gridMargins, area)) return;

    const entt::entity layerEnt = prepareGeneratorLayer(generatorLayerEnt(kGridId), generatorLayerName(kGridId));
    auto& reg = m_engine->getRegistry();
    auto& pc  = reg.get_or_emplace<plotter::paths_component>(layerEnt);

    plotgen::GridGeneratorSettings gs;
    gs.divX     = m_gridDivX;
    gs.divY     = m_gridDivY;
    gs.margins.left   = m_gridMargins.left;
    gs.margins.right  = m_gridMargins.right;
    gs.margins.top    = m_gridMargins.top;
    gs.margins.bottom = m_gridMargins.bottom;
    gs.square   = m_gridSquare;
    gs.style    = static_cast<plotgen::GridGeneratorStyle>(m_gridStyle);
    gs.markSize = m_gridMarkSize;

    plotgen::GeneratorPathContext ctx;
    ctx.paths = &pc;
    ctx.x0 = area.x0;
    ctx.y0 = area.y0;
    ctx.x1 = area.x1;
    ctx.y1 = area.y1;

    plotgen::GeneratorRegistry::instance().generatePaths({"path", "grid"}, ctx, gs.toJson());
    notifyGeneratorPathsChanged();
}

void GcodeGeneratorPanel::generateFractalTree()
{
    if (!m_engine) return;

    DrawTargetInnerRect area;
    if (!queryDrawTargetInnerRect(m_zones, m_fractalTreeMargins, area)) return;

    const entt::entity layerEnt = prepareGeneratorLayer(generatorLayerEnt(kFractalTreeId), generatorLayerName(kFractalTreeId));
    auto& reg = m_engine->getRegistry();
    auto& pc  = reg.get_or_emplace<plotter::paths_component>(layerEnt);

    plotgen::FractalTreeGeneratorSettings gs;
    gs.depth          = m_fractalTreeDepth;
    gs.branchAngleDeg = m_fractalBranchAngleDeg;
    gs.lengthScale    = m_fractalLengthScale;
    gs.trunkRatio     = m_fractalTrunkRatio;
    gs.margins.left   = m_fractalTreeMargins.left;
    gs.margins.right  = m_fractalTreeMargins.right;
    gs.margins.top    = m_fractalTreeMargins.top;
    gs.margins.bottom = m_fractalTreeMargins.bottom;

    plotgen::GeneratorPathContext ctx;
    ctx.paths = &pc;
    ctx.x0 = area.x0;
    ctx.y0 = area.y0;
    ctx.x1 = area.x1;
    ctx.y1 = area.y1;

    plotgen::GeneratorRegistry::instance().generatePaths({"path", "fractal_tree"}, ctx, gs.toJson());
    notifyGeneratorPathsChanged();
}


void GcodeGeneratorPanel::drawPenMachineSection(PlotDoc* eng)
{
    const auto presets = plotter::PenSettingsPresets::listEntries();
    const std::string activeName = plotter::PenSettingsPresets::matchingPresetName(eng->pen);
    const char* comboPreview = activeName.empty() ? "Custom" : activeName.c_str();

    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::BeginCombo("Preset##penmachine", comboPreview)) {
        if (ImGui::Selectable("Custom", activeName.empty())) {
            // Keep current values — no load.
        }
        for (const auto& entry : presets) {
            if (ImGui::Selectable(entry.name.c_str(), activeName == entry.name)) {
                PenSettings loaded = eng->pen;
                if (plotter::PenSettingsPresets::loadByName(entry.name, loaded)) {
                    eng->pen = loaded;
                    if (m_gen.markCanvasDirty)
                        m_gen.markCanvasDirty((unsigned)plotter::CanvasCacheMask::FlatPaths
                            | (unsigned)plotter::CanvasCacheMask::ToolpathPreview);
                    if (m_onRegenerate) m_onRegenerate();
                }
            }
        }
        ImGui::EndCombo();
    }

    const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Save preset…", ImVec2(btnW, 0)))
        ImGui::OpenPopup("Save pen preset");
    ImGui::SameLine();
    const bool canDelete = !activeName.empty()
        && std::none_of(presets.begin(), presets.end(), [&](const plotter::PenSettingsPresets::Entry& e) {
            return e.builtIn && e.name == activeName;
        });
    if (!canDelete) ImGui::BeginDisabled();
    if (ImGui::Button("Delete preset", ImVec2(btnW, 0))) {
        if (!activeName.empty())
            plotter::PenSettingsPresets::remove(activeName);
    }
    if (!canDelete) ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("Save pen preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", m_penPresetSaveBuf, sizeof(m_penPresetSaveBuf));
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            const std::string name = m_penPresetSaveBuf;
            if (!name.empty() && plotter::PenSettingsPresets::save(name, eng->pen))
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::DragFloat("Pen down Z", &eng->pen.penDownZ, 0.1f, -20.f, 20.f, "%.1f mm");
    ImGui::DragFloat("Pen up Z", &eng->pen.penUpZ, 0.1f, 0.f, 40.f, "%.1f mm");
    ImGui::DragFloat("Draw speed", &eng->pen.drawSpeed, 10.f, 100.f, 10000.f, "%.0f mm/min");
    ImGui::DragFloat("Travel speed", &eng->pen.travelSpeed, 10.f, 100.f, 10000.f, "%.0f mm/min");
    ImGui::Checkbox("Slow travels", &eng->pen.slowTravels);
    if (ImGui::Checkbox("Visible layers only", &eng->pen.visibleLayersOnly)) {
        if (m_gen.markCanvasDirty)
            m_gen.markCanvasDirty((unsigned)plotter::CanvasCacheMask::FlatPaths
                | (unsigned)plotter::CanvasCacheMask::ToolpathPreview);
        if (m_onRegenerate) m_onRegenerate();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Layers panel eye icon — affects export and draft paths in View.");
}

void GcodeGeneratorPanel::drawToolpathsTab()
{
    PlotDoc* eng = m_gen.engine ? m_gen.engine : m_engine;
    if (!eng) {
        ImGui::TextDisabled("No engine attached.");
        return;
    }

    ImGui::TextWrapped(
        "Run plot finders on the source → writes toolpaths into PlotDoc layers. "
        "Then use the G-code tab to export.");

    if (ImGui::CollapsingHeader("Pen / Machine", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawPenMachineSection(eng);
    }

    const bool generating = m_gen.generating && m_gen.generating->load();

    if (generating) {
        const float p = m_gen.progress ? m_gen.progress->load() : 0.f;
        const char* msg = (m_gen.progressMsg && !m_gen.progressMsg->empty())
            ? m_gen.progressMsg->c_str() : "Working...";
        ImGui::ProgressBar(p, ImVec2(-1.f, 0.f), msg);
    } else {
        bool canGenerate = eng->hasImage() || eng->hasFillLayers();
        bool hasSvgPaths = !eng->getPaths().empty() && !canGenerate;
        if (!canGenerate && !hasSvgPaths) ImGui::BeginDisabled();
        if (ImGui::Button("Generate toolpaths##run", ImVec2(-1.f, 0.f))) {
            if (hasSvgPaths && m_onRegenerate) m_onRegenerate();
            else if (m_gen.startGenerate) m_gen.startGenerate();
        }
        if (!canGenerate && !hasSvgPaths) ImGui::EndDisabled();
        if (!canGenerate && !hasSvgPaths)
            ImGui::TextDisabled("Load a source on the Source tab first.");
    }

    if (!generating && !eng->getPaths().empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Paths: %d   Points: %d", eng->totalPaths, eng->totalPoints);
        ImGui::TextDisabled("Distance: %.0f mm   Est. time: %.0f s",
            eng->totalDistance, eng->estimatedTime);
    }
}


void GcodeGeneratorPanel::drawGcodeTab()
{
    PlotDoc* eng = m_gen.engine ? m_gen.engine : m_engine;
    if (!eng) {
        ImGui::TextDisabled("No engine attached.");
        return;
    }

    ImGui::TextWrapped(
        "Export PlotDoc toolpaths through the post pipeline → G-code in the Code Editor.");

    {
        const bool hasSource = eng->hasImage() || !eng->getPaths().empty();
        const bool busy      = (m_gen.generating && m_gen.generating->load())
            || (m_gen.isExportBusy && m_gen.isExportBusy());
        if (!hasSource || busy) ImGui::BeginDisabled();
        if (m_gen.reloadSource && ImGui::Button("Reload source"))
            m_gen.reloadSource();
        if (!hasSource || busy) ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Reload the active source file from disk and regenerate toolpaths + G-code.");
        ImGui::Spacing();
    }

    if (eng->getPaths().empty()) {
        ImGui::TextDisabled("Generate toolpaths on the Toolpaths tab first.");
        ImGui::Spacing();
    }

    const bool connected = m_gen.senderConnected ? m_gen.senderConnected() : false;
    if (!connected) ImGui::BeginDisabled();
    if (m_gen.sendToPlotter && ImGui::Button("Send to plotter", ImVec2(-1.f, 0.f)))
        m_gen.sendToPlotter();
    if (!connected) ImGui::EndDisabled();

    if (m_gen.sender) {
        const float refFeed = m_gen.referenceFeedMmMin
            ? m_gen.referenceFeedMmMin()
            : (eng ? eng->pen.drawSpeed : 3000.f);
        plotter::kit::drawPlotFeedRateControl(m_gen.sender, refFeed, "send");
    }

    ImGui::Separator();

    // ---- Preamble -----------------------------------------------------------
    // A helper that draws a snippet selector + "Open in Editor" for one section.
    auto drawSnippetSection = [&](const char* headerLabel,
                                  entt::entity& entity,
                                  std::string& resourceName,
                                  bool defaultOpen) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (!defaultOpen) flags = 0;
        if (!ImGui::CollapsingHeader(headerLabel, flags)) return;

        const auto snippets = m_getSnippetResources
            ? m_getSnippetResources()
            : std::vector<std::pair<std::string, entt::entity>>{};

        // Build combo labels: "None", then snippet names, then "New…"
        int combo = 0;
        for (int si = 0; si < (int)snippets.size(); ++si)
            if (snippets[si].second == entity
             || (!resourceName.empty() && snippets[si].first == resourceName))
                combo = si + 1;

        std::vector<const char*> labels;
        labels.push_back("None");
        for (const auto& s : snippets) labels.push_back(s.first.c_str());
        labels.push_back("New\xe2\x80\xa6");

        const int newIdx = (int)labels.size() - 1;
        ImGui::SetNextItemWidth(-1.f);
        ImGui::PushID(headerLabel);
        if (ImGui::Combo("##snippetCombo", &combo, labels.data(), (int)labels.size())) {
            if (combo == 0) {
                entity = entt::null;
                resourceName.clear();
            } else if (combo == newIdx) {
                combo = 0;
                if (m_onCreateSnippet) {
                    const std::string name = std::string(headerLabel) + ".gcode";
                    entt::entity newEnt = m_onCreateSnippet(name);
                    entity = newEnt;
                    resourceName = name;
                    combo = 1;
                }
            } else {
                entity       = snippets[combo - 1].second;
                resourceName = snippets[combo - 1].first;
            }
        }
        ImGui::PopID();
    };

    drawSnippetSection("Preamble", m_preambleEntity, m_preambleResourceName, true);

    // ---- Postamble ----------------------------------------------------------
    drawSnippetSection("Postamble", m_postambleEntity, m_postambleResourceName, true);

    ImGui::Separator();
    ImGui::Checkbox("Run pipeline on export", &m_runPipelineOnExport);
    ImGui::Checkbox("Write back to layers", &m_writeBackToPaths);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("After merge/sort, add a new \"(optimized)\" layer per source layer\n"
                          "with the processed paths. Original layers are left unchanged.\n"
                          "Turn off to optimize G-code only without changing the document.");
    ImGui::TextDisabled("Pipeline steps and injection rules live in View \xe2\x86\x92 Pipeline / Injections.");

    ImGui::Separator();
    const bool exportBusy = m_gen.isExportBusy && m_gen.isExportBusy();
    if (exportBusy) ImGui::BeginDisabled();
    if (m_onRegenerate && ImGui::Button("Export G-code", ImVec2(-1, 0)))
        m_onRegenerate();
    if (exportBusy) ImGui::EndDisabled();

    if (m_onExportLayersPerFile) {
        if (exportBusy) ImGui::BeginDisabled();
        if (ImGui::Button("Export per layer\xe2\x80\xa6", ImVec2(-1, 0)))
            m_onExportLayersPerFile();
        if (exportBusy) ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Generate G-code for each visible layer and load them into\n"
                "the Code Editor sidebar.\n"
                "Use 'Save As\xe2\x80\xa6' from the sidebar to write files later.");
    }

    if (m_onExportLayersToFolder) {
        if (exportBusy) ImGui::BeginDisabled();
        if (ImGui::Button("Export per layer to folder\xe2\x80\xa6", ImVec2(-1, 0)))
            m_onExportLayersToFolder();
        if (exportBusy) ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Generate G-code for each visible layer and write one\n"
                "numbered .gcode file per layer into a folder you choose.\n"
                "The pipeline runs independently per layer.");
    }
}


void GcodeGeneratorPanel::draw(const char* title, bool& visible)
{
    ImGui::SetNextWindowSize(ImVec2(360, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, &visible)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("GcodeGenTabs")) {
        if (ImGui::BeginTabItem("Source")) {
            drawSourceTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Toolpaths")) {
            drawToolpathsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("G-code")) {
            drawGcodeTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}



} // namespace plotter::kit
