#include "PlotterPrintPreviewPanel.h"
#include "ImFonts.h"
#include "IconsFontAwesome5.h"
#include "PlotterPreviewGrids.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace plotter::kit {

// ── Data ─────────────────────────────────────────────────────────────────────

bool PlotterPrintPreviewPanel::loadFromFile(const std::string& path,
                                             const PrintPreviewOptions& opts)
{
    m_lastOpts = opts;
    m_view.contentSize = glm::vec2(opts.bounds.size());
    if (!m_preview.loadFromFile(path, opts)) return false;
    m_playback  = 1.f;
    m_isPlaying = false;
    parseTravelPaths();
    m_view.fitToCanvas();
    return true;
}

bool PlotterPrintPreviewPanel::loadFromText(const std::string& text,
                                             const PrintPreviewOptions& opts)
{
    m_lastOpts = opts;
    m_view.contentSize = glm::vec2(opts.bounds.size());
    if (!m_preview.loadFromText(text, opts)) return false;
    m_playback  = 1.f;
    m_isPlaying = false;
    parseTravelPaths();
    m_view.fitToCanvas();
    return true;
}

void PlotterPrintPreviewPanel::clear()
{
    m_preview.clear();
    m_travelPaths.clear();
    m_playback  = 1.f;
    m_isPlaying = false;
}

bool PlotterPrintPreviewPanel::hasGeometry() const
{
    return m_preview.hasGeometry();
}

const std::string& PlotterPrintPreviewPanel::sourceText() const
{
    return m_preview.sourceText();
}

const std::string& PlotterPrintPreviewPanel::sourceLabel() const
{
    return m_preview.sourceLabel();
}

// ── Companion actions ─────────────────────────────────────────────────────────

void PlotterPrintPreviewPanel::addCompanionAction(const std::string& label,
                                                   std::function<void()> cb)
{
    m_companionActions.push_back({label, std::move(cb)});
}

void PlotterPrintPreviewPanel::clearCompanionActions()
{
    m_companionActions.clear();
}

// ── Per-frame ─────────────────────────────────────────────────────────────────

void PlotterPrintPreviewPanel::update(float dt)
{
    if (!m_isPlaying || !m_preview.hasGeometry()) return;
    const int pathCount = (int)m_preview.paths().size();
    if (pathCount <= 0) return;
    m_playback += (playbackSpeed / (float)pathCount) * dt;
    if (m_playback >= 1.f) {
        m_playback  = 1.f;
        m_isPlaying = false;
    }
}

// ── Transport ─────────────────────────────────────────────────────────────────

int PlotterPrintPreviewPanel::maxPathIndex() const
{
    if (!m_preview.hasGeometry()) return 0;
    const int n = (int)m_preview.paths().size();
    return std::clamp((int)(m_playback * n), 0, n);
}

void PlotterPrintPreviewPanel::setPlayback(float t)
{
    m_playback  = std::clamp(t, 0.f, 1.f);
    m_isPlaying = false;
}

void PlotterPrintPreviewPanel::stepPath(int delta)
{
    if (!m_preview.hasGeometry()) return;
    const int n    = (int)m_preview.paths().size();
    if (n == 0) return;
    const int cur  = std::clamp((int)(m_playback * n), 0, n);
    const int next = std::clamp(cur + delta, 0, n);
    m_playback  = (float)next / (float)n;
    m_isPlaying = false;
}

void PlotterPrintPreviewPanel::fitView()
{
    m_view.fitToCanvas();
}

// ── Standalone ImGui window ───────────────────────────────────────────────────

void PlotterPrintPreviewPanel::draw(bool& visible)
{
    const char* title = m_imguiWindowTitle.empty() ? name().c_str() : m_imguiWindowTitle.c_str();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    const bool open = ImGui::Begin(title, &visible, ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar();
    if (!open) { ImGui::End(); return; }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Fit"))
                m_view.fitToCanvas();
            ImGui::MenuItem("Show Travel Path", nullptr, &showTravelPaths);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Compact toolbar row
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 3.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4.f, 3.f));
    drawHeader();
    ImGui::PopStyleVar(2);
    ImGui::Separator();

    // Transport row
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 3.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4.f, 3.f));
    drawTransportControls();
    ImGui::PopStyleVar(2);
    ImGui::Separator();

    // FBO preview canvas → ImGui vector canvas (crisp at any zoom; grids as overlay).
    const ImVec2 pos   = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int w = std::max(1, (int)avail.x);
    const int h = std::max(1, (int)avail.y);

    m_view.contentSize  = glm::vec2(m_lastOpts.bounds.size());
    m_view.canvasOrigin = {pos.x, pos.y};
    m_view.canvasW      = (float)w;
    m_view.canvasH      = (float)h;

    ImGui::InvisibleButton("##pp_canvas", ImVec2((float)w, (float)h));
    const bool hovered = ImGui::IsItemHovered();
    m_view.hovered = hovered;

    ImGuiIO& io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0.f) {
        m_view.applyScrollZoom(io.MouseWheel, io.MousePos.x, io.MousePos.y);
        io.MouseWheel  = 0.f;
        io.MouseWheelH = 0.f;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        m_view.fitToCanvas();
    if (hovered && (io.MouseDown[ImGuiMouseButton_Middle]
                 || (io.MouseDown[ImGuiMouseButton_Left] && io.KeyAlt)))
        m_view.applyPanDelta(io.MouseDelta.x, io.MouseDelta.y);

    m_view.updateDerived();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 canvasMax(pos.x + (float)w, pos.y + (float)h);
    dl->PushClipRect(pos, canvasMax, true);
    dl->AddRectFilled(pos, canvasMax, IM_COL32(18, 18, 24, 255));

    const float ox   = m_view.ox;
    const float oy   = m_view.oy;
    const float zoom = m_view.zoom_;
    drawCanvasImGui(dl, ox, oy, zoom, pos, canvasMax);
    dl->PopClipRect();

    ImGui::End();
}

// ── Header (ViewportInstance-compatible) ──────────────────────────────────────

bool PlotterPrintPreviewPanel::drawHeader()
{
    for (auto& act : m_companionActions) {
        if (ImGui::Button(act.label.c_str()) && act.cb) act.cb();
        ImGui::SameLine();
    }

    if (!m_preview.hasGeometry()) {
        ImGui::TextDisabled("No G-code loaded.");
        return false;
    }

    if (!m_preview.sourceLabel().empty())
        ImGui::TextDisabled("%s", m_preview.sourceLabel().c_str());

    const int   n   = (int)m_preview.paths().size();
    const float pct = m_playback * 100.f;
    float       tmp = pct;
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::SliderFloat("##pp_hdr_scrub", &tmp, 0.f, 100.f, "%.0f%%  |  paths"))
        setPlayback(tmp / 100.f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Path %d / %d", maxPathIndex(), n);

    return false;
}

// ── ImGui vector canvas ───────────────────────────────────────────────────────

void PlotterPrintPreviewPanel::drawCanvasImGui(ImDrawList* dl,
                                               float ox, float oy, float zoom,
                                               const ImVec2& clipMin,
                                               const ImVec2& clipMax)
{
    if (!dl || zoom <= 0.f) return;

    const BedView&       bed = m_lastOpts.bed;
    const PreviewBounds& pb  = m_lastOpts.bounds;

    const float envOrigX = bed.envelope.minX - pb.minX;
    const float envOrigY = bed.envelope.minY - pb.minY;
    const glm::vec2 paperOrg = pb.machineToContent(bed.bed.paperOriginX,
                                                   bed.bed.paperOriginY);

    ofColor envOutline = ofColor(envelopeColor);
    envOutline.r = (unsigned char)std::min(255, envOutline.r + 60);
    envOutline.g = (unsigned char)std::min(255, envOutline.g + 60);
    envOutline.b = (unsigned char)std::min(255, envOutline.b + 60);

    plotter::drawPrintPreviewBedImGui(dl, ox, oy, zoom,
                                      envOrigX, envOrigY,
                                      bed.envelope.spanX(), bed.envelope.spanY(),
                                      paperOrg.x, paperOrg.y,
                                      paperW, paperH,
                                      envelopeColor, ofColor(paperColor),
                                      envOutline);

    if (m_registry) {
        plotter::drawZoneGrids(dl, *m_registry, pb,
                               ox, oy, zoom,
                               clipMin.x, clipMin.y,
                               clipMax.x - clipMin.x,
                               clipMax.y - clipMin.y,
                               m_yAxisUp, m_gridStyle);
    }

    if (m_preview.hasGeometry()) {
        const int n       = (int)m_preview.paths().size();
        const int maxPath = std::clamp((int)(m_playback * n), 0, n);
        const float thicknessPx = scaleStrokeToPenWidth
            ? std::max(1.f, penStrokeWidthMm * zoom)
            : 1.5f;
        if (overrideColors) {
            const ofColor              col(drawColor);
            const std::vector<ofColor> cols(n, col);
            plotter::drawPrintPreviewPathsImGui(dl, ox, oy, zoom,
                                                m_preview.paths(), cols, maxPath,
                                                thicknessPx);
        } else {
            plotter::drawPrintPreviewPathsImGui(dl, ox, oy, zoom,
                                                m_preview.paths(),
                                                m_preview.pathColors(), maxPath,
                                                thicknessPx);
        }
    }

    // Travel (G0) on top so detours to maintenance zones stay visible over artwork.
    if (showTravelPaths && !m_travelPaths.empty()) {
        const ofColor col = ofColor(travelColor);
        const std::vector<ofColor> cols(m_travelPaths.size(), col);
        plotter::drawPrintPreviewPathsImGui(dl, ox, oy, zoom,
                                            m_travelPaths, cols, -1, 1.5f);
    }

    // Detour targets — zone positions (or zone centre when none are defined).
    if (m_registry && showTravelPaths) {
        for (auto e : plotter::collectZoneEntities(*m_registry)) {
            if (!m_registry->all_of<plotter::machine_zone_component>(e)) continue;
            const auto& z = m_registry->get<plotter::machine_zone_component>(e);
            auto mark = [&](float mx, float my) {
                const glm::vec2 c = pb.machineToContent(mx, my);
                const ImVec2 sp { ox + c.x * zoom, oy + c.y * zoom };
                dl->AddCircleFilled(sp, 4.f, IM_COL32(255, 200, 60, 255));
                dl->AddCircle(sp, 4.f, IM_COL32(40, 40, 40, 210), 0, 1.2f);
            };
            if (z.positions.empty())
                mark(z.x + z.w * 0.5f, z.y + z.h * 0.5f);
            else
                for (const auto& p : z.positions)
                    mark(p.x, p.y);
        }
    }
}

// ── Pure OF scene drawing ─────────────────────────────────────────────────────

void PlotterPrintPreviewPanel::drawScene(float pxPerMm)
{
    const BedView&       bed = m_lastOpts.bed;
    const PreviewBounds& pb  = m_lastOpts.bounds;

    const float     envOrigX = bed.envelope.minX - pb.minX;
    const float     envOrigY = bed.envelope.minY - pb.minY;
    const glm::vec2 paperOrg = pb.machineToContent(bed.bed.paperOriginX,
                                                   bed.bed.paperOriginY);

    drawPrintPreviewBed(envOrigX, envOrigY,
                        bed.envelope.spanX(), bed.envelope.spanY(),
                        paperOrg.x, paperOrg.y,
                        paperW, paperH,
                        envelopeColor,
                        ofColor(paperColor));

    // Envelope outline — brightened rim
    {
        const ofColor base = ofColor(envelopeColor);
        ofPushStyle();
        ofNoFill();
        ofSetColor(std::min(255, base.r + 60),
                   std::min(255, base.g + 60),
                   std::min(255, base.b + 60));
        ofSetLineWidth(1.f);
        ofDrawRectangle(envOrigX, envOrigY,
                        bed.envelope.spanX(), bed.envelope.spanY());
        ofPopStyle();
    }

    // G1/G2/G3 pen-down paths
    if (m_preview.hasGeometry()) {
        const int n       = (int)m_preview.paths().size();
        const int maxPath = std::clamp((int)(m_playback * n), 0, n);
        if (overrideColors) {
            const ofColor              col(drawColor);
            const std::vector<ofColor> cols(n, col);
            drawPrintPreviewPaths(m_preview.paths(), cols, maxPath, pxPerMm);
        } else {
            m_preview.draw(pxPerMm, maxPath);
        }
    }

    // G0 travel paths on top
    if (showTravelPaths && !m_travelPaths.empty()) {
        const ofColor col = ofColor(travelColor);
        ofPushStyle();
        ofNoFill();
        ofSetColor(col.r, col.g, col.b, col.a);
        ofSetLineWidth(1.5f);
        for (const auto& p : m_travelPaths)
            if (p.size() >= 2) p.draw();
        ofPopStyle();
    }
}

// ── Compact transport controls ────────────────────────────────────────────────

void PlotterPrintPreviewPanel::drawTransportControls()
{
    if (!m_preview.hasGeometry()) {
        ImGui::TextDisabled("No G-code loaded.");
        return;
    }

    const int n = (int)m_preview.paths().size();

    if (ImFonts::IconButton(ICON_FA_FAST_BACKWARD, "##pp0")) {
        m_playback = 0.f;
        m_isPlaying = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start");
    ImGui::SameLine();
    if (ImFonts::IconButton(ICON_FA_STEP_BACKWARD, "##pp1")) stepPath(-1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous path");
    ImGui::SameLine();
    if (m_isPlaying) {
        if (ImFonts::IconButton(ICON_FA_PAUSE, "##pp2")) m_isPlaying = false;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause");
    } else {
        if (ImFonts::IconButton(ICON_FA_PLAY, "##pp2")) {
            if (m_playback >= 1.f) m_playback = 0.f;
            m_isPlaying = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play");
    }
    ImGui::SameLine();
    if (ImFonts::IconButton(ICON_FA_STEP_FORWARD, "##pp3")) stepPath(+1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next path");
    ImGui::SameLine();
    if (ImFonts::IconButton(ICON_FA_FAST_FORWARD, "##pp4")) {
        m_playback = 1.f;
        m_isPlaying = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("End");
    ImGui::SameLine();
    float pct = m_playback * 100.f;
    ImGui::SetNextItemWidth(-140.f);
    if (ImGui::SliderFloat("##ppScrub", &pct, 0.f, 100.f, "%.1f%%"))
        setPlayback(pct / 100.f);
    ImGui::SameLine();
    ImGui::TextDisabled("%d/%d", maxPathIndex(), n);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.f);
    ImGui::SliderFloat("##ppSpd", &playbackSpeed, 1.f, 200.f, "%.0fps");
    ImGui::TextDisabled("Scroll zoom · Mid/Alt-drag pan · Dbl-click fit");
}

// ── Travel path parser ────────────────────────────────────────────────────────

void PlotterPrintPreviewPanel::parseTravelPaths()
{
    // Vertices keep MACHINE Z in .z (airborne feed moves included) so the 3D
    // toolpath view can show the true smooth-lead geometry; the 2D draw path
    // simply ignores the Z component.
    m_travelPaths = PlotterPrintPreview::parseTravelPathsFromGcode(
        m_preview.sourceText(), m_lastOpts.bounds,
        m_lastOpts.import.penDownMaxZ);
}

} // namespace plotter::kit
