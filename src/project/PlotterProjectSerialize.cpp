#include "PlotterProjectSerialize.h"

#include "PlotterJsonHelpers.h"
#include "components/hierarchy_components.h"
#include "components/layer_components.h"

#include <unordered_map>

namespace plotterProject {
namespace {

using Json = ofJson;

ofJson colorToJson(const ofColor& c) { return plotterJson::colorToJson<Json>(c); }
ofColor colorFromJson(const ofJson& j) { return plotterJson::colorFromJson(j); }
ofJson pathToJson(const ofPath& path) { return plotterJson::pathToJson<Json>(path); }
ofPath pathFromJson(const ofJson& j) { return plotterJson::pathFromJson(j); }
ofJson settingsToJson(const plotter::settings_component& sc)
{
	return plotterJson::settingsToJson<Json>(sc);
}
void settingsFromJson(const ofJson& j, plotter::settings_component& sc)
{
	plotterJson::settingsFromJson(j, sc);
}

} // namespace

ofJson pipelineToJson(const plotproc::PlotPipeline& pipeline)
{
    ofJson stepsJson = ofJson::array();
    for (const auto& s : pipeline.steps) {
        stepsJson.push_back({
            { "id",      s.processorId },
            { "enabled", s.enabled },
            { "options", s.options },
        });
    }
    return { { "steps", std::move(stepsJson) } };
}

void pipelineFromJson(const ofJson& j, plotproc::PlotPipeline& pipeline)
{
    if (!j.contains("steps") || !j["steps"].is_array()) return;
    pipeline.steps.clear();
    for (const auto& s : j["steps"]) {
        plotproc::PipelineStep step;
        step.processorId = s.value("id", "");
        step.enabled     = s.value("enabled", true);
        if (s.contains("options")) step.options = s["options"];
        if (!step.processorId.empty()) pipeline.steps.push_back(step);
    }
}

ofJson layersToJson(entt::registry& reg, const PlotDoc& doc)
{
    ofJson layers = ofJson::array();
    std::unordered_map<entt::entity, int> entityToIndex;
    for (int i = 0; i < (int)doc.layerOrder.size(); ++i)
        entityToIndex[doc.layerOrder[i]] = i;

    for (entt::entity e : doc.layerOrder) {
        if (!reg.valid(e) || !reg.all_of<ecs::layer_component>(e)) continue;

        const auto& lc = reg.get<ecs::layer_component>(e);
        const auto& rel = reg.get<ecs::Relationship>(e);

        ofJson entry;
        entry["name"]    = lc.name;
        entry["visible"] = lc.visible;
        entry["locked"]  = lc.locked;
        entry["color"]   = colorToJson(lc.color);
        entry["index"]   = lc.index;

        int parentIndex = -1;
        if (rel.parent != entt::null) {
            auto it = entityToIndex.find(rel.parent);
            if (it != entityToIndex.end()) parentIndex = it->second;
        }
        entry["parentIndex"] = parentIndex;

        if (reg.all_of<plotter::settings_component>(e))
            entry["settings"] = settingsToJson(reg.get<plotter::settings_component>(e));

        if (reg.all_of<plotter::paths_component>(e)) {
            const auto& pc = reg.get<plotter::paths_component>(e);
            ofJson pathsJ = ofJson::array();
            for (const auto& p : pc.paths)
                pathsJ.push_back(pathToJson(p));
            entry["paths"] = std::move(pathsJ);

            if (!pc.pathColors.empty()) {
                ofJson colorsJ = ofJson::array();
                for (const auto& c : pc.pathColors)
                    colorsJ.push_back(colorToJson(c));
                entry["pathColors"] = std::move(colorsJ);
            }
            if (!pc.pathFilled.empty()) {
                ofJson filledJ = ofJson::array();
                for (auto f : pc.pathFilled) filledJ.push_back(f != 0);
                entry["pathFilled"] = std::move(filledJ);
            }
        }

        if (reg.all_of<plotter::fill_raster_component>(e)) {
            const auto& frc = reg.get<plotter::fill_raster_component>(e);
            entry["fillRasterEnabled"] = frc.enabled;
        }

        layers.push_back(std::move(entry));
    }
    return layers;
}

void clearDocumentLayers(entt::registry& reg, PlotDoc& doc)
{
    std::vector<entt::entity> toDestroy;
    for (auto e : doc.layerOrder)
        toDestroy.push_back(e);
    for (auto e : toDestroy) {
        if (reg.valid(e))
            reg.destroy(e);
    }
    doc.layerOrder.clear();
    doc.activeLayer = entt::null;
}

entt::entity loadSingleLayerFromJson(entt::registry& reg, PlotDoc& doc,
                                     const ofJson& lj,
                                     const std::vector<entt::entity>& created)
{
    if (!lj.is_object()) return entt::null;

    entt::entity parent = entt::null;
    const int parentIndex = lj.value("parentIndex", -1);
    if (parentIndex >= 0 && parentIndex < (int)created.size())
        parent = created[parentIndex];

    const std::string name = lj.value("name", std::string{});
    entt::entity e = doc.addLayer(name, parent);

    auto& lc = reg.get<ecs::layer_component>(e);
    lc.visible = lj.value("visible", true);
    lc.locked  = lj.value("locked", false);
    if (lj.contains("color"))
        lc.color = colorFromJson(lj["color"]);
    lc.index   = lj.value("index", lc.index);

    if (lj.contains("settings") && reg.all_of<plotter::settings_component>(e))
        settingsFromJson(lj["settings"], reg.get<plotter::settings_component>(e));

    if (lj.contains("paths") && reg.all_of<plotter::paths_component>(e)) {
        auto& pc = reg.get<plotter::paths_component>(e);
        pc.paths.clear();
        pc.pathColors.clear();
        pc.pathFilled.clear();
        for (const auto& pj : lj["paths"])
            pc.paths.push_back(pathFromJson(pj));
        if (lj.contains("pathColors") && lj["pathColors"].is_array()) {
            for (const auto& cj : lj["pathColors"])
                pc.pathColors.push_back(colorFromJson(cj));
        }
        if (lj.contains("pathFilled") && lj["pathFilled"].is_array()) {
            for (const auto& fj : lj["pathFilled"])
                pc.pathFilled.push_back(fj.get<bool>() ? 1 : 0);
        }
    }

    if (lj.contains("fillRasterEnabled") && reg.all_of<plotter::fill_raster_component>(e))
        reg.get<plotter::fill_raster_component>(e).enabled = lj["fillRasterEnabled"].get<bool>();

    return e;
}

void finalizeLayerLoad(PlotDoc& doc)
{
    doc.rebuildFlatPaths();
    doc.refreshStats();
}

void loadLayersFromJson(entt::registry& reg, PlotDoc& doc, const ofJson& layersJ)
{
    if (!layersJ.is_array() || layersJ.empty()) return;

    clearDocumentLayers(reg, doc);

    std::vector<entt::entity> created;
    created.reserve(layersJ.size());

    for (const auto& lj : layersJ) {
        created.push_back(loadSingleLayerFromJson(reg, doc, lj, created));
    }

    finalizeLayerLoad(doc);
}

} // namespace plotterProject
