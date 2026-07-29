#include "GcodeSenderSceneDraw.h"

#include "PlotterPreviewDraw.h"
#include "PlotterPreviewGrids.h"

#include <algorithm>
#include <vector>

namespace plotter::kit {

void drawGcodeSenderScene(PlotDoc& doc,
                          const plotter::PlotterZoneStore& zones,
                          entt::registry& reg,
                          const grbl::MachinePrefs& prefs,
                          const GcodeSenderSceneOpts& opts)
{
    const BedView bed = BedView::fromPrefs(prefs);
    const PreviewBounds pb = PreviewBounds::fromEnvelope(bed);
    const glm::vec2 paperOrigin = pb.machineToContent(prefs.bed.paperOriginX, prefs.bed.paperOriginY);

    if (opts.drawBedPaperZones) {
        const glm::vec2 paper = doc.getPaperSizeMM();
        const glm::vec2 envMin = pb.machineToContent(prefs.envelope.minX, prefs.envelope.minY);

        drawPrintPreviewBed(envMin.x, envMin.y,
                            prefs.envelope.spanX(), prefs.envelope.spanY(),
                            paperOrigin.x, paperOrigin.y,
                            paper.x, paper.y,
                            opts.bedColor, opts.paperColor);

        if (opts.showLeadBounds && opts.leadBounds.valid) {
            const glm::vec2 cMin = pb.machineToContent(opts.leadBounds.minX, opts.leadBounds.minY);
            const glm::vec2 sz = opts.leadBounds.size();
            drawPrintPreviewLeadBounds(cMin.x, cMin.y, sz.x, sz.y, opts.leadBoundsColor);
        }

        // Zone outlines only for non–draw-target zones (paper fill already shows Canvas).
        // Per-zone grids/margins are drawn in MainView2D overlayDraw via drawZoneGrids.
        ofPushStyle();
        ofNoFill();
        ofSetLineWidth(1.f);
        for (auto e : plotter::collectZoneEntities(reg)) {
            if (!reg.valid(e) || !reg.all_of<plotter::machine_zone_component>(e)) continue;
            const auto& z = reg.get<plotter::machine_zone_component>(e);
            if (z.w <= 0.f || z.h <= 0.f) continue;
            if (plotter::isDrawTargetZone(zones, z)) continue;
            const glm::vec2 org = pb.machineToContent(z.x, z.y);
            ofSetColor(ofFloatColor(z.color[0], z.color[1], z.color[2],
                                    std::max(0.35f, z.color[3])));
            ofDrawRectangle(org.x, org.y, z.w, z.h);
        }
        ofPopStyle();
    } else if (opts.showLeadBounds && opts.leadBounds.valid) {
        const glm::vec2 cMin = pb.machineToContent(opts.leadBounds.minX, opts.leadBounds.minY);
        const glm::vec2 sz = opts.leadBounds.size();
        drawPrintPreviewLeadBounds(cMin.x, cMin.y, sz.x, sz.y, opts.leadBoundsColor);
    }

    if (!opts.showPaths) return;
    if (!opts.showMark && !opts.showToolpath) return;

    const float markThicknessPx = std::max(1.f, opts.penStrokeWidthMm * opts.contentZoomPxPerMm);
    const float toolThicknessPx = opts.toolpathWidthPx;

    // Mark = thick pen-width footprint. Toolpath = thin machine centerline.
    // Either layer can draw alone; when both are on, Mark is drawn first and
    // Toolpath is a second pass on top (only when Mark is actually thicker).
    const bool drawMark     = opts.showMark;
    const bool drawToolpath = opts.showToolpath
        && (!drawMark || markThicknessPx > toolThicknessPx + 0.5f);
    const float primaryThicknessPx = drawMark ? markThicknessPx : toolThicknessPx;

    auto drawToolpathPass = [&](const std::vector<ofPolyline>& paths) {
        if (!drawToolpath || !drawMark) return; // sole-toolpath uses primary pass below
        const ofColor col(opts.toolpathColor);
        if (opts.centerlineMeshCache) {
            opts.centerlineMeshCache->draw(paths, col, opts.maxPathIndex,
                                           opts.contentZoomPxPerMm, toolThicknessPx);
        } else {
            const std::vector<ofColor> cols(paths.size(), col);
            drawPrintPreviewPaths(paths, cols, opts.maxPathIndex,
                                  opts.contentZoomPxPerMm, toolThicknessPx);
        }
    };

    // Prefer G-code preview geometry (content mm) so pen-down + G0 travels share space.
    if (opts.contentDrawPaths && !opts.contentDrawPaths->empty()) {
        // Travels are part of the machine toolpath, not the mark footprint.
        if (opts.showTravelPaths && opts.showToolpath && opts.contentTravelPaths
            && !opts.contentTravelPaths->empty()) {
            ofColor tcol(opts.travelColor);
            if (tcol.a == 0) tcol.a = 140;
            // Batched (and VBO-cached when a cache is provided) — per-polyline
            // draw() calls dominate frame time on jobs with many travels.
            if (opts.travelMeshCache) {
                opts.travelMeshCache->draw(*opts.contentTravelPaths, tcol, -1,
                                           opts.contentZoomPxPerMm, 1.25f);
            } else {
                const ofFloatColor col(tcol.r / 255.f, tcol.g / 255.f,
                                       tcol.b / 255.f, tcol.a / 255.f);
                ofPushStyle();
                ofSetLineWidth(1.25f);
                ofMesh mesh;
                mesh.setMode(OF_PRIMITIVE_LINES);
                for (const auto& p : *opts.contentTravelPaths) {
                    const auto& verts = p.getVertices();
                    for (size_t v = 0; v + 1 < verts.size(); ++v) {
                        mesh.addVertex(verts[v]);
                        mesh.addVertex(verts[v + 1]);
                        mesh.addColor(col);
                        mesh.addColor(col);
                    }
                }
                if (mesh.getNumVertices() > 0)
                    mesh.draw();
                ofPopStyle();
            }
        }

        if (drawMark || (opts.showToolpath && !drawMark)) {
            // Sole toolpath uses toolpathColor; mark uses pathColor / per-path colors.
            if (!drawMark && opts.showToolpath) {
                const ofColor col(opts.toolpathColor);
                if (opts.centerlineMeshCache) {
                    opts.centerlineMeshCache->draw(*opts.contentDrawPaths, col, opts.maxPathIndex,
                                                   opts.contentZoomPxPerMm, primaryThicknessPx);
                } else {
                    const std::vector<ofColor> cols(opts.contentDrawPaths->size(), col);
                    drawPrintPreviewPaths(*opts.contentDrawPaths, cols, opts.maxPathIndex,
                                          opts.contentZoomPxPerMm, primaryThicknessPx);
                }
            } else if (opts.drawMeshCache) {
                if (!opts.overrideColors && opts.contentDrawColors) {
                    opts.drawMeshCache->draw(*opts.contentDrawPaths, *opts.contentDrawColors,
                                             opts.maxPathIndex, opts.contentZoomPxPerMm,
                                             primaryThicknessPx);
                } else {
                    const ofColor col = opts.overrideColors ? ofColor(opts.pathColor)
                                                            : ofColor(0, 0, 0);
                    opts.drawMeshCache->draw(*opts.contentDrawPaths, col, opts.maxPathIndex,
                                             opts.contentZoomPxPerMm, primaryThicknessPx);
                }
            } else if (opts.overrideColors) {
                const ofColor col(opts.pathColor);
                const std::vector<ofColor> cols(opts.contentDrawPaths->size(), col);
                drawPrintPreviewPaths(*opts.contentDrawPaths, cols, opts.maxPathIndex,
                                      opts.contentZoomPxPerMm, primaryThicknessPx);
            } else if (opts.contentDrawColors) {
                drawPrintPreviewPaths(*opts.contentDrawPaths, *opts.contentDrawColors,
                                      opts.maxPathIndex, opts.contentZoomPxPerMm, primaryThicknessPx);
            } else {
                const std::vector<ofColor> cols(opts.contentDrawPaths->size(), ofColor(0, 0, 0));
                drawPrintPreviewPaths(*opts.contentDrawPaths, cols, opts.maxPathIndex,
                                      opts.contentZoomPxPerMm, primaryThicknessPx);
            }
        }
        drawToolpathPass(*opts.contentDrawPaths);
        return;
    }

    // Fallback: PlotDoc paths are paper-local (placeSvgDocument).
    ofPushMatrix();
    ofTranslate(paperOrigin.x, paperOrigin.y);
    const auto& paths = doc.getPaths();
    if (drawMark || (opts.showToolpath && !drawMark)) {
        if (!drawMark && opts.showToolpath) {
            const ofColor col(opts.toolpathColor);
            const std::vector<ofColor> cols(paths.size(), col);
            drawPrintPreviewPaths(paths, cols, opts.maxPathIndex,
                                  opts.contentZoomPxPerMm, primaryThicknessPx);
        } else if (opts.overrideColors) {
            const ofColor col(opts.pathColor);
            const std::vector<ofColor> cols(paths.size(), col);
            drawPrintPreviewPaths(paths, cols, opts.maxPathIndex,
                                  opts.contentZoomPxPerMm, primaryThicknessPx);
        } else {
            drawPrintPreviewPaths(paths, doc.getFlatPathColors(), opts.maxPathIndex,
                                  opts.contentZoomPxPerMm, primaryThicknessPx);
        }
    }
    drawToolpathPass(paths);
    ofPopMatrix();
}

} // namespace plotter::kit
