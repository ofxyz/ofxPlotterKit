#include "PlotterPrintPreviewPanel.h"
#include "imgui.h"

#include <algorithm>
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    const bool open = ImGui::Begin(name().c_str(), &visible);
    ImGui::PopStyleVar();
    if (!open) { ImGui::End(); return; }

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

    // FBO preview canvas.
    // Input pattern mirrors ofxKit Runtime_tools drawViewportWindow2D:
    // InvisibleButton hit region, hover-gated pan/zoom, wheel consumed.
    const ImVec2 pos   = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int w = std::max(1, (int)avail.x);
    const int h = std::max(1, (int)avail.y);

    // View2DState works in canvas-local pixels: canvasOrigin stays {0,0} and
    // pivot coords are passed relative to the canvas top-left, so the derived
    // ox/oy map directly into the FBO.
    m_view.contentSize = glm::vec2(m_lastOpts.bounds.size());
    m_view.canvasW     = (float)w;
    m_view.canvasH     = (float)h;
    m_view.updateDerived();

    ImGui::InvisibleButton("##pp_canvas", ImVec2((float)w, (float)h));
    const bool hovered = ImGui::IsItemHovered();
    m_view.hovered = hovered;

    ImGuiIO& io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0.f) {
        m_view.applyScrollZoom(io.MouseWheel,
                               io.MousePos.x - pos.x,
                               io.MousePos.y - pos.y);
        // Wheel zoom owns the canvas — don't scroll the window or parent dock.
        io.MouseWheel  = 0.f;
        io.MouseWheelH = 0.f;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        m_view.fitToCanvas();
    if (hovered && (io.MouseDown[ImGuiMouseButton_Middle]
                 || (io.MouseDown[ImGuiMouseButton_Left] && io.KeyAlt)))
        m_view.applyPanDelta(io.MouseDelta.x, io.MouseDelta.y);

    m_view.updateDerived();
    renderToFbo(w, h);

    const ImTextureID texId = (ImTextureID)(uintptr_t)
        m_fbo.getTexture().getTextureData().textureID;
    ImGui::GetWindowDrawList()->AddImage(
        texId, pos, ImVec2(pos.x + (float)w, pos.y + (float)h),
        ImVec2(0.f, 1.f), ImVec2(1.f, 0.f));

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

    // G0 travel paths (under pen-down strokes)
    if (showTravelPaths && !m_travelPaths.empty()) {
        const ofColor col = ofColor(travelColor);
        ofPushStyle();
        ofNoFill();
        ofSetColor(col.r, col.g, col.b, col.a);
        ofSetLineWidth(1.f);
        for (const auto& p : m_travelPaths)
            if (p.size() >= 2) p.draw();
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
}

// ── FBO render ────────────────────────────────────────────────────────────────

void PlotterPrintPreviewPanel::renderToFbo(int w, int h)
{
    if (!m_fbo.isAllocated()
        || (int)m_fbo.getWidth()  != w
        || (int)m_fbo.getHeight() != h) {
        ofFboSettings s;
        s.width          = w;
        s.height         = h;
        s.internalformat = GL_RGBA;
        s.useDepth       = false;
        m_fbo.allocate(s);
    }

    m_fbo.begin();
    {
        const ofColor base = ofColor(envelopeColor);
        ofClear(base.r / 2, base.g / 2, base.b / 2, 255);
    }
    ofPushMatrix();
    ofTranslate(m_view.ox, m_view.oy);
    ofScale(m_view.zoom_);
    drawScene(m_view.zoom_);
    ofPopMatrix();
    m_fbo.end();
}

// ── Compact transport controls ────────────────────────────────────────────────

void PlotterPrintPreviewPanel::drawTransportControls()
{
    if (!m_preview.hasGeometry()) {
        ImGui::TextDisabled("No G-code loaded.");
        return;
    }

    const int n = (int)m_preview.paths().size();

    if (ImGui::Button("|<##pp0")) { m_playback = 0.f; m_isPlaying = false; }
    ImGui::SameLine();
    if (ImGui::Button("< ##pp1")) stepPath(-1);
    ImGui::SameLine();
    if (m_isPlaying) {
        if (ImGui::Button("||##pp2")) m_isPlaying = false;
    } else {
        if (ImGui::Button("> ##pp2")) {
            if (m_playback >= 1.f) m_playback = 0.f;
            m_isPlaying = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(" >##pp3")) stepPath(+1);
    ImGui::SameLine();
    if (ImGui::Button(">|##pp4")) { m_playback = 1.f; m_isPlaying = false; }
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
}

// ── Travel path parser ────────────────────────────────────────────────────────

void PlotterPrintPreviewPanel::parseTravelPaths()
{
    m_travelPaths.clear();
    const std::string& gcode = m_preview.sourceText();
    if (gcode.empty()) return;

    const PreviewBounds& pb = m_lastOpts.bounds;

    auto parseF = [](const std::string& tok, size_t start) -> float {
        try { return std::stof(tok.substr(start)); } catch (...) { return 0.f; }
    };

    ofPolyline seg;
    float x = 0.f, y = 0.f;
    bool  modalG0 = false;

    std::istringstream iss(gcode);
    std::string line;
    while (std::getline(iss, line)) {
        const auto ci = line.find(';');
        if (ci != std::string::npos) line.resize(ci);

        bool  isG0 = false, isG1orArc = false;
        float nx = x, ny = y;
        bool  moved = false;

        std::istringstream ls(line);
        std::string tok;
        while (ls >> tok) {
            if (tok.empty()) continue;
            const char c0 = (char)toupper((unsigned char)tok[0]);
            if (c0 == 'G') {
                int n2 = 0;
                try { n2 = std::stoi(tok.substr(1)); } catch (...) {}
                if (n2 == 0)                           isG0      = true;
                else if (n2 == 1 || n2 == 2 || n2 == 3) isG1orArc = true;
            } else if (c0 == 'X') { nx = parseF(tok, 1); moved = true; }
              else if (c0 == 'Y') { ny = parseF(tok, 1); moved = true; }
        }

        const bool wasG0 = modalG0;
        if (isG0)      modalG0 = true;
        if (isG1orArc) modalG0 = false;
        const bool nowG0 = modalG0;

        if (!moved) continue;
        if (wasG0 != nowG0) {
            if (seg.size() >= 2) m_travelPaths.push_back(seg);
            seg.clear();
        }
        x = nx; y = ny;
        if (nowG0) {
            const glm::vec2 c = pb.machineToContent(x, y);
            seg.addVertex(c.x, c.y, 0.f);
        }
    }
    if (seg.size() >= 2 && modalG0)
        m_travelPaths.push_back(seg);
}

} // namespace plotter::kit
