#include "kit/Toolpath3DView.h"

#include "LeadBounds.h"
#include "PlotterExporter.h"
#include "PlotterGCodeInjector.h"
#include "PlotterPreviewDraw.h"
#include "PlotterPreviewGrids.h"
#include "SmoothPenMotion.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace plotter::kit {

void Toolpath3DView::setInputs(Toolpath3DInputs inputs)
{
    m_in = std::move(inputs);
}

bool Toolpath3DView::inputsReady() const
{
    return m_in.plotDoc && m_in.zones && m_in.registry && m_in.machinePrefs
        && m_in.previewPanel && m_in.landingPads && m_in.buildPreviewBounds
        && m_in.toolpathPreviewRev
        && m_in.showLeadBounds && m_in.landingColor && m_in.leadBoundsColor;
}

void Toolpath3DView::setup()
{
    if (m_viewport) return;

    auto& rt = ofkitty::runtime();
    rt.setViewportRenderer([this] { drawScene(); });
    m_viewport = rt.addViewportWindow(kWindowTitle);
    if (!m_viewport) return;

    m_viewport->showGizmo    = true;
    m_viewport->editModeOnly = false;
    m_viewport->renderOnDemand = true;
    m_viewport->menuBarDraw = [this] { drawDisplayMenu(); };
    if (auto* win = rt.findWindow(kWindowTitle)) {
        win->editModeOnly = false;
        // Visibility comes from appPrefs / imgui.ini — do not force closed here
        // or fresh checkouts lose the shipped dock tab for Toolpath 3D.
    }
    frame();
}

void Toolpath3DView::update()
{
    updateScene();
}

void Toolpath3DView::frame()
{
    if (!m_viewport || !inputsReady()) return;

    const glm::vec2 sz = m_in.buildPreviewBounds().size();
    const PenSettings& pen = m_in.plotDoc->pen;
    const float zSpan = std::abs(pen.penUpZ - pen.penDownZ)
                      * std::max(1.f, m_zExagg);
    m_viewport->target    = { sz.x * 0.5f, zSpan * 0.5f, sz.y * 0.5f };
    m_viewport->azimuth   = 30.f;
    m_viewport->elevation = 38.f;
    m_viewport->distance  = std::clamp(std::max({sz.x, sz.y, 60.f}) * 1.4f,
                                       10.f, 5000.f);
}

void Toolpath3DView::updateScene()
{
    if (!m_viewport || !inputsReady()) return;
    auto* win = ofkitty::runtime().findWindow(kWindowTitle);
    if (!win || !win->visible) return;

    // Mix only fields that actually affect rebuildMeshes().
    std::size_t h = 0;
    auto mix = [&h](std::size_t v) {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    };
    auto mixF = [&](float f) { mix(std::hash<float>{}(f)); };
    auto mixColor = [&](const ofFloatColor& c) {
        mixF(c.r); mixF(c.g); mixF(c.b); mixF(c.a);
    };

    const PenSettings& pen = m_in.plotDoc->pen;
    mixF(pen.penUpZ);
    mixF(pen.penDownZ);
    mixF(pen.penWidth);
    mix(pen.smoothApproach ? 1u : 0u);
    mixF(pen.approachMm);
    mixF(pen.retractMm);
    mixF(pen.leadOverlapMm);

    const auto& prefs = *m_in.machinePrefs;
    mixF(prefs.bed.paperOriginX);
    mixF(prefs.bed.paperOriginY);
    mixF(prefs.envelope.minX);
    mixF(prefs.envelope.minY);
    mixF(prefs.envelope.maxX);
    mixF(prefs.envelope.maxY);

    auto& panel = *m_in.previewPanel;
    mix(*m_in.toolpathPreviewRev);
    mix(panel.drawPaths().size());
    mix(panel.travelPaths().size());
    mix(panel.overrideColors ? 1u : 2u);
    mix((m_showMarks   ? 1u : 0u)
        | (m_showTravels ? 2u : 0u)
        | (m_showLeads   ? 4u : 0u)
        | (m_showMarkers ? 8u : 0u)
        | (m_showRibbon  ? 16u : 0u)
        | (m_showZones   ? 32u : 0u));
    if (m_showZones) {
        mix(std::hash<std::string>{}(m_in.zones->drawTargetZoneId));
        for (auto e : plotter::collectZoneEntities(*m_in.registry)) {
            if (!m_in.registry->valid(e)
                || !m_in.registry->all_of<plotter::machine_zone_component>(e))
                continue;
            const auto& z = m_in.registry->get<plotter::machine_zone_component>(e);
            mix(std::hash<std::string>{}(z.zoneId));
            mix(std::hash<std::string>{}(z.snippetResourceName));
            mixF(z.x); mixF(z.y); mixF(z.w); mixF(z.h);
            mixF(z.color[0]); mixF(z.color[1]); mixF(z.color[2]); mixF(z.color[3]);
            mix(z.positions.size());
            for (const auto& p : z.positions) { mixF(p.x); mixF(p.y); }
        }
    }
    mixColor(ofFloatColor(panel.drawColor));
    mixColor(ofFloatColor(panel.travelColor));
    mixColor(ofFloatColor(panel.envelopeColor));
    mixColor(ofFloatColor(panel.paperColor));
    mixColor(*m_in.landingColor);
    mixF(m_zExagg);
    mix(m_in.landingPads->landings.size());
    mix(m_in.landingPads->liftoffs.size());

    if (h == m_sceneHash) return;
    m_sceneHash = h;
    rebuildMeshes();
    m_viewport->dirty = true;
}

void Toolpath3DView::rebuildMeshes()
{
    const plotter::PreviewBounds pb = m_in.buildPreviewBounds();
    const PenSettings& pen = m_in.plotDoc->pen;
    const float zSign = (pen.penUpZ >= pen.penDownZ) ? 1.f : -1.f;
    const float exagg = std::max(1.f, m_zExagg);
    auto height = [&](float machineZ) {
        return (machineZ - pen.penDownZ) * zSign * exagg;
    };

    m_ribbon.clear();  m_ribbon.setMode(OF_PRIMITIVE_TRIANGLES);
    m_draws.clear();   m_draws.setMode(OF_PRIMITIVE_LINES);
    m_travels.clear(); m_travels.setMode(OF_PRIMITIVE_LINES);
    m_leads.clear();   m_leads.setMode(OF_PRIMITIVE_LINES);
    m_markers.clear(); m_markers.setMode(OF_PRIMITIVE_TRIANGLES);
    m_zones.clear();     m_zones.setMode(OF_PRIMITIVE_TRIANGLES);
    m_zoneLines.clear(); m_zoneLines.setMode(OF_PRIMITIVE_LINES);

    auto& panel = *m_in.previewPanel;
    auto& registry = *m_in.registry;
    auto& zones = *m_in.zones;
    auto& landingPads = *m_in.landingPads;

    if (m_showZones) {
        const float yZone = 0.05f;
        const ofMesh ico = ofMesh::icosphere(0.8f, 1);
        const auto& iv = ico.getVertices();
        const auto& ii = ico.getIndices();
        for (auto e : plotter::collectZoneEntities(registry)) {
            if (!registry.valid(e)
                || !registry.all_of<plotter::machine_zone_component>(e))
                continue;
            const auto& z = registry.get<plotter::machine_zone_component>(e);
            if (z.w <= 0.f || z.h <= 0.f) continue;
            if (plotter::isDrawTargetZone(zones, z)) continue;

            const glm::vec2 org = pb.machineToContent(z.x, z.y);
            const glm::vec3 a {org.x,       yZone, org.y};
            const glm::vec3 b {org.x + z.w, yZone, org.y};
            const glm::vec3 c {org.x + z.w, yZone, org.y + z.h};
            const glm::vec3 d {org.x,       yZone, org.y + z.h};

            const ofFloatColor base(z.color[0], z.color[1], z.color[2],
                                    std::max(0.35f, z.color[3]));
            ofFloatColor fill = base;
            fill.a *= 0.35f;
            for (const auto& v : {a, b, c, a, c, d}) {
                m_zones.addVertex(v);
                m_zones.addColor(fill);
            }
            for (const auto* v : {&a, &b, &b, &c, &c, &d, &d, &a}) {
                m_zoneLines.addVertex(*v);
                m_zoneLines.addColor(base);
            }

            auto addPositionMarker = [&](float mx, float my) {
                const glm::vec2 p = pb.machineToContent(mx, my);
                const glm::vec3 centre {p.x, yZone, p.y};
                for (const auto idx : ii) {
                    m_zones.addVertex(iv[idx] + centre);
                    m_zones.addColor(base);
                }
            };
            if (z.positions.empty()) {
                addPositionMarker(z.x + z.w * 0.5f, z.y + z.h * 0.5f);
            } else {
                for (const auto& p : z.positions)
                    addPositionMarker(p.x, p.y);
            }

            const float span = pen.penUpZ - pen.penDownZ;
            const float band = std::clamp(std::abs(span) * 0.1f, 0.05f, 0.5f);
            auto isDown = [&](float mz) {
                return span >= 0.f ? (mz <= pen.penDownZ + band)
                                   : (mz >= pen.penDownZ - band);
            };
            for (const auto& sp : plotter::previewZoneSnippetPaths(z, registry, pen)) {
                const auto& verts = sp.getVertices();
                for (size_t v = 0; v + 1 < verts.size(); ++v) {
                    auto colAt = [&](float mz) {
                        ofFloatColor c = base;
                        c.a = isDown(mz) ? 0.95f : base.a * 0.45f;
                        return c;
                    };
                    const glm::vec2 va = pb.machineToContent(verts[v].x, verts[v].y);
                    const glm::vec2 vb = pb.machineToContent(verts[v + 1].x, verts[v + 1].y);
                    m_zoneLines.addVertex({va.x, height(verts[v].z),     va.y});
                    m_zoneLines.addVertex({vb.x, height(verts[v + 1].z), vb.y});
                    m_zoneLines.addColor(colAt(verts[v].z));
                    m_zoneLines.addColor(colAt(verts[v + 1].z));
                }
            }
        }
    }

    if (panel.hasGeometry() && m_showRibbon && pen.penWidth > 0.f) {
        const auto& paths  = panel.drawPaths();
        const auto& colors = panel.drawColors();
        const float halfW  = pen.penWidth * 0.5f;
        ofMesh flat;
        flat.setMode(OF_PRIMITIVE_TRIANGLES);
        for (size_t i = 0; i < paths.size(); ++i) {
            const ofFloatColor col = panel.overrideColors
                ? ofFloatColor(panel.drawColor)
                : (i < colors.size() ? ofFloatColor(colors[i])
                                     : ofFloatColor(0.f, 0.f, 0.f, 1.f));
            plotter::appendThickPolylineMesh(flat, paths[i], halfW, col, 12);
        }
        const auto& fv = flat.getVertices();
        const auto& fc = flat.getColors();
        for (size_t v = 0; v < fv.size(); ++v) {
            m_ribbon.addVertex({fv[v].x, 0.f, fv[v].y});
            m_ribbon.addColor(fc[v]);
        }
    }

    if (panel.hasGeometry() && m_showMarks) {
        const auto& paths  = panel.drawPaths();
        const auto& colors = panel.drawColors();
        for (size_t i = 0; i < paths.size(); ++i) {
            const ofFloatColor col = panel.overrideColors
                ? ofFloatColor(panel.drawColor)
                : (i < colors.size() ? ofFloatColor(colors[i])
                                     : ofFloatColor(0.f, 0.f, 0.f, 1.f));
            const auto& verts = paths[i].getVertices();
            for (size_t v = 0; v + 1 < verts.size(); ++v) {
                m_draws.addVertex({verts[v].x,     0.f, verts[v].y});
                m_draws.addVertex({verts[v + 1].x, 0.f, verts[v + 1].y});
                m_draws.addColor(col);
                m_draws.addColor(col);
            }
        }
    }

    if (panel.hasGeometry() && m_showTravels) {
        ofFloatColor tcol(panel.travelColor);
        if (tcol.a <= 0.f) tcol.a = 0.55f;
        tcol.a *= 0.8f;
        for (const auto& p : panel.travelPaths()) {
            const auto& verts = p.getVertices();
            for (size_t v = 0; v + 1 < verts.size(); ++v) {
                m_travels.addVertex(
                    {verts[v].x, height(verts[v].z), verts[v].y});
                m_travels.addVertex(
                    {verts[v + 1].x, height(verts[v + 1].z), verts[v + 1].y});
                m_travels.addColor(tcol);
                m_travels.addColor(tcol);
            }
        }
    }

    if (pen.smoothApproach && (m_showLeads || m_showMarkers)
        && (!landingPads.landings.empty() || !landingPads.liftoffs.empty())) {
        const ofFloatColor warm(*m_in.landingColor);
        ofFloatColor cool(0.35f, 0.6f, 1.f, m_in.landingColor->a);
        const float zLo = pen.penDownZ;
        const float zSpan = pen.penUpZ - zLo;
        auto zColor = [&](float z, float alphaScale) {
            float t = (std::abs(zSpan) > 1e-4f) ? (z - zLo) / zSpan : 0.f;
            t = std::clamp(t, 0.f, 1.f);
            ofFloatColor c = warm.getLerped(cool, t);
            c.a = warm.a * alphaScale;
            return c;
        };
        auto addLead = [&](const std::vector<glm::vec3>& path, float alphaScale) {
            for (size_t v = 0; v + 1 < path.size(); ++v) {
                const glm::vec2 a = pb.machineToContent(path[v].x, path[v].y);
                const glm::vec2 b = pb.machineToContent(path[v + 1].x, path[v + 1].y);
                m_leads.addVertex({a.x, height(path[v].z),     a.y});
                m_leads.addVertex({b.x, height(path[v + 1].z), b.y});
                m_leads.addColor(zColor(path[v].z, alphaScale));
                m_leads.addColor(zColor(path[v + 1].z, alphaScale));
            }
        };

        if (m_showLeads) {
            for (const auto& e : landingPads.landings) {
                const ofVec2f from = e.hasArrivalFrom
                    ? e.arrivalFrom
                    : e.point - e.dir * std::max(0.f, pen.approachMm);
                const auto* path = e.strokePath.empty() ? nullptr : &e.strokePath;
                addLead(plotter::previewLeadInPath3D(pen, from, e.point, e.dir, path), 1.f);
            }
            for (const auto& e : landingPads.liftoffs) {
                const auto* path = e.strokePath.empty() ? nullptr : &e.strokePath;
                addLead(plotter::previewLeadOutPath3D(pen, e.point, e.dir, path), 0.75f);
            }
        }

        if (m_showMarkers) {
            const ofMesh ico = ofMesh::icosphere(0.6f, 1);
            const auto& iv = ico.getVertices();
            const auto& ii = ico.getIndices();
            for (const auto& e : landingPads.landings) {
                const glm::vec2 c = pb.machineToContent(e.point.x, e.point.y);
                const glm::vec3 centre {c.x, 0.f, c.y};
                for (const auto idx : ii) {
                    m_markers.addVertex(iv[idx] + centre);
                    m_markers.addColor(warm);
                }
            }
        }
    }
}

void Toolpath3DView::drawDisplayMenu()
{
    if (!inputsReady()) return;
    if (!ImGui::BeginMenu("Display")) return;

    auto& panel = *m_in.previewPanel;

    ImGui::SeparatorText("Layers");
    ImGui::MenuItem("Mark Ribbon (pen width)", nullptr, &m_showRibbon);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The painted mark as the brush leaves it: a round-join ribbon\n"
                          "at pen width on the paper plane (Controls → Pen → Pen Width).");
    ImGui::MenuItem("Toolpath Centerline", nullptr, &m_showMarks);
    ImGui::MenuItem("Travels / Retracts", nullptr, &m_showTravels);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The airborne G-code as emitted: rapids plus the smooth-lead\n"
                          "climbs and descents, drawn at their true (exaggerated) height.");
    ImGui::MenuItem("Landing Overlay", nullptr, &m_showLeads);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Analytic lead-in / lift-off curves from the LIVE pen settings —\n"
                          "updates while dragging sliders, before the G-code re-prepares.\n"
                          "Needs 'Smooth sine approach' enabled in Controls → Pen.");
    ImGui::MenuItem("Touchdown Markers", nullptr, &m_showMarkers);
    ImGui::MenuItem("Zones & Positions", nullptr, &m_showZones);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Machine zones (paint dishes, wash stations …) as translucent\n"
                          "rectangles with their named detour positions as spheres.\n"
                          "Colours come from each zone (Zones panel).");

    ImGui::SeparatorText("Colours");
    const ImGuiColorEditFlags cf = ImGuiColorEditFlags_NoInputs
                                 | ImGuiColorEditFlags_AlphaBar;
    ImGui::Checkbox("Override path colours", &panel.overrideColors);
    if (panel.overrideColors)
        ImGui::ColorEdit4("Marks##tp3dCol",    (float*)&panel.drawColor, cf);
    ImGui::ColorEdit4("Travels##tp3dCol",  (float*)&panel.travelColor, cf);
    ImGui::ColorEdit4("Landings##tp3dCol", (float*)m_in.landingColor, cf);
    ImGui::ColorEdit3("Bed##tp3dCol",      (float*)&panel.envelopeColor, cf);
    ImGui::ColorEdit3("Paper##tp3dCol",    (float*)&panel.paperColor, cf);
    ImGui::TextDisabled("Shared with the 2D preview colours.");

    ImGui::SeparatorText("Scale");
    ImGui::SetNextItemWidth(140.f);
    if (ImGui::DragFloat("Z exaggeration##tp3d", &m_zExagg,
                         0.05f, 1.f, 50.f, "%.1f x"))
        m_zExagg = std::clamp(m_zExagg, 1.f, 50.f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Multiplies machine Z — pen heights are tiny next to\n"
                          "the bed, so exaggerate for readability.");

    ImGui::Separator();
    if (ImGui::MenuItem("Frame View"))
        frame();

    ImGui::EndMenu();
}

void Toolpath3DView::drawViewMenuItems()
{
    ImGui::Separator();
    auto* vpWin = ofkitty::runtime().findWindow(kWindowTitle);
    bool open3D = vpWin && vpWin->visible;
    if (ImGui::MenuItem("Toolpath 3D", nullptr, &open3D) && vpWin) {
        vpWin->visible = open3D;
        if (open3D && !m_framed) {
            frame();
            m_framed = true;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Slicer-style orbit view: toolpath at pen-down Z, travels at\n"
                          "pen-up Z, smooth-approach landings coloured by height.\n"
                          "Drag: orbit   Shift/middle-drag: pan   Right-drag / scroll: zoom");
    if (ImGui::MenuItem("Frame Toolpath 3D", nullptr, false, m_viewport != nullptr))
        frame();
    ImGui::SetNextItemWidth(140.f);
    if (ImGui::DragFloat("Z exaggeration", &m_zExagg,
                         0.05f, 1.f, 50.f, "%.1f x"))
        m_zExagg = std::clamp(m_zExagg, 1.f, 50.f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Multiplies machine Z in the Toolpath 3D view — pen heights\n"
                          "are tiny next to the bed, so exaggerate for readability.");
}

void Toolpath3DView::drawScene()
{
    if (!inputsReady()) return;

    const plotter::PreviewBounds pb = m_in.buildPreviewBounds();
    auto& panel = *m_in.previewPanel;
    auto& machinePrefs = *m_in.machinePrefs;

    ofPushStyle();
    ofDisableDepthTest();

    auto drawGroundRect = [&](float cx, float cy, float w, float h,
                              float atHeight, const ofFloatColor& col) {
        ofPushMatrix();
        ofTranslate(0.f, atHeight, 0.f);
        ofRotateDeg(90.f, 1.f, 0.f, 0.f);
        ofSetColor(col);
        ofDrawRectangle(cx, cy, w, h);
        ofPopMatrix();
    };

    const auto& envp = machinePrefs.envelope;
    const glm::vec2 envMin = pb.machineToContent(envp.minX, envp.minY);
    ofFloatColor bedCol(panel.envelopeColor.r, panel.envelopeColor.g,
                        panel.envelopeColor.b, 1.f);
    drawGroundRect(envMin.x, envMin.y, envp.spanX(), envp.spanY(), -0.4f, bedCol);

    const glm::vec2 paperOrigin = pb.machineToContent(machinePrefs.bed.paperOriginX,
                                                      machinePrefs.bed.paperOriginY);
    const glm::vec2 paper = m_in.plotDoc->getPaperSizeMM();
    ofFloatColor paperCol(panel.paperColor.r, panel.paperColor.g,
                          panel.paperColor.b, 1.f);
    drawGroundRect(paperOrigin.x, paperOrigin.y, paper.x, paper.y, -0.2f, paperCol);

    if (*m_in.showLeadBounds) {
        const auto bed = plotter::BedView::fromPrefs(machinePrefs);
        const plotter::LeadBounds lb = plotter::computeLeadBoundsFromPaperPaths(
            m_in.plotDoc->getPaths(), bed, m_in.plotDoc->pen);
        if (lb.valid) {
            const glm::vec2 cMin = pb.machineToContent(lb.minX, lb.minY);
            const glm::vec2 sz = lb.size();
            ofPushStyle();
            ofNoFill();
            ofSetColor(*m_in.leadBoundsColor);
            ofSetLineWidth(2.f);
            ofPushMatrix();
            ofTranslate(0.f, -0.1f, 0.f);
            ofRotateDeg(90.f, 1.f, 0.f, 0.f);
            ofDrawRectangle(cMin.x, cMin.y, sz.x, sz.y);
            ofPopMatrix();
            ofPopStyle();
        }
    }

    ofSetColor(255);
    if (m_zones.getNumVertices()     > 0) m_zones.draw();
    if (m_zoneLines.getNumVertices() > 0) m_zoneLines.draw();
    if (m_ribbon.getNumVertices()  > 0) m_ribbon.draw();
    if (m_draws.getNumVertices()   > 0) m_draws.draw();
    if (m_travels.getNumVertices() > 0) m_travels.draw();
    if (m_leads.getNumVertices()   > 0) m_leads.draw();
    if (m_markers.getNumVertices() > 0) m_markers.draw();

    ofEnableDepthTest();
    ofPopStyle();
}

} // namespace plotter::kit
