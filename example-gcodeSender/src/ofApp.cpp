#include "ofApp.h"
#include "GcodeSenderUiIds.h"

#include "PlotterGcodeSync.h"
#include "PlotterExporter.h"
#include "PlotterPreviewGrids.h"
#include "SmoothPenMotion.h"
#include "imgui.h"
#include "imgui_internal.h" // GetTopMostPopupModal (modal save/open must keep editor keys)
#include "ImFonts.h"   // ofxImGuiStyle — load the monospace editor font directly

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <vector>

namespace {

/// Bump when the shipped dock layout changes — reseeds imgui.ini from
/// imgui.default.ini once so fresh checkouts and upgrades share the same layout.
constexpr int kGcodeSenderImGuiLayoutVersion = 2;

bool ensureDefaultImGuiLayout()
{
    // Paths relative to bin/data (ofFile relativeToData=true).
    constexpr const char* kIni = "gcodeSender/imgui.ini";
    constexpr const char* kDefault = "gcodeSender/imgui.default.ini";
    constexpr const char* kMarker = "gcodeSender/.gcode_sender_imgui_layout_version";

    int saved = 0;
    if (ofFile::doesFileExist(kMarker)) {
        std::ifstream in(ofToDataPath(kMarker, true));
        in >> saved;
    }

    const bool needSeed = saved < kGcodeSenderImGuiLayoutVersion
                       || !ofFile::doesFileExist(kIni);
    if (needSeed && ofFile::doesFileExist(kDefault)) {
        ofFile::copyFromTo(kDefault, kIni, true, true);
        ofLogNotice("ofApp") << "Seeded G-code Sender dock layout from imgui.default.ini";
    }

    if (saved < kGcodeSenderImGuiLayoutVersion) {
        std::ofstream out(ofToDataPath(kMarker, true));
        out << kGcodeSenderImGuiLayoutVersion << '\n';
    }
    return needSeed;
}



std::string filenameFrom(const std::string& path)
{
    return std::filesystem::path(path).filename().string();
}

std::string formatGcodeValue(float value)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", value);
    std::string out(buf);
    const auto dotPos = out.find('.');
    if (dotPos != std::string::npos) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (out.empty() || out == "-0") out = "0";
    return out;
}

/// Negate every Z word in @p input (machine "Invert +Z" convention). Works for
/// both G90 and G91 since negation is linear; comments are left untouched.
std::string flipZWordsInGcode(const std::string& input)
{
    std::ostringstream out;
    std::istringstream in(input);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t commentPos = line.find(';');
        std::string code = (commentPos == std::string::npos) ? line : line.substr(0, commentPos);
        const std::string comment = (commentPos == std::string::npos) ? "" : line.substr(commentPos);

        std::istringstream ls(code);
        std::vector<std::string> tokens;
        std::string tok;
        bool changedLine = false;
        while (ls >> tok) {
            if (tok.size() >= 2
                && std::toupper((unsigned char)tok[0]) == 'Z') {
                try {
                    const float z = std::stof(tok.substr(1));
                    tok = "Z" + formatGcodeValue(-z);
                    changedLine = true;
                } catch (...) {
                }
            }
            tokens.push_back(tok);
        }

        if (changedLine) {
            std::ostringstream lineOut;
            for (std::size_t ti = 0; ti < tokens.size(); ++ti) {
                if (ti) lineOut << ' ';
                lineOut << tokens[ti];
            }
            std::string rebuilt = lineOut.str();
            if (!rebuilt.empty() && !comment.empty()) rebuilt += " ";
            rebuilt += comment;
            out << rebuilt;
        } else {
            out << line;
        }
        if (!in.eof()) out << "\n";
    }
    return out.str();
}

/// Label-left / widget-right row (same idea as ofxEnTTInspector ComponentInspector).
bool dragFloatLabeled(float labelColW, const char* label, float* v,
                      float speed, float vMin, float vMax, const char* fmt)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelColW);
    ImGui::SetNextItemWidth(std::max(48.f, ImGui::GetContentRegionAvail().x));
    ImGui::PushID(label);
    const bool changed = ImGui::DragFloat("##v", v, speed, vMin, vMax, fmt);
    ImGui::PopID();
    return changed;
}

bool comboLabeled(float labelColW, const char* label, int* current,
                  const char* const items[], int itemCount)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelColW);
    ImGui::SetNextItemWidth(std::max(48.f, ImGui::GetContentRegionAvail().x));
    ImGui::PushID(label);
    const bool changed = ImGui::Combo("##v", current, items, itemCount);
    ImGui::PopID();
    return changed;
}

float labelColumnWidth(std::initializer_list<const char*> labels)
{
    float w = 0.f;
    for (const char* label : labels)
        w = std::max(w, ImGui::CalcTextSize(label).x);
    return w + ImGui::GetStyle().ItemInnerSpacing.x * 2.f;
}

/// Fixed widget width in character units (keeps labels readable in narrow panels).
float controlFieldWidth(int chars = 15)
{
    return ImGui::CalcTextSize("0").x * static_cast<float>(chars)
         + ImGui::GetStyle().FramePadding.x * 2.f;
}

/// Side-profile preview of the parabolic pen landing: horizontal run (approach
/// length) vs Z (hover height), so the "kiss" shape is visible while dialing the
/// two values live. Left = descend from reservoir height to hover; the parabola
/// glides to touchdown (tangent to the paper), then the stroke continues flat.
/// @p overlapMm shifts the touchdown INTO the stroke: the green line starts
/// before the yellow touchdown dot, with the curve descending over it.
/// Side-profile of the real landing motion (matches SmoothPenMotion):
///   smooth ON  → travel @ pen-up → drop to hover → power-curve kiss to paper
///   smooth OFF → travel @ pen-up → square plunge to paper (Approach ≈ 0)
/// Angles come from landingAngleInfo (curve derivative), not the chord atan(h/L).
void drawApproachLandingProfile(const PenSettings& pen)
{
    const plotter::LandingAngleInfo ang = plotter::landingAngleInfo(pen);
    const float L = ang.L;
    const float h = ang.h;
    const float p = ang.p;
    const float o = ang.smooth
        ? std::clamp(pen.leadOverlapMm, 0.f, L) : 0.f;
    const float penUpH = std::max(ang.penUpSpan, h); // vertical domain (mm)

    const float availW = std::max(120.f, ImGui::GetContentRegionAvail().x);
    const ImVec2 size(availW, 140.f);
    const ImVec2 org = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##approachProfile", size);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 br(org.x + size.x, org.y + size.y);
    dl->AddRectFilled(org, br, IM_COL32(18, 18, 28, 255), 4.f);
    dl->AddRect(org, br, IM_COL32(70, 70, 90, 255), 4.f);
    dl->PushClipRect(org, br, true);

    const float pad   = 14.f;
    const float xL    = org.x + pad;
    const float xR    = org.x + size.x - pad;
    const float yBase = org.y + size.y - pad;        // paper (Z = penDown)
    const float yTop  = org.y + pad + 16.f;          // top of drawable (pen-up)
    // True Z scale: pen-up and hover share one mm→px mapping so the kiss is
    // not stretched to fill the panel when hover << pen-up.
    const float zPxPerMm = (penUpH > 1e-4f) ? ((yBase - yTop) / penUpH) : 0.f;
    const float ySafe  = yBase - penUpH * zPxPerMm;
    const float yHover = yBase - h * zPxPerMm;

    // Touchdown at 60%; lead-in left, stroke right.
    const float xTouch = org.x + size.x * 0.6f;
    // When there's no Approach run, keep a readable horizontal span for labels.
    const float leadPxPerMm = (L > 1e-4f) ? ((xTouch - xL) / L)
                                          : ((xTouch - xL) / 1.f);

    // Paper baseline + pen-up guide.
    dl->AddLine(ImVec2(xL, yBase), ImVec2(xR, yBase), IM_COL32(120, 120, 140, 255), 1.5f);
    if (penUpH > 1e-4f)
        dl->AddLine(ImVec2(xL, ySafe), ImVec2(xR, ySafe), IM_COL32(55, 55, 75, 255), 1.f);

    char buf[128];
    if (ang.smooth) {
        const float xStart = xTouch - o * leadPxPerMm;
        const float xEntry = xTouch - L * leadPxPerMm;

        // Stroke on paper from stroke-start onward.
        dl->AddLine(ImVec2(xStart, yBase), ImVec2(xR, yBase), IM_COL32(90, 235, 150, 255), 2.5f);

        // Travel at pen-up into the overshoot entry, then drop to hover.
        dl->AddLine(ImVec2(xL, ySafe), ImVec2(xEntry, ySafe), IM_COL32(90, 100, 160, 255), 1.5f);
        if (std::abs(ySafe - yHover) > 0.5f)
            dl->AddLine(ImVec2(xEntry, ySafe), ImVec2(xEntry, yHover),
                        IM_COL32(140, 120, 200, 255), 1.5f);

        if (h > 1e-4f) {
            dl->AddLine(ImVec2(xL, yHover), ImVec2(xEntry, yHover), IM_COL32(70, 70, 95, 255), 1.f);

            // Power-curve kiss: entry (xrem=L, z=h) → touchdown (xrem=0, z=0).
            ImVec2 prev(0, 0);
            const int N = 40;
            for (int i = 0; i <= N; ++i) {
                const float tt   = static_cast<float>(i) / static_cast<float>(N);
                const float xrem = L * (1.f - tt);
                const float z    = h * std::pow(xrem / L, p);
                const ImVec2 cur(xTouch - xrem * leadPxPerMm, yBase - z * zPxPerMm);
                if (i > 0) dl->AddLine(prev, cur, IM_COL32(90, 180, 255, 255), 2.f);
                prev = cur;
            }
        } else {
            // Hover 0: after the pen-up drop you're already on paper — flat run.
            dl->AddLine(ImVec2(xEntry, yBase), ImVec2(xTouch, yBase),
                        IM_COL32(90, 180, 255, 255), 2.f);
        }

        if (o > 0.01f) {
            dl->AddLine(ImVec2(xStart, yBase - 6.f), ImVec2(xStart, yBase + 4.f),
                        IM_COL32(230, 230, 240, 255), 1.5f);
        }

        // Entry / touchdown ticks for the angle read-out.
        if (h > 1e-4f) {
            const float tick = 10.f;
            // Entry slope mark (atan(p·h/L)).
            const float entryRad = ang.entryDeg * (3.14159265f / 180.f);
            dl->AddLine(ImVec2(xEntry, yHover),
                        ImVec2(xEntry + tick * std::cos(entryRad),
                               yHover + tick * std::sin(entryRad)),
                        IM_COL32(255, 180, 90, 200), 1.5f);
        }

        if (o > 0.01f)
            std::snprintf(buf, sizeof(buf),
                          "entry %.0f\xc2\xb0  touch %.0f\xc2\xb0   hover %.1f / run %.1f  pow %.2f  overlap %.1f",
                          ang.entryDeg, ang.touchDeg, h, L, p, o);
        else
            std::snprintf(buf, sizeof(buf),
                          "entry %.0f\xc2\xb0  touch %.0f\xc2\xb0   hover %.1f / run %.1f mm   pow %.2f",
                          ang.entryDeg, ang.touchDeg, h, L, p);
    } else {
        // Square plunge from pen-up (hover unused) — Approach 0 or smoothing off.
        dl->AddLine(ImVec2(xTouch, yBase), ImVec2(xR, yBase), IM_COL32(90, 235, 150, 255), 2.5f);
        dl->AddLine(ImVec2(xL, ySafe), ImVec2(xTouch, ySafe), IM_COL32(90, 100, 160, 255), 1.5f);
        if (penUpH > 1e-4f)
            dl->AddLine(ImVec2(xTouch, ySafe), ImVec2(xTouch, yBase),
                        IM_COL32(90, 180, 255, 255), 2.f);
        std::snprintf(buf, sizeof(buf),
                      "square plunge 90\xc2\xb0   pen-up %.1f mm   (Approach 0 — no lead-in)",
                      penUpH);
    }

    dl->AddCircleFilled(ImVec2(xTouch, yBase), 3.5f, IM_COL32(255, 215, 90, 255));
    dl->AddText(ImVec2(xL, org.y + 3.f), IM_COL32(205, 205, 215, 255), buf);
    dl->PopClipRect();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::setup()
{
    ofSetWindowTitle("G-code Sender");
    ofBackground(14, 14, 22);
    ofSetFrameRate(60);
    ofDisableArbTex();

    // Before Runtime loads ImGui settings: seed dock layout for fresh checkouts
    // (and once after a layout-version bump).
    ensureDefaultImGuiLayout();

    // Plotter preview app — no 3D Scene View / full-window pixel rulers / gizmo chrome.
    ofkitty::runtime().enableSceneEditorFeatures(false);

    // Runs in Runtime::onSetup after ImGui/fonts exist (ofApp::setup runs first).
    ofkitty::runtime().addPostSetupHook([this](ofxImGui::Gui& gui) {
        codeEditor.SetPalette(TextEditor::PaletteId::Dark);
        codeEditor.SetShowLineNumbersEnabled(true);
        codeEditor.SetShowWhitespacesEnabled(false);
        codeEditor.SetLineSpacing(1.0f);
        if (ImFont* f = ImFonts::LoadCodeEditorFont(ImGui::GetIO().Fonts, 14.0f)) {
            codeEditor.SetFont(f);
            gui.rebuildFontsTexture();
        }
        lastUndoIndex = codeEditor.GetUndoIndex();
    });

    codeEditor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::Gcode);

    machinePrefs.load();
    paperOriginX = machinePrefs.bed.paperOriginX;
    paperOriginY = machinePrefs.bed.paperOriginY;
    plotter::kit::addBuiltinPaperPresets(paperPresets);
    paperPresets.loadUserFromDataPath("settings/PaperPresets.json");
    plotter::kit::addBuiltinEnvelopePresets(envelopePresets);
    envelopePresets.loadUserFromDataPath("settings/EnvelopePresets.json");
    plotter::kit::addBuiltinPipelinePresets(pipelinePresets);
    pipelinePresets.loadUserFromDataPath("settings/PipelinePresets.json");
    plotter::kit::addBuiltinPenPresets(penPresets);
    penPresets.loadUserFromDataPath("settings/PenPresets.json");

    m_snippetCatalog.ensureDefaults();

    m_zones.load(m_registry);
    m_plotDoc.setDrawTargetSource(&m_zones);
    ensureDefaultDrawTargetZone();
    plotter::applyDrawTargetToPrefs(m_registry, m_zones, machinePrefs);
    syncDrawTarget(false);

    m_zonesPanel.setRegistry(&m_registry);
    m_zonesPanel.setZoneStore(&m_zones);
    m_zonesPanel.setPrefs(&machinePrefs);
    // false: do not reimport source G-code on every zone tweak (breaks live ImGui edits).
    m_zonesPanel.setOnDrawTargetChanged([this] { syncDrawTarget(false); });

    m_panel.setRegistry(&m_registry);

    {
        const std::string settingsPath = ofToDataPath("settings/plot_pipeline_default.json", true);
        const std::string legacyPath   = ofToDataPath("plot_pipeline_default.json", true);
        if (ofFile::doesFileExist(settingsPath))
            m_pipeline = plotproc::PlotPipeline::loadPreset(settingsPath);
        else if (ofFile::doesFileExist(legacyPath))
            m_pipeline = plotproc::PlotPipeline::loadPreset(legacyPath);
        else
            m_pipeline = plotproc::PlotPipeline::defaults();
    }
    m_pipelinePanel.setPipeline(&m_pipeline);
    m_pipelinePanel.setOnChanged([this] {
        markPrepareDirty("Pipeline changed — press Update/Generate");
    });
    m_pipelinePanel.setDrawPresetFooter([this] {
        if (pipelinePresets.drawPicker(
                "pipeline",
                [this] { return plotter::kit::pipelinePresetJson(m_pipeline); },
                [this](const ofJson& j) {
                    plotter::kit::applyPipelinePreset(m_pipeline, j);
                },
                plotter::kit::pipelinePresetEquals)) {
            markPrepareDirty("Pipeline preset applied — press Update/Generate");
        }
    });

    m_injectionsPanel.setRegistry(&m_registry);
    m_injectionsPanel.setZoneStore(&m_zones);
    m_injectionsPanel.setSnippetCatalog(&m_snippetCatalog);
    m_injectionsPanel.setOnChanged([this] {
        markPrepareDirty("Injections changed — press Update/Generate");
    });

    m_plotDoc.pen.penDownZ    = 0.f;
    m_plotDoc.pen.penUpZ      = 5.f;
    m_plotDoc.pen.drawSpeed   = 800.f;
    m_plotDoc.pen.travelSpeed = 3000.f;
    m_plotDoc.pen.penWidth    = 0.3f;
    m_panel.penStrokeWidthMm  = m_plotDoc.pen.penWidth;
    m_panel.overrideColors    = true;
    m_panel.showTravelPaths   = true;
    m_panel.travelColor       = { 0.65f, 0.35f, 0.75f, 0.65f }; // pen-up / G0

    m_layersPanel.setup(&m_registry, &m_plotDoc.activeLayer);
    m_layersPanel.setOnAddLayer([this] {
        m_plotDoc.addLayer();
        markPrepareDirty();
    });
    m_layersPanel.setOnRemoveLayer([this](entt::entity e) {
        m_plotDoc.removeLayer(e);
        markPrepareDirty();
    });
    m_layersPanel.setOnClearAll([this] {
        m_plotDoc.resetCanvas();
        markPrepareDirty();
        if (m_plotDoc.activeLayer != entt::null)
            ofkitty::runtime().select(m_plotDoc.activeLayer);
    });
    m_layersPanel.setOnReparent([this](entt::entity child,
                                       entt::entity newParent,
                                       entt::entity insertBefore) {
        m_plotDoc.reparentLayer(child, newParent, insertBefore);
        markPrepareDirty();
    });
    m_layersPanel.setOnLayerChanged([this] {
        markPrepareDirty();
        if (m_plotDoc.activeLayer != entt::null)
            ofkitty::runtime().select(m_plotDoc.activeLayer);
    });
    m_layersPanel.setGetBadge([this](entt::entity e) -> std::string {
        if (!m_registry.valid(e)) return "";
        if (m_registry.all_of<plotter::toolpath_stats_component>(e)) {
            const auto& sc = m_registry.get<plotter::toolpath_stats_component>(e);
            return std::to_string(sc.totalPaths) + " paths";
        }
        if (m_registry.all_of<plotter::paths_component>(e)) {
            const auto& pc = m_registry.get<plotter::paths_component>(e);
            if (!pc.paths.empty())
                return std::to_string(pc.paths.size()) + " paths";
        }
        return "";
    });

    m_exportSession.bind(&m_plotDoc, &m_zones, &m_pipeline, &machinePrefs, &m_registry);
    m_exportSession.setSourceProvider([this] { return pipelineSourceText(); });
    m_exportSession.setExportSettingsHasher([this] { return hashExportSettings(); });
    m_exportSession.setPreambleProvider([] { return std::string{}; });
    syncExportSessionFlags();
    m_exportSession.setOnPrepared([this](const std::string& gcode) {
        prepareDirty = false;
        pushPreparedGcodeToEditor();
        syncToolpathPreview(gcode); // full job incl. injections + travels
        m_landingPads = m_exportSession.landings(); // exact touchdown/lift-off geometry
        statusMessage = "Prepared G-code ready.";
    });
    m_exportSession.setReportSink([this](const plotproc::PipelineRunReport& report, bool ok) {
        m_pipelinePanel.setLastReport(report, ok);
    });

    wireDocumentKit();

    m_view = ofkitty::runtime().setMainView2D(buildPreviewBounds().size(), "mm");
    m_view->showRulers = true;
    m_view->guides     = &m_guides; // rulers alone do not create guides — pointer is required
    m_view->overlayDraw = [this](ofkitty::Runtime::MainView2D& mv) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (!dl) return;
        const auto& v = mv.view2D;
        const glm::vec2 off = mv.imguiScreenOffset;
        plotter::drawZoneGrids(dl, m_registry, buildPreviewBounds(),
                               v.ox + off.x, v.oy + off.y, v.zoom_,
                               v.canvasOrigin.x + off.x, v.canvasOrigin.y + off.y,
                               v.canvasW, v.canvasH,
                               false, mv.grid);
    };
    ofkitty::runtime().setViewMenuExtra([this] { drawPreviewViewMenuItems(); });

    {
        plotter::kit::Toolpath3DInputs in;
        in.plotDoc = &m_plotDoc;
        in.zones = &m_zones;
        in.registry = &m_registry;
        in.machinePrefs = &machinePrefs;
        in.previewPanel = &m_panel;
        in.landingPads = &m_landingPads;
        in.buildPreviewBounds = [this] { return buildPreviewBounds(); };
        in.toolpathPreviewRev = &m_toolpathPreviewRev;
        in.showLeadBounds = &m_showLeadBounds;
        in.landingColor = &m_landingColor;
        in.leadBoundsColor = &m_leadBoundsColor;
        m_toolpath3D.setInputs(std::move(in));
    }
    m_toolpath3D.setup();

    sender.setSimulationMode(false);
    serialWindow.setSender(&sender);
    serialWindow.setPrefs(&machinePrefs);
    serialWindow.refreshDeviceList();
    serialWindow.syncSelectionFromPrefs();
    m_panel.addCompanionAction(ICON_FA_PRINT " Send", [this] { sendPreparedGcodeToPrinter(); });

    ofkitty::runtime().registerPreferencePage({
        "G-code Sender", "Print Preview", "gcode_sender.prefs.preview",
        [this] { drawPrintPreviewPreferencePage(); }
    });

    ofkitty::runtime().registerPrefSerializer(
        "gcode_sender.preview",
        [this](ofJson& j) {
            auto& p = j["gcodeSenderPreview"];
            p["overrideColors"]  = m_panel.overrideColors;
            p["showMark"]        = m_showMark;
            p["showToolpath"]    = m_showToolpath;
            p["showLandingPads"] = m_showLandingPads;
            p["showLeadBounds"]  = m_showLeadBounds;
            p["showTravelPaths"] = m_panel.showTravelPaths;
            p["leadBoundsColor"] = { m_leadBoundsColor.r, m_leadBoundsColor.g,
                                     m_leadBoundsColor.b, m_leadBoundsColor.a };
            p["toolpath3DZExagg"] = m_toolpath3D.zExaggeration();
            p["tp3dShowRibbon"]  = m_toolpath3D.showRibbon();
            p["tp3dShowMarks"]   = m_toolpath3D.showMarks();
            p["tp3dShowTravels"] = m_toolpath3D.showTravels();
            p["tp3dShowLeads"]   = m_toolpath3D.showLeads();
            p["tp3dShowMarkers"] = m_toolpath3D.showMarkers();
            p["tp3dShowZones"]   = m_toolpath3D.showZones();
            // Legacy key kept in sync so older builds / docs still round-trip.
            p["scaleStrokeToPenWidth"] = m_showMark;
            p["toolpathColor"] = { m_toolpathColor.r, m_toolpathColor.g,
                                   m_toolpathColor.b, m_toolpathColor.a };
            p["landingColor"]    = { m_landingColor.r, m_landingColor.g,
                                     m_landingColor.b, m_landingColor.a };
            p["drawColor"]       = { m_panel.drawColor.r, m_panel.drawColor.g,
                                     m_panel.drawColor.b, m_panel.drawColor.a };
            p["travelColor"]     = { m_panel.travelColor.r, m_panel.travelColor.g,
                                     m_panel.travelColor.b, m_panel.travelColor.a };
            p["envelopeColor"]   = { m_panel.envelopeColor.r, m_panel.envelopeColor.g,
                                     m_panel.envelopeColor.b, m_panel.envelopeColor.a };
            p["paperColor"]      = { m_panel.paperColor.r, m_panel.paperColor.g,
                                     m_panel.paperColor.b, m_panel.paperColor.a };
        },
        [this](const ofJson& j) {
            if (!j.contains("gcodeSenderPreview")) return;
            const auto& p = j["gcodeSenderPreview"];
            if (p.contains("overrideColors"))  m_panel.overrideColors  = p["overrideColors"];
            if (p.contains("showLandingPads")) m_showLandingPads      = p["showLandingPads"];
            if (p.contains("showLeadBounds"))  m_showLeadBounds       = p["showLeadBounds"];
            if (p.contains("showTravelPaths")) m_panel.showTravelPaths = p["showTravelPaths"];
            if (p.contains("toolpath3DZExagg"))
                m_toolpath3D.setZExaggeration(p["toolpath3DZExagg"].get<float>());
            if (p.contains("tp3dShowRibbon"))  m_toolpath3D.setShowRibbon(p["tp3dShowRibbon"]);
            if (p.contains("tp3dShowMarks"))   m_toolpath3D.setShowMarks(p["tp3dShowMarks"]);
            if (p.contains("tp3dShowTravels")) m_toolpath3D.setShowTravels(p["tp3dShowTravels"]);
            if (p.contains("tp3dShowLeads"))   m_toolpath3D.setShowLeads(p["tp3dShowLeads"]);
            if (p.contains("tp3dShowMarkers")) m_toolpath3D.setShowMarkers(p["tp3dShowMarkers"]);
            if (p.contains("tp3dShowZones"))   m_toolpath3D.setShowZones(p["tp3dShowZones"]);
            // New keys; fall back to legacy scaleStroke / showCenterline pairing.
            if (p.contains("showMark")) {
                m_showMark = p["showMark"].get<bool>();
            } else if (p.contains("scaleStrokeToPenWidth")) {
                m_showMark = p["scaleStrokeToPenWidth"].get<bool>();
            }
            if (p.contains("showToolpath")) {
                // Legacy: showToolpath was a master gate; with Mark on, the thin
                // overlay lived under showCenterline. Prefer the new meaning.
                m_showToolpath = p["showToolpath"].get<bool>();
                if (p.contains("showCenterline") && m_showMark)
                    m_showToolpath = p["showCenterline"].get<bool>();
            }
            m_panel.scaleStrokeToPenWidth = m_showMark;
            auto loadColor4 = [&](const char* key, ofFloatColor& c) {
                if (!p.contains(key) || !p[key].is_array() || p[key].size() < 4) return;
                c.r = p[key][0]; c.g = p[key][1]; c.b = p[key][2]; c.a = p[key][3];
            };
            auto loadColor3 = [&](const char* key, ofFloatColor& c) {
                if (!p.contains(key) || !p[key].is_array() || p[key].size() < 3) return;
                c.r = p[key][0]; c.g = p[key][1]; c.b = p[key][2];
            };
            loadColor4("drawColor", m_panel.drawColor);
            loadColor4("travelColor", m_panel.travelColor);
            loadColor3("envelopeColor", m_panel.envelopeColor);
            loadColor3("paperColor", m_panel.paperColor);
            if (p.contains("toolpathColor"))
                loadColor4("toolpathColor", m_toolpathColor);
            else
                loadColor4("centerlineColor", m_toolpathColor); // legacy
            loadColor4("landingColor", m_landingColor);
            loadColor4("leadBoundsColor", m_leadBoundsColor);
        });

    ofkitty::runtime().setFileDropHandler([this](const std::vector<std::filesystem::path>& files,
                                                 glm::vec2) {
        if (files.empty()) return;
        const std::string path = files.front().string();
        if (ofIsStringInString(ofToLower(path), ".ofdoc"))
            m_docKit.openDocument(path);
        else
            importGcodeFile(path);
    });

    ofkitty::progress().registerWithRuntime();

    setupUi();
    ofkitty::runtime().setEditMode(true);
    ofkitty::runtime().enableBuiltInWindow("Properties");
}

void ofApp::update()
{
    if (settingsDirty && !sourceGcodePath.empty()) {
        importGcodeFile(sourceGcodePath);
        settingsDirty = false;
    }

    // Drain Generate outside the ImGui Controls click (avoids nested progress/ImGui).
    if (prepareRequested && !m_exportSession.isExporting()) {
        prepareRequested = false;
        syncExportSessionFlags();
        m_exportSession.invalidate();
        m_exportSession.requestAsync();
        statusMessage = "Preparing G-code…";
    }

    m_exportSession.poll();
    sender.update();
    m_toolpath3D.update();

    const int undoIdx = codeEditor.GetUndoIndex();
    if (undoIdx != lastUndoIndex) {
        lastUndoIndex = undoIdx;
        if (gcodeEditorView == GcodeEditorView::Source) {
            codeEditor.SetText(liveSourceGcode);
            lastUndoIndex = codeEditor.GetUndoIndex();
        } else {
            editorDirty = true;
        }
    }
}

void ofApp::draw()
{
    if (!m_view) return;
    syncMainViewContentSize();
    auto& v = m_view->view2D;
    v.updateDerived();
    ofPushMatrix();
    ofTranslate(v.ox, v.oy);
    ofScale(v.zoom_, v.zoom_);
    // Keep the panel/doc field in sync with View → Show Mark (presets / .ofdoc).
    m_panel.scaleStrokeToPenWidth = m_showMark;

    plotter::kit::GcodeSenderSceneOpts opts;
    opts.showPaths             = m_showMark || m_showToolpath;
    opts.bedColor              = m_panel.envelopeColor;
    opts.paperColor            = ofColor(m_panel.paperColor);
    opts.overrideColors        = m_panel.overrideColors;
    opts.pathColor             = m_panel.drawColor;
    opts.showMark              = m_showMark;
    opts.penStrokeWidthMm      = m_panel.penStrokeWidthMm;
    opts.contentZoomPxPerMm    = v.zoom_;
    opts.showToolpath          = m_showToolpath;
    opts.toolpathColor         = m_toolpathColor;
    opts.centerlineMeshCache   = &m_centerlineMeshCache;
    opts.showTravelPaths       = m_panel.showTravelPaths;
    opts.travelColor           = m_panel.travelColor;
    if (m_panel.hasGeometry()) {
        opts.contentDrawPaths   = &m_panel.drawPaths();
        opts.contentDrawColors  = &m_panel.drawColors();
        opts.contentTravelPaths = &m_panel.travelPaths();
    }
    opts.drawMeshCache   = &m_drawMeshCache;
    opts.travelMeshCache = &m_travelMeshCache;
    if (m_showLeadBounds) {
        const auto bed = plotter::BedView::fromPrefs(machinePrefs);
        opts.showLeadBounds = true;
        opts.leadBounds = plotter::computeLeadBoundsFromPaperPaths(
            m_plotDoc.getPaths(), bed, m_plotDoc.pen);
        opts.leadBoundsColor = m_leadBoundsColor;
    }
    plotter::kit::drawGcodeSenderScene(m_plotDoc, m_zones, m_registry, machinePrefs, opts);

    if (m_showLandingPads && m_plotDoc.pen.smoothApproach)
        drawLandingPads(v.zoom_);

    ofPopMatrix();
}

void ofApp::drawLandingPads(float zoom)
{
    if (zoom <= 1e-4f) return;
    if (m_landingPads.landings.empty() && m_landingPads.liftoffs.empty()) return;

    // Touchdown / lift-off *points* come from the last prepare (machine mm).
    // The lead-in / lead-out *shape* is rebuilt every frame from the live pen
    // settings so Approach / Retract / Arc radius / style update immediately
    // (to scale in content mm) without waiting for Update/Generate.
    //
    // The polyline is the XYZ preview geometry projected to XY, coloured by Z:
    // airborne alignment arcs (pen up) are cool and faint, the parabolic kiss
    // near pen-down is warm and opaque — so hooks read as "in the air", not as
    // painted mark. Orbit the Toolpath 3D panel to see the same paths in 3D.
    const plotter::PreviewBounds pb = buildPreviewBounds();
    const float rPx = 5.f / zoom; // ~5 px touchdown ring regardless of zoom
    const PenSettings& pen = m_plotDoc.pen;

    const ofFloatColor warm(m_landingColor);              // touchdown / kiss
    ofFloatColor cool(0.35f, 0.6f, 1.f, m_landingColor.a); // pen-up travel height
    const float zLo = pen.penDownZ;
    const float zHi = pen.penUpZ;
    const float zSpan = zHi - zLo; // signed — normalisation works either way

    ofPushStyle();
    ofSetLineWidth(1.5f);

    // Colour for one XYZ sample: warm→cool by height, alpha fades when airborne.
    auto zColor = [&](float z, float alphaScale) {
        float t = (std::abs(zSpan) > 1e-4f) ? (z - zLo) / zSpan : 0.f;
        t = std::clamp(t, 0.f, 1.f);
        ofFloatColor c = warm.getLerped(cool, t);
        c.a = warm.a * alphaScale * (1.f - 0.72f * t);
        return c;
    };

    // Draw an XYZ lead path (machine mm) as a Z-coloured content-space polyline.
    // ringAtFront: lift-off (stroke end first); else landing (touchdown last).
    auto drawPath = [&](const std::vector<glm::vec3>& path, bool ringAtFront,
                        float ringR, float alphaScale) {
        if (path.empty()) return;
        if (path.size() >= 2) {
            ofMesh mesh;
            mesh.setMode(OF_PRIMITIVE_LINE_STRIP);
            for (const auto& mp : path) {
                const glm::vec2 c = pb.machineToContent(mp.x, mp.y);
                mesh.addVertex({c.x, c.y, 0.f});
                mesh.addColor(zColor(mp.z, alphaScale));
            }
            mesh.draw();
        }
        const glm::vec3& ringM = ringAtFront ? path.front() : path.back();
        const glm::vec3& farM  = ringAtFront ? path.back()  : path.front();
        const glm::vec2 ring = pb.machineToContent(ringM.x, ringM.y);
        const glm::vec2 farC = pb.machineToContent(farM.x, farM.y);
        ofSetColor(zColor(ringM.z, alphaScale));
        ofNoFill(); ofDrawCircle(ring.x, ring.y, ringR);
        ofSetColor(zColor(farM.z, alphaScale));
        ofFill();   ofDrawCircle(farC.x, farC.y, rPx * 0.4f);
    };

    for (const auto& e : m_landingPads.landings) {
        const ofVec2f from = e.hasArrivalFrom
            ? e.arrivalFrom
            : e.point - e.dir * std::max(0.f, pen.approachMm);
        const auto* path = e.strokePath.empty() ? nullptr : &e.strokePath;
        drawPath(plotter::previewLeadInPath3D(pen, from, e.point, e.dir, path),
                 /*ringAtFront=*/false, rPx, 1.f);
    }

    for (const auto& e : m_landingPads.liftoffs) {
        const auto* path = e.strokePath.empty() ? nullptr : &e.strokePath;
        drawPath(plotter::previewLeadOutPath3D(pen, e.point, e.dir, path),
                 /*ringAtFront=*/true, rPx * 0.75f, 0.7f);
    }

    ofPopStyle();
}

void ofApp::exit()
{
    m_exportSession.cancelJoin();
}

// ── file loading ──────────────────────────────────────────────────────────────

void ofApp::pushPreparedGcodeToEditor()
{
    if (gcodeEditorView != GcodeEditorView::Prepared) return;
    if (!m_exportSession.cacheHit()) return;

    // Cache already includes source XY transform (applied before pipeline/injections).
    const std::string& prepared = m_exportSession.cachedGcode();
    if (prepared.empty()) return;

    codeEditor.SetText(prepared);
    lastUndoIndex = codeEditor.GetUndoIndex();
    editorDirty   = false;
}

void ofApp::syncGcodeEditorToView()
{
    if (gcodeEditorView == GcodeEditorView::Source) {
        codeEditor.SetText(liveSourceValid ? liveSourceGcode : std::string{});
        lastUndoIndex = codeEditor.GetUndoIndex();
        editorDirty   = false;
        return;
    }

    if (m_exportSession.cacheHit())
        pushPreparedGcodeToEditor();
    else if (prepareDirty)
        statusMessage = "Prepared view stale — press Update/Generate";
}

void ofApp::importGcodeFile(const std::string& path)
{
    if (path.empty()) return;
    sourceGcodePath = path;

    const ofBuffer buf = ofBufferFromFile(sourceGcodePath);
    if (buf.size() == 0) {
        statusMessage = "Failed to load: " + filenameFrom(sourceGcodePath);
        return;
    }

    liveSourceGcode = buf.getText();
    liveSourceValid = true;
    m_exportSession.syncPlotDocFromSource(transformedSourceText());
    syncToolpathPreview(transformedSourceText());
    markProjectDirty();
    markPrepareDirty("Imported — press Update/Generate");

    if (m_plotDoc.docEntity() != entt::null
        && m_registry.all_of<plotter::plot_doc_component>(m_plotDoc.docEntity())) {
        m_registry.get<plotter::plot_doc_component>(m_plotDoc.docEntity()).sourcePath = sourceGcodePath;
    }

    gcodeEditorView = GcodeEditorView::Source;
    syncGcodeEditorToView();

    lastEditorLine = 0;
    editorDirty    = false;
    statusMessage  = "Imported: " + filenameFrom(sourceGcodePath)
                   + " — press Update/Generate to prepare";
}

void ofApp::openImportGcodeDialog()
{
    ofkitty::runtime().openFileDialog(
        "gcode_sender.open",
        "Open G-code",
        ".gcode,.nc,.txt",
        [this](const std::string& path) { importGcodeFile(path); });
}

void ofApp::saveEditorToFile(bool showSaveDialog)
{
    auto writePath = [this](const std::string& savePath) {
        if (savePath.empty()) return;
        const std::string text = codeEditor.GetText();
        ofBuffer buf;
        buf.set(text);
        if (ofBufferToFile(savePath, buf)) {
            sourceGcodePath = savePath;
            editorDirty     = false;
            lastUndoIndex   = codeEditor.GetUndoIndex();
            statusMessage   = "Saved: " + filenameFrom(savePath);
        } else {
            statusMessage = "Failed to save G-code.";
        }
    };

    if (!showSaveDialog && !sourceGcodePath.empty()) {
        writePath(sourceGcodePath);
        return;
    }

    const std::string defaultName = sourceGcodePath.empty()
        ? "export.gcode"
        : filenameFrom(sourceGcodePath);
    ofkitty::runtime().saveFileDialog(
        "gcode_sender.save_gcode",
        "Save G-code As",
        "G-code{.gcode,.nc,.cnc,.tap},All Files{.*}",
        defaultName,
        [writePath](const std::string& path) { writePath(path); });
}

void ofApp::parseEditorText()
{
    if (gcodeEditorView == GcodeEditorView::Source) {
        liveSourceGcode = codeEditor.GetText();
        liveSourceValid = !liveSourceGcode.empty();
    }
    m_exportSession.syncPlotDocFromSource(transformedSourceText());
    syncToolpathPreview(transformedSourceText());
    markProjectDirty();
    markPrepareDirty("Source parsed — press Update/Generate");
}

std::string ofApp::pipelineSourceText() const
{
    return transformedSourceText();
}

std::string ofApp::transformedSourceText(bool* sawIncremental) const
{
    std::string raw;
    if (liveSourceValid && !liveSourceGcode.empty())
        raw = liveSourceGcode;
    else if (gcodeEditorView == GcodeEditorView::Source)
        raw = codeEditor.GetText();
    else
        raw = liveSourceGcode;
    return transformAbsoluteXYInGcode(raw, sawIncremental);
}

void ofApp::markPrepareDirty(const std::string& reason)
{
    prepareDirty = true;
    m_exportSession.invalidate();
    if (!reason.empty())
        statusMessage = reason;
}

void ofApp::syncExportSessionFlags()
{
    m_exportSession.setRunPipeline(usePipeline);
    m_exportSession.setApplyInjections(useInjections);
}

void ofApp::requestPrepare()
{
    if (!liveSourceValid || liveSourceGcode.empty()) {
        statusMessage = "No source G-code to prepare.";
        return;
    }
    if (m_exportSession.isExporting() || prepareRequested) {
        statusMessage = "Prepare already in progress…";
        return;
    }
    prepareRequested = true;
    gcodeEditorView = GcodeEditorView::Prepared;
    statusMessage = "Preparing…";
}

std::size_t ofApp::hashExportSettings() const
{
    std::ostringstream oss;
    oss << machinePrefs.bed.paperOriginX << ','
        << machinePrefs.bed.paperOriginY << ','
        << machinePrefs.envelope.minX << ','
        << machinePrefs.envelope.minY << ','
        << machinePrefs.envelope.maxX << ','
        << machinePrefs.envelope.maxY << ','
        << machinePrefs.envelope.minZ << ','
        << machinePrefs.envelope.maxZ << ','
        << machinePrefs.axes.signX << ','
        << machinePrefs.axes.signY << ','
        << machinePrefs.axes.signZ << '\n';
    oss << m_zones.drawTargetZoneId << '\n';
    oss << m_plotDoc.pen.penUpZ << ','
        << m_plotDoc.pen.penDownZ << ','
        << m_plotDoc.pen.drawSpeed << ','
        << m_plotDoc.pen.travelSpeed << ','
        << m_plotDoc.pen.penWidth << ','
        << m_plotDoc.pen.slowTravels << ','
        << m_plotDoc.pen.smoothApproach << ','
        << m_plotDoc.pen.approachMm << ','
        << m_plotDoc.pen.retractMm << ','
        << m_plotDoc.pen.leadOverlapMm << ','
        << m_plotDoc.pen.approachSteps << ','
        << m_plotDoc.pen.feedEasing << ','
        << m_plotDoc.pen.easeInMm << ','
        << m_plotDoc.pen.easeOutMm << ','
        << m_plotDoc.pen.easeMinFeedFrac << ','
        << m_plotDoc.pen.approachHeightMm << ','
        << m_plotDoc.pen.approachCurvePow << '\n';
    oss << m_zones.toJson(const_cast<entt::registry&>(m_registry)).dump();
    return std::hash<std::string>{}(oss.str());
}

std::string ofApp::buildOutputGcode(bool* sawIncremental)
{
    // Transform is applied to source before pipeline/injections (see pipelineSourceText).
    if (sawIncremental) {
        bool sawInc = false;
        (void)transformedSourceText(&sawInc);
        *sawIncremental = sawInc;
    }
    if (prepareDirty || !m_exportSession.cacheHit()) {
        syncExportSessionFlags();
        m_exportSession.invalidate();
    }
    std::string gcode = m_exportSession.getPostPipelineGcodeBlocking();
    // Machine-facing Z convention: the app works pen-up = +Z; flip on the way
    // out for machines with inverted Z (see "Invert +Z" in Machine Envelope).
    if (!gcode.empty() && machinePrefs.axes.signZ < 0.f)
        gcode = "; Z axis inverted for machine (Invert +Z)\n" + flipZWordsInGcode(gcode);
    return gcode;
}

std::string ofApp::transformAbsoluteXYInGcode(const std::string& input,
                                              bool* sawIncremental) const
{
    if (sawIncremental) *sawIncremental = false;
    if (input.empty()) return {};
    if (printOffsetX == 0.f && printOffsetY == 0.f
        && printScaleX == 1.f && printScaleY == 1.f
        && (printRotateQuarterTurns % 4) == 0) {
        return input;
    }

    bool absoluteMode = true;
    float currentX = 0.f;
    float currentY = 0.f;
    bool  hasCurrent = false;

    auto applyPoint = [this](float x, float y) -> glm::vec2 {
        float tx = x * printScaleX;
        float ty = y * printScaleY;
        switch ((printRotateQuarterTurns % 4 + 4) % 4) {
            case 1: { const float nx =  ty; const float ny = -tx; tx = nx; ty = ny; break; } // 90 CW
            case 2: tx = -tx; ty = -ty; break; // 180
            case 3: { const float nx = -ty; const float ny =  tx; tx = nx; ty = ny; break; } // 270 CW
            default: break;
        }
        tx += printOffsetX;
        ty += printOffsetY;
        return { tx, ty };
    };

    std::ostringstream out;
    std::istringstream in(input);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t commentPos = line.find(';');
        std::string code = (commentPos == std::string::npos) ? line : line.substr(0, commentPos);
        const std::string comment = (commentPos == std::string::npos) ? "" : line.substr(commentPos);
        std::istringstream ls(code);
        std::vector<std::string> tokens;
        std::string tok;
        while (ls >> tok) tokens.push_back(tok);

        bool hasX = false, hasY = false;
        float xVal = 0.f, yVal = 0.f;
        int xIndex = -1, yIndex = -1;
        for (int ti = 0; ti < (int)tokens.size(); ++ti) {
            const std::string& t = tokens[ti];
            if (t.size() < 2) continue;
            const char c = (char)std::toupper((unsigned char)t[0]);
            if (c == 'G') {
                try {
                    const int g = std::stoi(t.substr(1));
                    if (g == 90) absoluteMode = true;
                    else if (g == 91) {
                        absoluteMode = false;
                        if (sawIncremental) *sawIncremental = true;
                    }
                } catch (...) {
                }
            } else if (c == 'X') {
                try {
                    xVal = std::stof(t.substr(1));
                    hasX = true;
                    xIndex = ti;
                } catch (...) {
                }
            } else if (c == 'Y') {
                try {
                    yVal = std::stof(t.substr(1));
                    hasY = true;
                    yIndex = ti;
                } catch (...) {
                }
            }
        }

        if (absoluteMode && (hasX || hasY)) {
            const float srcX = hasX ? xVal : (hasCurrent ? currentX : 0.f);
            const float srcY = hasY ? yVal : (hasCurrent ? currentY : 0.f);
            const glm::vec2 dst = applyPoint(srcX, srcY);

            if (hasX && xIndex >= 0) tokens[(size_t)xIndex] = "X" + formatGcodeValue(dst.x);
            if (hasY && yIndex >= 0) tokens[(size_t)yIndex] = "Y" + formatGcodeValue(dst.y);
            if (!hasX) tokens.push_back("X" + formatGcodeValue(dst.x));
            if (!hasY) tokens.push_back("Y" + formatGcodeValue(dst.y));

            currentX = srcX;
            currentY = srcY;
            hasCurrent = true;
        } else if (hasX || hasY) {
            if (!absoluteMode && sawIncremental) *sawIncremental = true;
            if (!hasCurrent) { currentX = 0.f; currentY = 0.f; hasCurrent = true; }
            if (absoluteMode) {
                if (hasX) currentX = xVal;
                if (hasY) currentY = yVal;
            } else {
                if (hasX) currentX += xVal;
                if (hasY) currentY += yVal;
            }
        }

        std::ostringstream lineOut;
        for (std::size_t ti = 0; ti < tokens.size(); ++ti) {
            if (ti) lineOut << ' ';
            lineOut << tokens[ti];
        }
        std::string rebuilt = lineOut.str();
        if (!rebuilt.empty() && !comment.empty()) rebuilt += " ";
        rebuilt += comment;
        out << rebuilt;
        if (!in.eof()) out << "\n";
    }

    return out.str();
}

void ofApp::ensureDefaultDrawTargetZone()
{
    if (m_zones.findDrawTargetZone(m_registry) != entt::null) return;

    plotter::machine_zone_component canvas;
    canvas.zoneId      = "zone_canvas";
    canvas.name        = "Canvas";
    canvas.x           = machinePrefs.bed.paperOriginX;
    canvas.y           = machinePrefs.bed.paperOriginY;
    canvas.w           = std::max(10.f, machinePrefs.envelope.maxX - machinePrefs.envelope.minX);
    canvas.h           = std::max(10.f, machinePrefs.envelope.maxY - machinePrefs.envelope.minY);
    canvas.margins.setUniform(m_plotDoc.marginMMFallback);
    canvas.showGrid    = true;
    canvas.showMargins = true;
    plotter::createZoneEntity(m_registry, std::move(canvas), -1);
    m_zones.drawTargetZoneId = "zone_canvas";
}

void ofApp::syncDrawTarget(bool markSettingsDirty)
{
    plotter::applyDrawTargetToPrefs(m_registry, m_zones, machinePrefs);

    const entt::entity target = m_zones.findDrawTargetZone(m_registry);
    if (target != entt::null && m_registry.all_of<plotter::machine_zone_component>(target)) {
        const auto& z = m_registry.get<plotter::machine_zone_component>(target);
        paperOriginX = z.x;
        paperOriginY = z.y;
        m_panel.paperW = z.w;
        m_panel.paperH = z.h;
        machinePrefs.bed.paperOriginX = paperOriginX;
        machinePrefs.bed.paperOriginY = paperOriginY;
    }

    machinePrefs.save();
    syncMainViewContentSize();
    markPrepareDirty();
    // Full source reimport is rarely needed and fights live DragFloat/InputText edits.
    if (markSettingsDirty)
        settingsDirty = true;
}

// ── coordinate helpers ────────────────────────────────────────────────────────

void ofApp::sendPreparedGcodeToPrinter()
{
    if (!sender.isConnected()) {
        statusMessage = "Printer is not connected.";
        return;
    }
    bool sawIncremental = false;
    const std::string gcode = buildOutputGcode(&sawIncremental);
    if (gcode.empty()) {
        statusMessage = "No G-code to send.";
        return;
    }

    // Preflight: refuse to stream a job whose moves (incl. injected pen up/down
    // and snippets) leave the software envelope — a Z beyond travel trips the
    // machine's drive alarm mid-job.
    if (const auto err = grbl::checkGCodeBlockAgainstEnvelope(
            gcode, machinePrefs.envelope, 0.f, 0.f, 0.f)) {
        statusMessage = "Send blocked: " + *err;
        return;
    }
    prepareDirty = false;

    sender.enqueueGCodeBlock(gcode);
    statusMessage = "Queued G-code for printer.";
    if (sawIncremental) {
        statusMessage += " (G91 lines unchanged)";
    }
}

void ofApp::savePreparedGcode()
{
    bool sawIncremental = false;
    const std::string gcode = buildOutputGcode(&sawIncremental);
    if (gcode.empty()) {
        statusMessage = "No G-code to save.";
        return;
    }

    const std::string defaultName = sourceGcodePath.empty()
        ? "print_output.gcode"
        : filenameFrom(sourceGcodePath);
    ofkitty::runtime().saveFileDialog(
        "gcode_sender.export_prepared",
        "Export prepared G-code",
        "G-code{.gcode,.nc,.cnc,.tap},All Files{.*}",
        defaultName,
        [this, gcode, sawIncremental](const std::string& path) {
            ofBuffer buf;
            buf.set(gcode);
            if (ofBufferToFile(path, buf)) {
                prepareDirty  = false;
                statusMessage = "Exported prepared G-code: " + filenameFrom(path);
                if (sawIncremental)
                    statusMessage += " (G91 lines unchanged)";
            } else {
                statusMessage = "Failed to export prepared G-code.";
            }
        });
}

// ── coordinate helpers ────────────────────────────────────────────────────────

plotter::BedView ofApp::buildBedView() const
{
    grbl::Envelope env;
    env.minX = machinePrefs.envelope.minX;
    env.minY = machinePrefs.envelope.minY;
    env.maxX = machinePrefs.envelope.maxX;
    env.maxY = machinePrefs.envelope.maxY;
    if (env.maxX <= env.minX) env.maxX = env.minX + 420.f;
    if (env.maxY <= env.minY) env.maxY = env.minY + 297.f;
    grbl::BedLayout bed;
    bed.paperOriginX = paperOriginX;
    bed.paperOriginY = paperOriginY;
    grbl::AxisConvention axes = machinePrefs.axes;
    axes.normalize();
    return { env, bed, axes };
}

plotter::PreviewBounds ofApp::buildPreviewBounds() const
{
    return plotter::PreviewBounds::fromEnvelope(buildBedView(), 5.f);
}

void ofApp::syncMainViewContentSize()
{
    if (!m_view) return;
    m_view->view2D.contentSize = buildPreviewBounds().size();
    m_view->guides = &m_guides;
}

void ofApp::syncToolpathPreview(const std::string& gcode)
{
    m_drawMeshCache.invalidate();
    m_travelMeshCache.invalidate();
    m_centerlineMeshCache.invalidate();
    ++m_toolpathPreviewRev; // 3D scene cache keys off this
    if (gcode.empty()) {
        m_panel.clear();
        return;
    }
    plotter::PrintPreviewOptions opts;
    opts.bed    = buildBedView();
    opts.bounds = buildPreviewBounds();
    opts.import.fitBeziers   = false;
    // Contact band: only Z within a whisker of pen-down counts as marking, so
    // the thick Mark preview does not run on under the smooth-lift climb (the
    // old halfway threshold classified most of the airborne parabola as draw).
    {
        const float span = m_plotDoc.pen.penUpZ - m_plotDoc.pen.penDownZ;
        const float band = std::clamp(std::abs(span) * 0.1f, 0.05f, 0.5f);
        opts.import.penDownMaxZ = m_plotDoc.pen.penDownZ + (span >= 0.f ? band : -band);
    }
    const auto paper = m_plotDoc.getPaperSizeMM();
    m_panel.paperW = paper.x;
    m_panel.paperH = paper.y;
    if (!m_panel.loadFromText(gcode, opts))
        ofLogWarning("gcodeSender") << "syncToolpathPreview: failed to parse G-code preview";
}

void ofApp::drawPreviewViewMenuItems()
{
    if (!m_view) return;

    if (ImGui::BeginMenu("Preview")) {
        ImGui::MenuItem("Show Mark", nullptr, &m_showMark);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Thick stroke footprint at pen width (the painted mark).\n"
                              "Colour / width live in Controls \xe2\x86\x92 Pen.");
        ImGui::MenuItem("Show Toolpath", nullptr, &m_showToolpath);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Thin machine path (centerline) + G0 travels.\n"
                              "Independent of Show Mark — both can be on together.");
        ImGui::MenuItem("Show Parabolic Landing Pads", nullptr, &m_showLandingPads);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Mark each stroke's touchdown + lead-in (and lift-off)\n"
                              "so the smooth parabolic approach is visible. Needs\n"
                              "'Smooth sine approach' enabled in Controls → Pen.");
        ImGui::MenuItem("Show Lead bounds", nullptr, &m_showLeadBounds);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bounding box of the artwork plus Approach/Retract\n"
                              "overshoot — align this to the machine bed.");
        ImGui::MenuItem("Show Travel (pen up)", nullptr, &m_panel.showTravelPaths);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("G0 rapid moves in travel colour (Preferences → Print Preview).\n"
                              "Only drawn when Show Toolpath is on.");

        m_toolpath3D.drawViewMenuItems();
        ImGui::EndMenu();
    }

    ImGui::Separator();
    ImGui::MenuItem("Rulers", nullptr, &m_view->showRulers);
    ImGui::MenuItem("Guides", nullptr, &m_guides.visible);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drag from a ruler strip to create a guide.\nRulers alone do not enable guides — ofxKit needs a GuideSet*.");
    if (ImGui::MenuItem("Clear Guides")) {
        m_guides.h.clear();
        m_guides.v.clear();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Fit to window"))
        m_view->view2D.fitToCanvas();
}

// ── editor sync ───────────────────────────────────────────────────────────────

void ofApp::syncEditorToPlayback()
{
    if (!syncCursorToPlayback || !m_panel.hasGeometry()) return;
    const int lineCount = codeEditor.GetLineCount();
    if (lineCount <= 1) return;
    const int line = plotterGcodeSync::playbackToLine(m_panel.playback(), lineCount);
    if (line == lastEditorLine) return;
    lastEditorLine = line;
    codeEditor.SelectLine(line);
    codeEditor.SetViewAtLine(line, TextEditor::SetViewAtLineMode::Centered);
}

// ── UI (ofxKit registered windows) ──────────────────────────────────────────

void ofApp::setupUi()
{
    using namespace gcodeSender;

    m_zonesPanel.setImGuiWindowTitle(kZonesWindow);
    m_pipelinePanel.setImGuiWindowTitle(plotter::kit::kImGuiTitlePipeline);
    m_injectionsPanel.setImGuiWindowTitle("Injections###gcode_sender_injections");
    m_injectionsPanel.setPenZProviders(
        [this] { return m_plotDoc.pen.penUpZ; },
        [this] { return m_plotDoc.pen.penDownZ; });

    serialWindow.setUsbSerialWindowTitle("USB Serial###gcode_sender_usb");
    serialWindow.setConsoleWindowTitle("Serial Console###gcode_sender_console");

    ofkitty::runtime().registerWindow({
        "Controls", "View", true, true,
        [this](bool& v) { drawControlPanel(v); },
        "gcode_sender_controls"
    });
    plotter::kit::registerWindow(ofkitty::runtime(), m_zonesPanel,
                                 { true, true, true, "Zones", "gcode_sender_zones" });
    plotter::kit::registerWindow(ofkitty::runtime(), m_pipelinePanel,
                                 { true, true, true, {}, plotter::kit::kWinIdPipeline });
    plotter::kit::registerWindow(ofkitty::runtime(), m_injectionsPanel,
                                 { true, true, true, "Injections", "gcode_sender_injections" });
    ofkitty::runtime().registerWindow({
        "Layers", "View", true, true,
        [this](bool& v) { m_layersPanel.draw(gcodeSender::kLayersWindow, v); },
        "gcode_sender_layers"
    });
    ofkitty::runtime().registerWindow({
        "G-code", "View", true, true,
        [this](bool& v) { drawCodePanel(v); },
        "gcode_sender_code"
    });
    plotter::kit::registerSerialWindows(
        ofkitty::runtime(), serialWindow,
        { true, false, false, "USB Serial", "gcode_sender_usb" },
        { true, false, false, "Serial Console", "gcode_sender_console" });

    // Fallback when no imgui.ini exists (imgui.default.ini is preferred).
    // Mirrors the shipped layout: Controls left; G-code + Toolpath 3D right;
    // Zones / Injections / Pipeline also available from View.
    ofkitty::runtime().addDefaultLayoutLeftDockTop(kControlsWindow);
    ofkitty::runtime().addDefaultLayoutLeftDock(kZonesWindow);
    ofkitty::runtime().addDefaultLayoutLeftDock("Injections###gcode_sender_injections");
    ofkitty::runtime().addDefaultLayoutLeftDock(plotter::kit::kImGuiTitlePipeline);
    ofkitty::runtime().addDefaultLayoutRightDock(kGcodeWindow);
    ofkitty::runtime().addDefaultLayoutRightDock("Toolpath 3D");
}

void ofApp::drawFileDropzone()
{
    const ImVec2 dropSize(-1.f, 72.f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##file_dropzone", dropSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1 = pos;
    const ImVec2 p2(pos.x + ImGui::GetItemRectSize().x, pos.y + ImGui::GetItemRectSize().y);
    const ImU32 borderCol = hovered
        ? ImGui::GetColorU32(ImGuiCol_ButtonHovered)
        : ImGui::GetColorU32(ImGuiCol_Border);
    dl->AddRect(p1, p2, borderCol, 4.f, 0, hovered ? 2.f : 1.f);

    const char* hint = sourceGcodePath.empty()
        ? "Drop .gcode here or click to open"
        : filenameFrom(sourceGcodePath).c_str();
    const ImVec2 textSize = ImGui::CalcTextSize(hint);
    const ImVec2 textPos(
        pos.x + (p2.x - p1.x - textSize.x) * 0.5f,
        pos.y + (p2.y - p1.y - textSize.y) * 0.5f);
    dl->AddText(textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);

    if (clicked)
        openImportGcodeDialog();

    if (!statusMessage.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", statusMessage.c_str());
    }
    if (!sourceGcodePath.empty()) {
        if (ImGui::SmallButton(ICON_FA_SYNC " Reload from disk"))
            importGcodeFile(sourceGcodePath);
    }
    ImGui::Spacing();
}

void ofApp::drawPrintPreviewPreferencePage()
{
    ImGui::Checkbox("Show travel paths (G0)", &m_panel.showTravelPaths);
    if (m_panel.showTravelPaths) {
        ImGui::SameLine();
        ImGui::ColorEdit4("##travelcol", (float*)&m_panel.travelColor,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel
                          | ImGuiColorEditFlags_AlphaBar);
    }

    ImGui::Spacing();
    ImGui::ColorEdit3("Envelope##col", (float*)&m_panel.envelopeColor,
                      ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(); ImGui::TextUnformatted("Envelope");
    ImGui::ColorEdit3("Paper##col", (float*)&m_panel.paperColor,
                      ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(); ImGui::TextUnformatted("Paper");
}

void ofApp::drawControlPanel(bool& visible)
{
    if (!ImGui::Begin(gcodeSender::kControlsWindow, &visible)) { ImGui::End(); return; }

    constexpr ImGuiTreeNodeFlags sectionFlags =
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed;

    const float fieldW = controlFieldWidth(15);
    bool changed = false;

    drawFileDropzone();

    if (ImGui::CollapsingHeader("Machine Envelope", sectionFlags)) {
        if (envelopePresets.drawPicker(
                "envelope",
                [this] {
                    return plotter::kit::envelopePresetJson(
                        machinePrefs.envelope.minX, machinePrefs.envelope.minY,
                        machinePrefs.envelope.maxX, machinePrefs.envelope.maxY);
                },
                [this, &changed](const ofJson& j) {
                    machinePrefs.envelope.minX = j["minX"].get<float>();
                    machinePrefs.envelope.minY = j["minY"].get<float>();
                    machinePrefs.envelope.maxX = j["maxX"].get<float>();
                    machinePrefs.envelope.maxY = j["maxY"].get<float>();
                    machinePrefs.save();
                    changed = true;
                },
                plotter::kit::envelopePresetEquals)) {
            changed = true;
        }

        const float envW = machinePrefs.envelope.maxX - machinePrefs.envelope.minX;
        const float envH = machinePrefs.envelope.maxY - machinePrefs.envelope.minY;
        ImGui::TextDisabled("Size %.0f x %.0f mm", envW, envH);

        ImGui::SetNextItemWidth(fieldW);
        changed |= ImGui::DragFloat("Min X", &machinePrefs.envelope.minX, 1.f, -5000.f, 5000.f, "%.0f mm");
        ImGui::SetNextItemWidth(fieldW);
        changed |= ImGui::DragFloat("Min Y", &machinePrefs.envelope.minY, 1.f, -5000.f, 5000.f, "%.0f mm");
        ImGui::SetNextItemWidth(fieldW);
        changed |= ImGui::DragFloat("Max X", &machinePrefs.envelope.maxX, 1.f, -5000.f, 5000.f, "%.0f mm");
        ImGui::SetNextItemWidth(fieldW);
        changed |= ImGui::DragFloat("Max Y", &machinePrefs.envelope.maxY, 1.f, -5000.f, 5000.f, "%.0f mm");
        ImGui::SetNextItemWidth(fieldW);
        changed |= ImGui::DragFloat("Min Z", &machinePrefs.envelope.minZ, 0.5f, -500.f, 500.f, "%.1f mm");
        ImGui::SetNextItemWidth(fieldW);
        changed |= ImGui::DragFloat("Max Z", &machinePrefs.envelope.maxZ, 0.5f, -500.f, 500.f, "%.1f mm");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Software Z travel limits (machine mm). Sends are refused when any\n"
                "move (incl. pen up/down and snippets) would leave this range.");

        bool invX = machinePrefs.axes.signX < 0.f;
        bool invY = machinePrefs.axes.signY < 0.f;
        if (ImGui::Checkbox("Invert +X", &invX)) {
            machinePrefs.axes.signX = invX ? -1.f : 1.f;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "When checked, positive paper X travels toward decreasing machine X.\n"
                "Paper origin still sets where paper (0,0) sits in machine mm.");
        if (ImGui::Checkbox("Invert +Y", &invY)) {
            machinePrefs.axes.signY = invY ? -1.f : 1.f;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "When checked, positive paper Y travels toward decreasing machine Y.\n"
                "Paper origin still sets where paper (0,0) sits in machine mm.");
        bool invZ = machinePrefs.axes.signZ < 0.f;
        if (ImGui::Checkbox("Invert +Z", &invZ)) {
            machinePrefs.axes.signZ = invZ ? -1.f : 1.f;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "For machines whose Z zero is the top of travel (pen up = negative Z).\n"
                "The app keeps working with pen up = +Z; every Z in the outgoing\n"
                "G-code (send/export) gets its sign flipped.");

        if (ImGui::Button("Open USB Serial (Bed Layout)", ImVec2(-1.f, 0.f)))
            ofkitty::runtime().setWindowVisible("USB Serial", true);
    }

    if (ImGui::CollapsingHeader("Pen", sectionFlags)) {
        bool penExportChanged = false;

        if (penPresets.drawPicker(
                "pen",
                [this] {
                    return plotter::kit::penPresetJson(
                        m_plotDoc.pen, m_panel.drawColor, m_showMark);
                },
                [this, &penExportChanged](const ofJson& j) {
                    plotter::kit::applyPenPreset(
                        j, m_plotDoc.pen, m_panel.drawColor, m_showMark);
                    m_panel.scaleStrokeToPenWidth = m_showMark;
                    m_panel.penStrokeWidthMm = m_plotDoc.pen.penWidth;
                    m_panel.overrideColors     = true;
                    m_injectionsPanel.setPenUpZ(m_plotDoc.pen.penUpZ);
                    m_injectionsPanel.setPenDownZ(m_plotDoc.pen.penDownZ);
                    penExportChanged = true;
                },
                plotter::kit::penPresetEquals)) {
            penExportChanged = true;
        }

        ImGui::SetNextItemWidth(fieldW);
        if (ImGui::DragFloat("Pen Up", &m_plotDoc.pen.penUpZ, 0.1f, -40.f, 40.f, "%.1f mm"))
            penExportChanged = true;
        ImGui::SetNextItemWidth(fieldW);
        if (ImGui::DragFloat("Pen Down", &m_plotDoc.pen.penDownZ, 0.1f, -20.f, 20.f, "%.1f mm"))
            penExportChanged = true;
        ImGui::SetNextItemWidth(fieldW);
        if (ImGui::DragFloat("Draw speed", &m_plotDoc.pen.drawSpeed, 10.f, 100.f, 10000.f, "%.0f mm/min"))
            penExportChanged = true;
        ImGui::SetNextItemWidth(fieldW);
        if (ImGui::DragFloat("Travel speed", &m_plotDoc.pen.travelSpeed, 10.f, 100.f, 10000.f, "%.0f mm/min"))
            penExportChanged = true;
        ImGui::SetNextItemWidth(fieldW);
        if (ImGui::DragFloat("Pen Width", &m_plotDoc.pen.penWidth, 0.05f, 0.05f, 10.f, "%.2f mm")) {
            m_panel.penStrokeWidthMm = m_plotDoc.pen.penWidth;
            penExportChanged = true;
        }
        // ── Lead In / Out — all motion shaping around stroke ends. Data stays
        // in PenSettings (physically coupled to the pen; presets cover it);
        // only the UI is grouped here.
        const bool leadOpen = ImGui::TreeNodeEx(
            "Lead In / Out (smooth motion)",
            ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "How the brush lands on and lifts off each stroke: parabolic\n"
                "approach/retract overshoot, lead overlap, and feed easing.");
        if (leadOpen) {
        if (ImGui::Checkbox("Smooth sine approach", &m_plotDoc.pen.smoothApproach))
            penExportChanged = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Sine-eased XYZ lead-in/out so the brush is already moving\n"
                "when Z reaches pen-down. Uses draw speed. Off = square plunge.");
        if (m_plotDoc.pen.smoothApproach) {
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::DragFloat("Approach", &m_plotDoc.pen.approachMm, 0.1f, 0.f, 50.f, "%.1f mm"))
                penExportChanged = true;
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::DragFloat("Retract", &m_plotDoc.pen.retractMm, 0.1f, 0.f, 50.f, "%.1f mm"))
                penExportChanged = true;
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::DragFloat("Lead overlap", &m_plotDoc.pen.leadOverlapMm, 0.05f, 0.f,
                                 std::max({0.f, m_plotDoc.pen.approachMm,
                                           m_plotDoc.pen.retractMm}), "%.2f mm"))
                penExportChanged = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Move both lead curves INTO the stroke: touchdown lands this far\n"
                    "AFTER the stroke start, and the lift-off climb begins this far\n"
                    "BEFORE the stroke end. The brush keeps marking while the tangent\n"
                    "curves hug the paper, so with 0 the mark bleeds outside both ends;\n"
                    "overlap them so the fade-in/out land ON the intended endpoints.\n"
                    "The curves follow the actual path inside the stroke.");
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::DragFloat("Hover height", &m_plotDoc.pen.approachHeightMm, 0.1f, 0.f, 50.f, "%.1f mm"))
                penExportChanged = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Low height where the parabolic landing begins. The tool travels\n"
                    "high at Pen Up (to clear the paint reservoir), drops to this hover,\n"
                    "then glides in. Small hover + longer Approach = gentler landing.");
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::DragFloat("Curve", &m_plotDoc.pen.approachCurvePow, 0.05f, 1.f, 4.f, "%.2f"))
                penExportChanged = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Landing curve exponent: 1 = straight ramp, 2 = parabola (tangent\n"
                    "'kiss' — brush touches while moving along the stroke), >2 = flatter.");
            // Angles from the real power-curve slope (see landingAngleInfo), not
            // the chord atan(hover/approach).
            {
                const plotter::LandingAngleInfo ang = plotter::landingAngleInfo(m_plotDoc.pen);
                if (ang.smooth) {
                    ImGui::TextDisabled(
                        "Entry %.0f\xc2\xb0 \xc2\xb7 touchdown %.0f\xc2\xb0  (hover %.1f / approach %.1f mm, pow %.2f)",
                        ang.entryDeg, ang.touchDeg, ang.h, ang.L, ang.p);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Entry = slope leaving hover: atan(pow\xc2\xb7hover/approach).\n"
                            "Touchdown = slope at paper: 0\xc2\xb0 when Curve > 1 (kiss),\n"
                            "atan(hover/approach) when Curve = 1 (straight ramp).\n"
                            "Old 'Landing ~atan(hover/approach)' was only the chord.");
                } else {
                    ImGui::TextDisabled(
                        "Square plunge 90\xc2\xb0 from pen-up %.1f mm  (Approach 0 — lead-in off)",
                        ang.penUpSpan);
                }
            }
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::DragInt("Approach steps", &m_plotDoc.pen.approachSteps, 1, 2, 64))
                penExportChanged = true;

            // Live side-profile of the real landing motion — updates as values change.
            drawApproachLandingProfile(m_plotDoc.pen);

            ImGui::Checkbox("Show landing pads", &m_showLandingPads);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Overlay each stroke's touchdown + lead-in (and lift-off)\n"
                                  "on the toolpath. Also in View \xe2\x86\x92 Preview.");
            if (m_showLandingPads) {
                ImGui::SameLine();
                ImGui::ColorEdit4("Pad color", (float*)&m_landingColor,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            }
        }

        {
            const auto bed = plotter::BedView::fromPrefs(machinePrefs);
            const plotter::LeadBounds lb = plotter::computeLeadBoundsFromPaperPaths(
                m_plotDoc.getPaths(), bed, m_plotDoc.pen);
            ImGui::Checkbox("Show Lead bounds", &m_showLeadBounds);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Artwork bounding box plus Approach/Retract overshoot in\n"
                    "machine mm. Align this box to the bed before running.");
            if (m_showLeadBounds) {
                ImGui::SameLine();
                ImGui::ColorEdit4("Bounds color", (float*)&m_leadBoundsColor,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            }
            if (lb.valid) {
                const glm::vec2 sz = lb.size();
                const bool fits = lb.fitsIn(machinePrefs.envelope);
                ImGui::TextDisabled("Lead bounds %.1f \xc3\x97 %.1f mm", sz.x, sz.y);
                if (fits)
                    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.f), "Fits machine envelope");
                else
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.f),
                                       "Outside machine envelope");
            } else {
                ImGui::TextDisabled("Lead bounds — no paths yet");
            }
        }

        if (ImGui::Checkbox("Feed easing", &m_plotDoc.pen.feedEasing))
            penExportChanged = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Sine-ramp F over the first/last mm of draw strokes and\n"
                "pen-up travels (Z lift + XY). Softens abrupt start/stop;\n"
                "GRBL accel still applies. Pen-up cruise uses Travel speed.\n"
                "True G0 rapids ignore F — with easing on, travels are G1.");
        if (m_plotDoc.pen.feedEasing) {
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::DragFloat("Ease in", &m_plotDoc.pen.easeInMm, 0.1f, 0.f, 100.f, "%.1f mm"))
                penExportChanged = true;
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::DragFloat("Ease out", &m_plotDoc.pen.easeOutMm, 0.1f, 0.f, 100.f, "%.1f mm"))
                penExportChanged = true;
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::DragFloat("Min feed", &m_plotDoc.pen.easeMinFeedFrac, 0.01f, 0.05f, 1.f, "%.2f x"))
                penExportChanged = true;
        }
        ImGui::TreePop();
        } // Lead In / Out
        m_panel.overrideColors = true;
        ImGui::ColorEdit4("Mark color", (float*)&m_panel.drawColor,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Colour of the thick mark footprint.\n"
                              "Visibility: View \xe2\x86\x92 Preview \xe2\x86\x92 Show Mark.");
        ImGui::SameLine();
        ImGui::ColorEdit4("Toolpath color", (float*)&m_toolpathColor,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Colour of the thin machine path.\n"
                              "Visibility: View \xe2\x86\x92 Preview \xe2\x86\x92 Show Toolpath.");

        if (penExportChanged) {
            m_injectionsPanel.setPenUpZ(m_plotDoc.pen.penUpZ);
            m_injectionsPanel.setPenDownZ(m_plotDoc.pen.penDownZ);
            // Flush live pen state into the pen_settings_component and mark the
            // project dirty so the .ofdoc save (which reads the component, not
            // m_plotDoc.pen) round-trips pen settings and applied pen presets.
            markProjectDirty();
            markPrepareDirty("Pen settings changed — press Update/Generate");
        }
    }

    if (ImGui::CollapsingHeader("Paper", sectionFlags)) {
        m_zonesPanel.drawTargetZonePicker();

        // Draw-target zone is the source of truth (same as Properties). Mirror fields
        // alone were overwritten by syncToolpathPreview / settingsDirty reimport.
        plotter::machine_zone_component* zone = nullptr;
        {
            const entt::entity target = m_zones.findDrawTargetZone(m_registry);
            if (target != entt::null)
                zone = m_registry.try_get<plotter::machine_zone_component>(target);
        }

        if (!zone) {
            ImGui::TextDisabled("No draw-target zone — pick or create one above.");
        } else {
            if (paperPresets.drawPicker(
                    "paper",
                    [zone] {
                        return plotter::kit::paperPresetJson(zone->x, zone->y, zone->w, zone->h);
                    },
                    [this, zone](const ofJson& j) {
                        zone->x = j["paperOriginX"].get<float>();
                        zone->y = j["paperOriginY"].get<float>();
                        zone->w = std::max(10.f, j["paperW"].get<float>());
                        zone->h = std::max(10.f, j["paperH"].get<float>());
                        syncDrawTarget(false);
                    },
                    plotter::kit::paperPresetEquals)) {
                // apply already synced
            }

            bool paperChanged = false;
            ImGui::SetNextItemWidth(fieldW);
            paperChanged |= ImGui::DragFloat("Origin X", &zone->x, 1.f, -500.f, 5000.f, "%.0f mm");
            ImGui::SetNextItemWidth(fieldW);
            paperChanged |= ImGui::DragFloat("Origin Y", &zone->y, 1.f, -500.f, 5000.f, "%.0f mm");
            ImGui::SetNextItemWidth(fieldW);
            paperChanged |= ImGui::DragFloat("Paper W",  &zone->w, 1.f, 10.f, 5000.f, "%.0f mm");
            ImGui::SetNextItemWidth(fieldW);
            paperChanged |= ImGui::DragFloat("Paper H",  &zone->h, 1.f, 10.f, 5000.f, "%.0f mm");
            if (paperChanged)
                syncDrawTarget(false);
        }
    }

    if (ImGui::CollapsingHeader("Source Output", sectionFlags)) {
        const float labelColW = labelColumnWidth(
            { "Offset X", "Offset Y", "Scale X", "Scale Y", "Rotate" });

        bool transformChanged = false;
        transformChanged |= dragFloatLabeled(labelColW, "Offset X", &printOffsetX, 0.1f, -2000.f, 2000.f, "%.2f mm");
        transformChanged |= dragFloatLabeled(labelColW, "Offset Y", &printOffsetY, 0.1f, -2000.f, 2000.f, "%.2f mm");
        transformChanged |= dragFloatLabeled(labelColW, "Scale X", &printScaleX, 0.01f, -10.f, 10.f, "%.3f");
        transformChanged |= dragFloatLabeled(labelColW, "Scale Y", &printScaleY, 0.01f, -10.f, 10.f, "%.3f");
        if (std::abs(printScaleX) < 0.001f) printScaleX = (printScaleX < 0.f) ? -0.001f : 0.001f;
        if (std::abs(printScaleY) < 0.001f) printScaleY = (printScaleY < 0.f) ? -0.001f : 0.001f;

        static const char* kRotLabels[] = { "0 deg", "90 deg CW", "180 deg", "270 deg CW" };
        transformChanged |= comboLabeled(labelColW, "Rotate", &printRotateQuarterTurns,
                                         kRotLabels, IM_ARRAYSIZE(kRotLabels));

        if (ImGui::Button("Flip X sign")) { printScaleX = -printScaleX; transformChanged = true; }
        ImGui::SameLine();
        if (ImGui::Button("Flip Y sign")) { printScaleY = -printScaleY; transformChanged = true; }

        if (transformChanged)
            markPrepareDirty("Source transform changed — press Update/Generate");
    }

    if (ImGui::CollapsingHeader("Pipeline", sectionFlags)) {
        if (ImGui::Checkbox("Use Pipeline", &usePipeline)) {
            syncExportSessionFlags();
            markPrepareDirty(usePipeline ? "Pipeline enabled — press Update/Generate"
                                         : "Pipeline disabled — press Update/Generate");
        }
        if (ImGui::Button("Open Pipeline window", ImVec2(-1.f, 0.f)))
            ofkitty::runtime().setWindowVisible("Pipeline", true);
    }

    if (ImGui::CollapsingHeader("Injections", sectionFlags)) {
        if (ImGui::Checkbox("Use Injections", &useInjections)) {
            syncExportSessionFlags();
            markPrepareDirty(useInjections ? "Injections enabled — press Update/Generate"
                                           : "Injections disabled — press Update/Generate");
        }
        if (ImGui::Button("Open Injections window", ImVec2(-1.f, 0.f)))
            ofkitty::runtime().setWindowVisible("Injections", true);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (prepareDirty)
        ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f), "Prepare stale");
    else if (m_exportSession.isExporting())
        ImGui::TextDisabled("Preparing…");
    else
        ImGui::TextDisabled("Prepared up to date");

    const bool preparing = m_exportSession.isExporting() || prepareRequested;
    if (preparing) ImGui::BeginDisabled();
    if (ImGui::Button(ICON_FA_SYNC " Update / Generate", ImVec2(-1.f, 0.f)))
        requestPrepare();
    if (preparing) ImGui::EndDisabled();

    if (ImGui::Button("USB Serial Window", ImVec2(-1.f, 0.f)))
        ofkitty::runtime().setWindowVisible("USB Serial", true);
    if (ImGui::Button("Serial Console", ImVec2(-1.f, 0.f)))
        ofkitty::runtime().setWindowVisible("Serial Console", true);

    const bool canSend = sender.isConnected();
    if (!canSend) ImGui::BeginDisabled();
    if (ImGui::Button(ICON_FA_PRINT " Send to printer", ImVec2(-1.f, 0.f)))
        sendPreparedGcodeToPrinter();
    if (!canSend) ImGui::EndDisabled();

    if (ImGui::Button(ICON_FA_SAVE " Export prepared G-code…", ImVec2(-1.f, 0.f)))
        savePreparedGcode();

    if (changed) {
        machinePrefs.bed.paperOriginX = paperOriginX;
        machinePrefs.bed.paperOriginY = paperOriginY;
        machinePrefs.save();
        syncMainViewContentSize();
        markPrepareDirty("Machine settings changed — press Update/Generate");
        // Do not set settingsDirty here: update() reimports the whole source file and
        // resets ImGui drag/input state (and used to clobber paper size from the zone).
    }

    ImGui::End();
}

void ofApp::drawCodePanel(bool& visible)
{
    // MenuBar flag required — without it File/Edit never appear (BeginMenuBar fails).
    if (!ImGui::Begin(gcodeSender::kGcodeWindow, &visible, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    // Playback/scrub → highlight current G-code line
    if (syncCursorToPlayback && m_panel.hasGeometry())
        syncEditorToPlayback();

    // Reverse sync: editor cursor → playback
    if (syncCursorToPlayback && m_panel.hasGeometry()) {
        int curLine = 0, curCol = 0;
        codeEditor.GetCursorPosition(curLine, curCol);
        if (curLine != lastEditorLine) {
            lastEditorLine = curLine;
            const int lineCount = codeEditor.GetLineCount();
            m_panel.setPlayback(plotterGcodeSync::lineToPlayback(curLine, lineCount));
        }
    }

    const bool dirty = editorDirty;
    const bool sourceView = (gcodeEditorView == GcodeEditorView::Source);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            ImGui::BeginDisabled(!dirty || sourceView);
            if (ImGui::MenuItem("Save", "Ctrl+S"))
                saveEditorToFile(false);
            ImGui::EndDisabled();
            if (ImGui::MenuItem("Save As…", "Ctrl+Shift+S"))
                saveEditorToFile(true);
            ImGui::Separator();
            if (ImGui::MenuItem("Export prepared…"))
                savePreparedGcode();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Find", "Ctrl+F"))
                m_codeEditor.showFind(false);
            if (ImGui::MenuItem("Find & Replace", "Ctrl+Shift+F"))
                m_codeEditor.showFind(true);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Regex: toggle [.*] in the find bar.");
            if (ImGui::MenuItem("Parse"))
                parseEditorText();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            const bool showPrepared = !sourceView;
            if (ImGui::MenuItem("Prepared G-code", nullptr, showPrepared)) {
                if (!showPrepared) {
                    gcodeEditorView = GcodeEditorView::Prepared;
                    syncGcodeEditorToView();
                }
            }
            if (ImGui::MenuItem("Source G-code", nullptr, sourceView)) {
                if (!sourceView) {
                    gcodeEditorView = GcodeEditorView::Source;
                    syncGcodeEditorToView();
                }
            }
            ImGui::Separator();
            ImGui::MenuItem("Sync cursor to playback", nullptr, &syncCursorToPlayback);
            ImGui::EndMenu();
        }
        {
            const char* viewTag = sourceView ? "source" : "prepared";
            const std::string status = std::string(viewTag) + "  |  "
                + std::to_string(codeEditor.GetLineCount()) + " lines"
                + (editorDirty ? "  (modified)" : "");
            const float tw = ImGui::CalcTextSize(status.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - tw - ImGui::GetStyle().WindowPadding.x);
            ImGui::TextDisabled("%s", status.c_str());
        }
        ImGui::EndMenuBar();
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S))
            saveEditorToFile(true);
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && !sourceView)
            saveEditorToFile(false);
        m_codeEditor.handleShortcuts();
    }

    m_codeEditor.drawFindReplace(codeEditor, "gcode");
    if (sourceView)
        ImGui::BeginDisabled();
    // Fill remaining space (same idea as TextEditorPanel / example-codeEditor).
    // Pass real focus — hardcoding true steals keys from modal save/open dialogs.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool editorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)
        && ImGui::GetTopMostPopupModal() == nullptr;
    codeEditor.Render("##gcode_editor", editorFocused,
                      ImVec2(-1.f, avail.y > 40.f ? avail.y : 40.f), true);
    if (sourceView)
        ImGui::EndDisabled();
    ImGui::End();
}

// ── input ─────────────────────────────────────────────────────────────────────

void ofApp::keyPressed(int key)
{
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (key == 'o' || key == 'O') openImportGcodeDialog();
    if ((key == 'r' || key == 'R') && !sourceGcodePath.empty()) importGcodeFile(sourceGcodePath);
    if (key == 's' || key == 'S') { markProjectDirty(); m_docKit.saveDocument(); }
    if (key == 'n' || key == 'N') m_docKit.newDocument();
}

void ofApp::dragEvent(ofDragInfo info)
{
    (void)info;
}

void ofApp::windowResized(int, int)
{
}

void ofApp::markProjectDirty()
{
    m_docKit.session().markDirty();
    if (m_plotDoc.docEntity() != entt::null) {
        if (m_registry.all_of<plotter::pen_settings_component>(m_plotDoc.docEntity()))
            m_registry.get<plotter::pen_settings_component>(m_plotDoc.docEntity()).pen = m_plotDoc.pen;
        else
            m_registry.emplace<plotter::pen_settings_component>(m_plotDoc.docEntity()).pen = m_plotDoc.pen;
    }
}

plotter::PlotterDocumentEnvelope ofApp::capturePlotterEnvelope() const
{
    plotter::PlotterDocumentEnvelope env;
    env.drawTargetZoneId = m_zones.drawTargetZoneId;
    env.pipeline = m_pipeline;
    env.printOffsetX = printOffsetX;
    env.printOffsetY = printOffsetY;
    env.printScaleX = printScaleX;
    env.printScaleY = printScaleY;
    env.printRotateQuarterTurns = printRotateQuarterTurns;
    env.liveTransformEnabled = false;
    env.usePipeline = usePipeline;
    env.useInjections = useInjections;
    env.machineEnvelope = machinePrefs.envelope;
    env.machineBed = machinePrefs.bed;
    env.hasMachineBed = true;
    // Per-document mark appearance (artwork state, not machine config).
    env.penColor = m_panel.drawColor;
    env.scaleStrokeToPenWidth = m_showMark; // persisted key; UI: Show Mark
    env.hasPenAppearance = true;
    return env;
}

void ofApp::applyPlotterEnvelope(const plotter::PlotterDocumentEnvelope& env)
{
    m_zones.drawTargetZoneId = env.drawTargetZoneId;
    m_pipeline = env.pipeline;
    printOffsetX = env.printOffsetX;
    printOffsetY = env.printOffsetY;
    printScaleX = env.printScaleX;
    printScaleY = env.printScaleY;
    printRotateQuarterTurns = env.printRotateQuarterTurns;
    usePipeline = env.usePipeline;
    useInjections = env.useInjections;
    syncExportSessionFlags();
    // Restore per-document pen appearance; legacy docs (hasPenAppearance == false)
    // keep the current app-global pref so nothing regresses.
    if (env.hasPenAppearance) {
        m_panel.drawColor = env.penColor;
        m_showMark = env.scaleStrokeToPenWidth; // persisted key; UI: Show Mark
        m_panel.scaleStrokeToPenWidth = m_showMark;
        m_panel.overrideColors = true;
    }
    if (env.hasMachineBed) {
        machinePrefs.envelope = env.machineEnvelope;
        machinePrefs.bed = env.machineBed;
        paperOriginX = machinePrefs.bed.paperOriginX;
        paperOriginY = machinePrefs.bed.paperOriginY;
    }
    syncDrawTarget(false);
}

void ofApp::onDocumentLoaded()
{
    m_exportSession.cancelJoin();
    m_exportSession.invalidate();
    m_plotDoc.rebindFromRegistry();
    m_plotDoc.setDrawTargetSource(&m_zones);
    ensureDefaultDrawTargetZone();

    // Pen numeric settings were just restored into m_plotDoc.pen; keep the
    // preview stroke width in sync (pen color/scale come via applyPlotterEnvelope).
    m_panel.penStrokeWidthMm = m_plotDoc.pen.penWidth;
    m_injectionsPanel.setPenUpZ(m_plotDoc.pen.penUpZ);
    m_injectionsPanel.setPenDownZ(m_plotDoc.pen.penDownZ);

    if (m_plotDoc.docEntity() != entt::null
        && m_registry.all_of<plotter::plot_doc_component>(m_plotDoc.docEntity())) {
        const auto& dc = m_registry.get<plotter::plot_doc_component>(m_plotDoc.docEntity());
        if (!dc.sourcePath.empty()) {
            sourceGcodePath = dc.sourcePath;
            if (ofFile::doesFileExist(sourceGcodePath)) {
                const ofBuffer buf = ofBufferFromFile(sourceGcodePath);
                liveSourceGcode = buf.getText();
                liveSourceValid = !liveSourceGcode.empty();
            }
        }
    }

    syncMainViewContentSize();

    if (liveSourceValid)
        requestPrepare();
    else
        markPrepareDirty("Document ready — import G-code then Update/Generate");
    statusMessage = "Document ready.";
}

void ofApp::wireDocumentKit()
{
    plotter::registerPlotterDocumentSerializers();

    m_docKit.registerWith(ofkitty::runtime(), {
        .registry = &m_registry,
        .registerFileMenu = false,
        .wireFileDrop = false,
        .prefsSubdir = "gcodeSender",
    });

    m_docKit.session().serializer().setRootExtensionWriter(
        [this](entt::registry&, ofxDocumentKit::ordered_json& root) {
            plotter::writePlotterDocumentEnvelope(root, capturePlotterEnvelope());
        });
    m_docKit.session().serializer().setRootExtensionReader(
        [this](entt::registry&, const ofxDocumentKit::ordered_json& root) {
            plotter::PlotterDocumentEnvelope env;
            plotter::readPlotterDocumentEnvelope(root, env);
            applyPlotterEnvelope(env);
        });

    m_docKit.session().setOnBeforeClear([this] {
        m_exportSession.cancelJoin();
        m_exportSession.invalidate();
        liveSourceGcode.clear();
        liveSourceValid = false;
        sourceGcodePath.clear();
    });
    m_docKit.session().setOnDocumentLoaded([this] { onDocumentLoaded(); });

    ofkitty::runtime().addMenuBarGroup("File", [this] {
        if (ImGui::MenuItem("New", "Ctrl+N"))
            m_docKit.newDocument();
        if (ImGui::MenuItem("Open…", "Ctrl+O")) {
            ofkitty::runtime().openFileDialog(
                "gcode_sender.open_ofdoc", "Open Project", ".ofdoc",
                [this](const std::string& path) { m_docKit.openDocument(path); });
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            markProjectDirty(); // flush live pen state into the component first
            m_docKit.saveDocument();
        }
        if (ImGui::MenuItem("Save As…")) {
            ofkitty::runtime().saveFileDialog(
                "gcode_sender.save_as", "Save Project As", ".ofdoc", "Untitled.ofdoc",
                [this](const std::string& path) {
                    markProjectDirty(); // flush live pen state into the component first
                    m_docKit.saveDocumentAs(path);
                });
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import G-code…"))
            openImportGcodeDialog();
        if (!sourceGcodePath.empty() && ImGui::MenuItem("Reload G-code", "R"))
            importGcodeFile(sourceGcodePath);
        // G-code Save As / Export prepared live on the G-code window File menu.
    });
}
