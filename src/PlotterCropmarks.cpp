#include "PlotterCropmarks.h"

#include "GeneratorLayerHelper.h"
#include "ofxPlotGenerators.h"

#include <algorithm>
#include <cmath>

namespace plotter::kit {

bool zoneMarginBox(const machine_zone_component& zone,
                   const ZoneMarginsMM& margins,
                   float& x0, float& y0, float& x1, float& y1)
{
    const float innerW = zone.w - margins.left - margins.right;
    const float innerH = zone.h - margins.top - margins.bottom;
    if (innerW <= 0.f || innerH <= 0.f) return false;
    x0 = margins.left;
    y0 = margins.top;
    x1 = zone.w - margins.right;
    y1 = zone.h - margins.bottom;
    return x1 > x0 && y1 > y0;
}

namespace {

glm::vec2 zoneLocalToPaper(float lx, float ly,
                           const machine_zone_component& zone,
                           const BedView* bed,
                           bool zoneIsDrawTarget)
{
    // Draw-target zone-local == paper-local (same convention as generators /
    // SVG placement). Do NOT run Invert ±X/±Y here — export's paperToMachine
    // applies axis signs. Mapping through machineToPaper would flip Y and park
    // marks under the paper origin ("only at the bottom").
    if (zoneIsDrawTarget || !bed)
        return { lx, ly };

    const float mx = zone.x + lx;
    const float my = zone.y + ly;
    return bed->machineToPaper(mx, my);
}

ofPath transformPathToPaper(const ofPath& src,
                            const machine_zone_component& zone,
                            const BedView* bed,
                            bool zoneIsDrawTarget,
                            float thicknessMm,
                            const ofColor& color)
{
    ofPath out;
    out.setMode(ofPath::COMMANDS);
    out.setFilled(false);
    out.setStrokeWidth(std::max(0.05f, thicknessMm));
    out.setStrokeColor(color);
    out.setColor(color);

    // Use commands — getOutline() often returns empty for unfilled 2-point
    // stroke paths (cropmark L arms), which dropped top/side marks.
    const auto& cmds = src.getCommands();
    if (!cmds.empty()) {
        for (const auto& cmd : cmds) {
            switch (cmd.type) {
                case ofPath::Command::moveTo: {
                    const glm::vec2 p = zoneLocalToPaper(
                        cmd.to.x, cmd.to.y, zone, bed, zoneIsDrawTarget);
                    out.moveTo(p.x, p.y);
                    break;
                }
                case ofPath::Command::lineTo: {
                    const glm::vec2 p = zoneLocalToPaper(
                        cmd.to.x, cmd.to.y, zone, bed, zoneIsDrawTarget);
                    out.lineTo(p.x, p.y);
                    break;
                }
                default:
                    break;
            }
        }
        return out;
    }

    // Fallback: tessellated outlines if commands were stripped.
    ofPath tess = src;
    tess.setStrokeWidth(std::max(0.05f, thicknessMm));
    tess.setCurveResolution(8);
    (void)tess.getOutline();
    for (const ofPolyline& pl : tess.getOutline()) {
        if (pl.size() < 2) continue;
        for (size_t i = 0; i < pl.size(); ++i) {
            const auto& v = pl.getVertices()[i];
            const glm::vec2 p = zoneLocalToPaper(
                v.x, v.y, zone, bed, zoneIsDrawTarget);
            if (i == 0)
                out.moveTo(p.x, p.y);
            else
                out.lineTo(p.x, p.y);
        }
    }
    return out;
}

void pinLayerFirst(PlotDoc& doc, entt::entity layerEnt)
{
    if (layerEnt == entt::null) return;
    auto& order = doc.layerOrder;
    auto it = std::find(order.begin(), order.end(), layerEnt);
    if (it == order.end() || it == order.begin()) return;
    order.erase(it);
    order.insert(order.begin(), layerEnt);
    auto& reg = doc.getRegistry();
    for (int i = 0; i < (int)order.size(); ++i) {
        if (reg.valid(order[i]) && reg.all_of<ecs::layer_component>(order[i]))
            reg.get<ecs::layer_component>(order[i]).index = i;
    }
}

} // namespace

entt::entity generateCropmarksForZone(PlotDoc& doc,
                                      PlotterZoneStore& zones,
                                      const grbl::MachinePrefs* prefs,
                                      entt::entity zoneEntity,
                                      const CropmarksGenerateOpts& opts,
                                      std::string* errMsg)
{
    auto& reg = doc.getRegistry();
    if (zoneEntity == entt::null || !reg.valid(zoneEntity)
        || !reg.all_of<machine_zone_component>(zoneEntity)) {
        if (errMsg) *errMsg = "Invalid zone.";
        return entt::null;
    }

    const auto& zone = reg.get<machine_zone_component>(zoneEntity);

    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
    if (!zoneMarginBox(zone, opts.margins, x0, y0, x1, y1)) {
        if (errMsg) *errMsg = "Zone margins leave an empty rectangle.";
        return entt::null;
    }

    plotter::paths_component temp;
    plotgen::GeneratorPathContext ctx;
    ctx.paths = &temp;
    ctx.x0 = x0;
    ctx.y0 = y0;
    ctx.x1 = x1;
    ctx.y1 = y1;

    const ofJson settings = {
        { "length", std::max(1.f, opts.lengthMm) },
        { "inset", opts.insetMm },
    };
    plotgen::GeneratorRegistry::instance().generatePaths({ "path", "cropmarks" }, ctx, settings);
    if (temp.paths.empty()) {
        if (errMsg) *errMsg = "Cropmarks generator produced no paths (check inset / size).";
        return entt::null;
    }

    std::string layerName = opts.newLayerName;
    if (layerName.empty())
        layerName = "Cropmarks — " + (zone.name.empty() ? zone.zoneId : zone.name);

    entt::entity layerEnt = opts.targetLayer;
    if (layerEnt == entt::null || !reg.valid(layerEnt)
        || !reg.all_of<ecs::layer_component>(layerEnt)) {
        layerEnt = plotgen::findLayerByName(doc, layerName);
    }
    if (layerEnt == entt::null || !reg.valid(layerEnt))
        layerEnt = doc.addLayer(layerName);
    else if (reg.all_of<ecs::layer_component>(layerEnt))
        reg.get<ecs::layer_component>(layerEnt).name = layerName;

    plotgen::clearPathsOnLayer(reg, layerEnt);
    doc.activeLayer = layerEnt;

    auto& lc = reg.get<ecs::layer_component>(layerEnt);
    lc.color = opts.color;

    auto& pc = reg.get_or_emplace<plotter::paths_component>(layerEnt);

    BedView bedStorage;
    const BedView* bed = nullptr;
    if (prefs) {
        bedStorage = BedView::fromPrefs(*prefs);
        bed = &bedStorage;
    }
    const bool zoneIsDrawTarget = isDrawTargetZone(zones, zone);

    pc.paths.reserve(temp.paths.size());
    pc.pathColors.reserve(temp.paths.size());
    pc.pathFilled.reserve(temp.paths.size());
    for (const ofPath& src : temp.paths) {
        ofPath mapped = transformPathToPaper(
            src, zone, bed, zoneIsDrawTarget, opts.thicknessMm, opts.color);
        if (mapped.getCommands().empty())
            continue;
        pc.paths.push_back(std::move(mapped));
        pc.pathColors.push_back(opts.color);
        pc.pathFilled.push_back(0);
    }
    if (pc.paths.empty()) {
        if (errMsg) *errMsg = "Cropmarks transform produced no paths.";
        return entt::null;
    }

    if (opts.pinFirst)
        pinLayerFirst(doc, layerEnt);

    doc.markCanvasDirty();
    return layerEnt;
}

} // namespace plotter::kit
