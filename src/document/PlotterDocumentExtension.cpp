#include "PlotterDocumentExtension.h"

namespace plotter {
namespace {

ofxDocumentKit::ordered_json ofJsonToOrdered(const ofJson& j)
{
    return ofxDocumentKit::ordered_json::parse(j.dump());
}

ofJson orderedToOfJson(const ofxDocumentKit::ordered_json& j)
{
    return ofJson::parse(j.dump());
}

} // namespace

void writePlotterDocumentEnvelope(ofxDocumentKit::ordered_json& root,
                                  const PlotterDocumentEnvelope& env)
{
    ofxDocumentKit::ordered_json p;
    p["drawTargetZoneId"]        = env.drawTargetZoneId;
    p["pipeline"]                = ofJsonToOrdered(env.pipeline.toJson());
    p["printOffsetX"]            = env.printOffsetX;
    p["printOffsetY"]            = env.printOffsetY;
    p["printScaleX"]             = env.printScaleX;
    p["printScaleY"]             = env.printScaleY;
    p["printRotateQuarterTurns"] = env.printRotateQuarterTurns;
    p["liveTransformEnabled"]    = env.liveTransformEnabled;
    p["usePipeline"]             = env.usePipeline;
    p["useInjections"]           = env.useInjections;
    if (env.hasPenAppearance) {
        p["penColor"] = { env.penColor.r, env.penColor.g, env.penColor.b, env.penColor.a };
        p["scaleStrokeToPenWidth"] = env.scaleStrokeToPenWidth;
    }
    if (env.hasMachineBed) {
        p["machineBed"] = {
            { "paperOriginX", env.machineBed.paperOriginX },
            { "paperOriginY", env.machineBed.paperOriginY },
            { "envelope",
              { { "minX", env.machineEnvelope.minX },
                { "minY", env.machineEnvelope.minY },
                { "minZ", env.machineEnvelope.minZ },
                { "maxX", env.machineEnvelope.maxX },
                { "maxY", env.machineEnvelope.maxY },
                { "maxZ", env.machineEnvelope.maxZ } } },
        };
    }
    root["plotter"] = std::move(p);
}

void readPlotterDocumentEnvelope(const ofxDocumentKit::ordered_json& root,
                                 PlotterDocumentEnvelope& env)
{
    if (!root.contains("plotter") || !root["plotter"].is_object())
        return;
    const auto& p = root["plotter"];
    if (p.contains("drawTargetZoneId") && p["drawTargetZoneId"].is_string())
        env.drawTargetZoneId = p["drawTargetZoneId"].get<std::string>();
    if (p.contains("pipeline"))
        env.pipeline.fromJson(orderedToOfJson(p["pipeline"]));
    if (p.contains("printOffsetX") && p["printOffsetX"].is_number())
        env.printOffsetX = p["printOffsetX"].get<float>();
    if (p.contains("printOffsetY") && p["printOffsetY"].is_number())
        env.printOffsetY = p["printOffsetY"].get<float>();
    if (p.contains("printScaleX") && p["printScaleX"].is_number())
        env.printScaleX = p["printScaleX"].get<float>();
    if (p.contains("printScaleY") && p["printScaleY"].is_number())
        env.printScaleY = p["printScaleY"].get<float>();
    if (p.contains("printRotateQuarterTurns") && p["printRotateQuarterTurns"].is_number_integer())
        env.printRotateQuarterTurns = p["printRotateQuarterTurns"].get<int>();
    if (p.contains("liveTransformEnabled") && p["liveTransformEnabled"].is_boolean())
        env.liveTransformEnabled = p["liveTransformEnabled"].get<bool>();
    if (p.contains("usePipeline") && p["usePipeline"].is_boolean())
        env.usePipeline = p["usePipeline"].get<bool>();
    if (p.contains("useInjections") && p["useInjections"].is_boolean())
        env.useInjections = p["useInjections"].get<bool>();
    if (p.contains("penColor") && p["penColor"].is_array() && p["penColor"].size() >= 4) {
        env.penColor.r = p["penColor"][0].get<float>();
        env.penColor.g = p["penColor"][1].get<float>();
        env.penColor.b = p["penColor"][2].get<float>();
        env.penColor.a = p["penColor"][3].get<float>();
        env.hasPenAppearance = true;
    }
    if (p.contains("scaleStrokeToPenWidth") && p["scaleStrokeToPenWidth"].is_boolean()) {
        env.scaleStrokeToPenWidth = p["scaleStrokeToPenWidth"].get<bool>();
        env.hasPenAppearance = true;
    }
    if (p.contains("machineBed") && p["machineBed"].is_object()) {
        env.hasMachineBed = true;
        const auto& bed = p["machineBed"];
        if (bed.contains("paperOriginX") && bed["paperOriginX"].is_number())
            env.machineBed.paperOriginX = bed["paperOriginX"].get<float>();
        if (bed.contains("paperOriginY") && bed["paperOriginY"].is_number())
            env.machineBed.paperOriginY = bed["paperOriginY"].get<float>();
        if (bed.contains("envelope") && bed["envelope"].is_object()) {
            const auto& e = bed["envelope"];
            if (e.contains("minX")) env.machineEnvelope.minX = e["minX"].get<float>();
            if (e.contains("minY")) env.machineEnvelope.minY = e["minY"].get<float>();
            if (e.contains("minZ")) env.machineEnvelope.minZ = e["minZ"].get<float>();
            if (e.contains("maxX")) env.machineEnvelope.maxX = e["maxX"].get<float>();
            if (e.contains("maxY")) env.machineEnvelope.maxY = e["maxY"].get<float>();
            if (e.contains("maxZ")) env.machineEnvelope.maxZ = e["maxZ"].get<float>();
        }
    }
}

} // namespace plotter
