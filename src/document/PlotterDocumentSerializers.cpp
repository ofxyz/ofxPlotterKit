#include "PlotterDocumentSerializers.h"

#include "ComponentSerializerRegistry.h"
#include "CoreSerializers.h"
#include "PlotterJsonHelpers.h"
#include "SerializerHelpers.h"

#include "PenSettings.h"
#include "PlotDocComponent.h"
#include "PlotterZones.h"

#include "components/base_components.h"

namespace ofxDocumentKit {

namespace {

using Json = ordered_json;

ordered_json ofJsonToOrdered(const ofJson& j)
{
	return ordered_json::parse(j.dump());
}

ofJson orderedToOfJson(const ordered_json& j)
{
	return ofJson::parse(j.dump());
}

ordered_json pathToJson(const ofPath& path) { return plotterJson::pathToJson<Json>(path); }
ofPath pathFromJson(const ordered_json& j) { return plotterJson::pathFromJson(j); }
ordered_json settingsToJson(const plotter::settings_component& sc)
{
	return plotterJson::settingsToJson<Json>(sc);
}
void settingsFromJson(const ordered_json& j, plotter::settings_component& sc)
{
	plotterJson::settingsFromJson(j, sc);
}

} // namespace

bool serializePlotDoc(entt::registry& reg, entt::entity e, ordered_json& j)
{
    const auto& c = reg.get<plotter::plot_doc_component>(e);
    j = {
        { "name", c.name },
        { "paperSizeMM", vec2ToJson(c.paperSizeMM) },
        { "sourcePath", toRelativePath(c.sourcePath, activeDocumentDir()) },
        { "workingSvgPath", toRelativePath(c.workingSvgPath, activeDocumentDir()) },
    };
    // Alias used by gcodeSender projects (same field as sourcePath).
    if (!c.sourcePath.empty())
        j["sourceGcodePath"] = j["sourcePath"];
    return true;
}

bool deserializePlotDoc(entt::registry& reg, entt::entity e, const ordered_json& j)
{
    plotter::plot_doc_component c;
    if (j.contains("name")) c.name = j["name"].get<std::string>();
    if (j.contains("paperSizeMM")) c.paperSizeMM = vec2FromJson(j["paperSizeMM"]);
    if (j.contains("sourceGcodePath") && j["sourceGcodePath"].is_string())
        c.sourcePath = fromRelativePath(j["sourceGcodePath"].get<std::string>(), activeDocumentDir());
    else if (j.contains("sourcePath") && j["sourcePath"].is_string())
        c.sourcePath = fromRelativePath(j["sourcePath"].get<std::string>(), activeDocumentDir());
    if (j.contains("workingSvgPath") && j["workingSvgPath"].is_string())
        c.workingSvgPath = fromRelativePath(j["workingSvgPath"].get<std::string>(), activeDocumentDir());
    reg.emplace_or_replace<plotter::plot_doc_component>(e, std::move(c));
    return true;
}

bool serializePlotDocRef(entt::registry& reg, entt::entity e, ordered_json& j)
{
    j = { { "docId", reg.get<plotter::plot_doc_ref>(e).docId } };
    return true;
}

bool deserializePlotDocRef(entt::registry& reg, entt::entity e, const ordered_json& j)
{
    plotter::plot_doc_ref r;
    if (j.contains("docId")) r.docId = j["docId"].get<std::uint32_t>();
    reg.emplace_or_replace<plotter::plot_doc_ref>(e, r);
    return true;
}

bool serializePlotPaths(entt::registry& reg, entt::entity e, ordered_json& j)
{
    const auto& pc = reg.get<plotter::paths_component>(e);
    ordered_json pathsJ = ordered_json::array();
    for (const auto& p : pc.paths)
        pathsJ.push_back(pathToJson(p));
    j["paths"] = std::move(pathsJ);
    if (!pc.pathColors.empty()) {
        ordered_json colorsJ = ordered_json::array();
        for (const auto& c : pc.pathColors)
            colorsJ.push_back(colorToJson(c));
        j["pathColors"] = std::move(colorsJ);
    }
    if (!pc.pathFilled.empty()) {
        ordered_json filledJ = ordered_json::array();
        for (auto f : pc.pathFilled)
            filledJ.push_back(f != 0);
        j["pathFilled"] = std::move(filledJ);
    }
    return true;
}

bool deserializePlotPaths(entt::registry& reg, entt::entity e, const ordered_json& j)
{
    plotter::paths_component pc;
    if (j.contains("paths") && j["paths"].is_array()) {
        for (const auto& pj : j["paths"])
            pc.paths.push_back(pathFromJson(pj));
    }
    if (j.contains("pathColors") && j["pathColors"].is_array()) {
        for (const auto& cj : j["pathColors"])
            pc.pathColors.push_back(colorFromJson(cj));
    }
    if (j.contains("pathFilled") && j["pathFilled"].is_array()) {
        for (const auto& fj : j["pathFilled"])
            pc.pathFilled.push_back(fj.get<bool>() ? 1 : 0);
    }
    reg.emplace_or_replace<plotter::paths_component>(e, std::move(pc));
    return true;
}

bool serializePlotSettings(entt::registry& reg, entt::entity e, ordered_json& j)
{
    j = settingsToJson(reg.get<plotter::settings_component>(e));
    return true;
}

bool deserializePlotSettings(entt::registry& reg, entt::entity e, const ordered_json& j)
{
    plotter::settings_component sc;
    settingsFromJson(j, sc);
    reg.emplace_or_replace<plotter::settings_component>(e, std::move(sc));
    return true;
}

bool serializePlotStats(entt::registry& reg, entt::entity e, ordered_json& j)
{
    const auto& s = reg.get<plotter::toolpath_stats_component>(e);
    j = {
        { "totalPaths", s.totalPaths },
        { "totalPoints", s.totalPoints },
        { "totalDistance", s.totalDistance },
        { "estimatedTime", s.estimatedTime },
    };
    return true;
}

bool deserializePlotStats(entt::registry& reg, entt::entity e, const ordered_json& j)
{
    plotter::toolpath_stats_component s;
    if (j.contains("totalPaths")) s.totalPaths = j["totalPaths"].get<int>();
    if (j.contains("totalPoints")) s.totalPoints = j["totalPoints"].get<int>();
    if (j.contains("totalDistance")) s.totalDistance = j["totalDistance"].get<float>();
    if (j.contains("estimatedTime")) s.estimatedTime = j["estimatedTime"].get<float>();
    reg.emplace_or_replace<plotter::toolpath_stats_component>(e, s);
    return true;
}

bool serializeMachineZone(entt::registry& reg, entt::entity e, ordered_json& j)
{
    j = ofJsonToOrdered(plotter::machineZoneToJson(reg.get<plotter::machine_zone_component>(e)));
    j["sortOrder"] = reg.get<plotter::machine_zone_component>(e).sortOrder;
    return true;
}

bool deserializeMachineZone(entt::registry& reg, entt::entity e, const ordered_json& j)
{
    auto z = plotter::machineZoneFromJson(orderedToOfJson(j), "", reg);
    if (j.contains("sortOrder") && j["sortOrder"].is_number_integer())
        z.sortOrder = j["sortOrder"].get<int>();
    reg.emplace_or_replace<plotter::machine_zone_component>(e, std::move(z));
    if (!reg.all_of<ecs::selectable_component>(e))
        reg.emplace<ecs::selectable_component>(e, false);
    return true;
}

bool serializeInjectionRule(entt::registry& reg, entt::entity e, ordered_json& j)
{
    j = ofJsonToOrdered(plotter::injectionRuleToJson(reg.get<plotter::injection_rule_component>(e)));
    j["sortOrder"] = reg.get<plotter::injection_rule_component>(e).sortOrder;
    return true;
}

bool deserializeInjectionRule(entt::registry& reg, entt::entity e, const ordered_json& j)
{
    int sortOrder = 0;
    if (j.contains("sortOrder") && j["sortOrder"].is_number_integer())
        sortOrder = j["sortOrder"].get<int>();
    auto r = plotter::injectionRuleFromJson(orderedToOfJson(j), sortOrder);
    reg.emplace_or_replace<plotter::injection_rule_component>(e, std::move(r));
    if (!reg.all_of<ecs::selectable_component>(e))
        reg.emplace<ecs::selectable_component>(e, false);
    return true;
}

bool serializePenSettings(entt::registry& reg, entt::entity e, ordered_json& j)
{
    const auto& pen = reg.get<plotter::pen_settings_component>(e).pen;
    j = {
        { "penDownZ", pen.penDownZ },
        { "penUpZ", pen.penUpZ },
        { "drawSpeed", pen.drawSpeed },
        { "travelSpeed", pen.travelSpeed },
        { "penWidth", pen.penWidth },
        { "slowTravels", pen.slowTravels },
        { "visibleLayersOnly", pen.visibleLayersOnly },
        { "smoothApproach", pen.smoothApproach },
        { "approachMm", pen.approachMm },
        { "retractMm", pen.retractMm },
        { "leadOverlapMm", pen.leadOverlapMm },
        { "approachSteps", pen.approachSteps },
        { "feedEasing", pen.feedEasing },
        { "easeInMm", pen.easeInMm },
        { "easeOutMm", pen.easeOutMm },
        { "easeMinFeedFrac", pen.easeMinFeedFrac },
        { "approachHeightMm", pen.approachHeightMm },
        { "approachCurvePow", pen.approachCurvePow },
    };
    return true;
}

bool deserializePenSettings(entt::registry& reg, entt::entity e, const ordered_json& j)
{
    plotter::pen_settings_component c;
    auto& pen = c.pen;
    if (j.contains("penDownZ")) pen.penDownZ = j["penDownZ"].get<float>();
    if (j.contains("penUpZ")) pen.penUpZ = j["penUpZ"].get<float>();
    if (j.contains("drawSpeed")) pen.drawSpeed = j["drawSpeed"].get<float>();
    if (j.contains("travelSpeed")) pen.travelSpeed = j["travelSpeed"].get<float>();
    if (j.contains("penWidth")) pen.penWidth = j["penWidth"].get<float>();
    if (j.contains("slowTravels")) pen.slowTravels = j["slowTravels"].get<bool>();
    if (j.contains("visibleLayersOnly")) pen.visibleLayersOnly = j["visibleLayersOnly"].get<bool>();
    if (j.contains("smoothApproach")) pen.smoothApproach = j["smoothApproach"].get<bool>();
    if (j.contains("approachMm")) pen.approachMm = j["approachMm"].get<float>();
    if (j.contains("retractMm")) pen.retractMm = j["retractMm"].get<float>();
    if (j.contains("leadOverlapMm")) pen.leadOverlapMm = j["leadOverlapMm"].get<float>();
    else if (j.contains("liftEarlyMm")) pen.leadOverlapMm = j["liftEarlyMm"].get<float>(); // legacy
    if (j.contains("approachSteps")) pen.approachSteps = j["approachSteps"].get<int>();
    if (j.contains("feedEasing")) pen.feedEasing = j["feedEasing"].get<bool>();
    if (j.contains("easeInMm")) pen.easeInMm = j["easeInMm"].get<float>();
    if (j.contains("easeOutMm")) pen.easeOutMm = j["easeOutMm"].get<float>();
    if (j.contains("easeMinFeedFrac")) pen.easeMinFeedFrac = j["easeMinFeedFrac"].get<float>();
    if (j.contains("approachHeightMm")) pen.approachHeightMm = j["approachHeightMm"].get<float>();
    if (j.contains("approachCurvePow")) pen.approachCurvePow = j["approachCurvePow"].get<float>();
    // Legacy keys leadInStyle / leadArcRadiusMm / leadArcSweepDeg are ignored.
    reg.emplace_or_replace<plotter::pen_settings_component>(e, std::move(c));
    return true;
}

void registerPlotterDocumentSerializers()
{
    // Static OFDOC_REGISTER_COMPONENT initializers below do the work.
}

OFDOC_REGISTER_COMPONENT(PlotDoc, plotter::plot_doc_component, "plot_doc");
OFDOC_REGISTER_COMPONENT(PlotDocRef, plotter::plot_doc_ref, "plot_doc_ref");
OFDOC_REGISTER_COMPONENT(PlotPaths, plotter::paths_component, "plot_paths");
OFDOC_REGISTER_COMPONENT(PlotSettings, plotter::settings_component, "plot_settings");
OFDOC_REGISTER_COMPONENT(PlotStats, plotter::toolpath_stats_component, "plot_stats");
OFDOC_REGISTER_COMPONENT(MachineZone, plotter::machine_zone_component, "machine_zone");
OFDOC_REGISTER_COMPONENT(InjectionRule, plotter::injection_rule_component, "injection_rule");
OFDOC_REGISTER_COMPONENT(PenSettings, plotter::pen_settings_component, "pen_settings");

} // namespace ofxDocumentKit

namespace plotter {

void registerPlotterDocumentSerializers()
{
    ofxDocumentKit::registerPlotterDocumentSerializers();
}

} // namespace plotter
