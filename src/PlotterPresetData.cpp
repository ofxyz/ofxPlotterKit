#include "PlotterPresetData.h"

namespace plotter::kit {

namespace {

ofJson paper(float ox, float oy, float w, float h)
{
    return paperPresetJson(ox, oy, w, h);
}

ofJson envelope(float minX, float minY, float maxX, float maxY)
{
    return envelopePresetJson(minX, minY, maxX, maxY);
}

} // namespace

ofJson paperPresetJson(float originX, float originY, float paperW, float paperH)
{
    return ofJson{
        { "paperOriginX", originX },
        { "paperOriginY", originY },
        { "paperW",       paperW },
        { "paperH",       paperH },
    };
}

bool paperPresetEquals(const ofJson& a, const ofJson& b)
{
    if (!a.is_object() || !b.is_object()) return false;
    for (const char* key : { "paperOriginX", "paperOriginY", "paperW", "paperH" }) {
        if (!a.contains(key) || !b.contains(key)) return false;
        if (std::abs(a[key].get<float>() - b[key].get<float>()) >= 0.01f) return false;
    }
    return true;
}

ofJson envelopePresetJson(float minX, float minY, float maxX, float maxY)
{
    return ofJson{
        { "minX", minX },
        { "minY", minY },
        { "maxX", maxX },
        { "maxY", maxY },
    };
}

bool envelopePresetEquals(const ofJson& a, const ofJson& b)
{
    if (!a.is_object() || !b.is_object()) return false;
    for (const char* key : { "minX", "minY", "maxX", "maxY" }) {
        if (!a.contains(key) || !b.contains(key)) return false;
        if (std::abs(a[key].get<float>() - b[key].get<float>()) >= 0.01f) return false;
    }
    return true;
}

void addBuiltinPaperPresets(ofkitty::PresetLibrary& lib)
{
    lib.addBuiltin("A5 Portrait",   paper(0.f, 0.f, 148.f, 210.f));
    lib.addBuiltin("A5 Landscape",  paper(0.f, 0.f, 210.f, 148.f));
    lib.addBuiltin("A4 Portrait",   paper(0.f, 0.f, 210.f, 297.f));
    lib.addBuiltin("A4 Landscape",  paper(0.f, 0.f, 297.f, 210.f));
    lib.addBuiltin("A3 Portrait",   paper(0.f, 0.f, 297.f, 420.f));
    lib.addBuiltin("A3 Landscape",  paper(0.f, 0.f, 420.f, 297.f));
}

void addBuiltinEnvelopePresets(ofkitty::PresetLibrary& lib)
{
    lib.addBuiltin("A3 bed (420 x 297)", envelope(0.f, 0.f, 420.f, 297.f));
    lib.addBuiltin("A4 portrait (210 x 297)", envelope(0.f, 0.f, 210.f, 297.f));
    lib.addBuiltin("A4 landscape (297 x 210)", envelope(0.f, 0.f, 297.f, 210.f));
    lib.addBuiltin("US Letter (279 x 216)", envelope(0.f, 0.f, 279.4f, 215.9f));
}

ofJson pipelinePresetJson(const plotproc::PlotPipeline& pipeline)
{
    ofJson stepsJson = ofJson::array();
    for (const auto& s : pipeline.steps) {
        stepsJson.push_back({
            { "id",      s.processorId },
            { "enabled", s.enabled },
            { "options", s.options },
        });
    }
    return ofJson{ { "steps", std::move(stepsJson) } };
}

void applyPipelinePreset(plotproc::PlotPipeline& pipeline, const ofJson& j)
{
    if (!j.is_object() || !j.contains("steps") || !j["steps"].is_array()) return;
    pipeline.steps.clear();
    for (const auto& s : j["steps"]) {
        if (!s.is_object()) continue;
        plotproc::PipelineStep step;
        step.processorId = s.value("id", std::string{});
        step.enabled     = s.value("enabled", true);
        if (s.contains("options")) step.options = s["options"];
        if (!step.processorId.empty()) pipeline.steps.push_back(std::move(step));
    }
}

bool pipelinePresetEquals(const ofJson& a, const ofJson& b)
{
    return a == b;
}

void addBuiltinPipelinePresets(ofkitty::PresetLibrary& lib)
{
    lib.addBuiltin("Default", pipelinePresetJson(plotproc::PlotPipeline::defaults()));
}

namespace {

ofJson penPreset(const PenSettings& pen,
                 const ofFloatColor& color,
                 bool scaleStrokeToPenWidth)
{
    return penPresetJson(pen, color, scaleStrokeToPenWidth);
}

ofFloatColor blueDrawColor()
{
    return { 0.08f, 0.15f, 0.75f, 1.f };
}

} // namespace

ofJson penPresetJson(const PenSettings& pen,
                     const ofFloatColor& color,
                     bool scaleStrokeToPenWidth)
{
    return ofJson{
        { "penUpZ",                 pen.penUpZ },
        { "penDownZ",               pen.penDownZ },
        { "drawSpeed",              pen.drawSpeed },
        { "travelSpeed",            pen.travelSpeed },
        { "penWidth",               pen.penWidth },
        { "slowTravels",            pen.slowTravels },
        { "smoothApproach",         pen.smoothApproach },
        { "approachMm",             pen.approachMm },
        { "retractMm",              pen.retractMm },
        { "leadOverlapMm",          pen.leadOverlapMm },
        { "approachSteps",          pen.approachSteps },
        { "feedEasing",             pen.feedEasing },
        { "easeInMm",               pen.easeInMm },
        { "easeOutMm",              pen.easeOutMm },
        { "easeMinFeedFrac",        pen.easeMinFeedFrac },
        { "approachHeightMm",       pen.approachHeightMm },
        { "approachCurvePow",       pen.approachCurvePow },
        { "scaleStrokeToPenWidth",  scaleStrokeToPenWidth },
        { "color", ofJson::array({ color.r, color.g, color.b, color.a }) },
    };
}

void applyPenPreset(const ofJson& j,
                    PenSettings& pen,
                    ofFloatColor& color,
                    bool& scaleStrokeToPenWidth)
{
    if (!j.is_object()) return;
    if (j.contains("penUpZ"))      pen.penUpZ      = j["penUpZ"].get<float>();
    if (j.contains("penDownZ"))    pen.penDownZ    = j["penDownZ"].get<float>();
    if (j.contains("drawSpeed"))   pen.drawSpeed   = j["drawSpeed"].get<float>();
    if (j.contains("travelSpeed")) pen.travelSpeed = j["travelSpeed"].get<float>();
    if (j.contains("penWidth"))    pen.penWidth    = j["penWidth"].get<float>();
    if (j.contains("slowTravels")) pen.slowTravels = j["slowTravels"].get<bool>();
    if (j.contains("smoothApproach")) pen.smoothApproach = j["smoothApproach"].get<bool>();
    if (j.contains("approachMm"))  pen.approachMm  = j["approachMm"].get<float>();
    if (j.contains("retractMm"))   pen.retractMm   = j["retractMm"].get<float>();
    if (j.contains("leadOverlapMm")) pen.leadOverlapMm = j["leadOverlapMm"].get<float>();
    else if (j.contains("liftEarlyMm")) pen.leadOverlapMm = j["liftEarlyMm"].get<float>(); // legacy
    if (j.contains("approachSteps")) pen.approachSteps = j["approachSteps"].get<int>();
    if (j.contains("feedEasing"))  pen.feedEasing  = j["feedEasing"].get<bool>();
    if (j.contains("easeInMm"))    pen.easeInMm    = j["easeInMm"].get<float>();
    if (j.contains("easeOutMm"))   pen.easeOutMm   = j["easeOutMm"].get<float>();
    if (j.contains("easeMinFeedFrac")) pen.easeMinFeedFrac = j["easeMinFeedFrac"].get<float>();
    if (j.contains("approachHeightMm")) pen.approachHeightMm = j["approachHeightMm"].get<float>();
    if (j.contains("approachCurvePow")) pen.approachCurvePow = j["approachCurvePow"].get<float>();
    // Legacy keys leadInStyle / leadArcRadiusMm / leadArcSweepDeg are ignored.
    if (j.contains("scaleStrokeToPenWidth"))
        scaleStrokeToPenWidth = j["scaleStrokeToPenWidth"].get<bool>();
    if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 4) {
        color.r = j["color"][0].get<float>();
        color.g = j["color"][1].get<float>();
        color.b = j["color"][2].get<float>();
        color.a = j["color"][3].get<float>();
    }
}

bool penPresetEquals(const ofJson& a, const ofJson& b)
{
    if (!a.is_object() || !b.is_object()) return false;
    for (const char* key : { "penUpZ", "penDownZ", "drawSpeed", "travelSpeed", "penWidth",
                             "approachMm", "retractMm", "easeInMm", "easeOutMm", "easeMinFeedFrac",
                             "approachHeightMm", "approachCurvePow" }) {
        if (!a.contains(key) || !b.contains(key)) return false;
        if (std::abs(a[key].get<float>() - b[key].get<float>()) >= 0.01f) return false;
    }
    for (const char* key : { "slowTravels", "scaleStrokeToPenWidth", "smoothApproach", "feedEasing" }) {
        if (!a.contains(key) || !b.contains(key)) return false;
        if (a[key].get<bool>() != b[key].get<bool>()) return false;
    }
    if (a.value("approachSteps", 8) != b.value("approachSteps", 8)) return false;
    if (std::abs(a.value("leadOverlapMm", 0.f) - b.value("leadOverlapMm", 0.f)) >= 0.01f) return false;
    if (!a.contains("color") || !b.contains("color")) return false;
    const auto& ca = a["color"];
    const auto& cb = b["color"];
    if (!ca.is_array() || !cb.is_array() || ca.size() < 4 || cb.size() < 4) return false;
    for (int i = 0; i < 4; ++i) {
        if (std::abs(ca[i].get<float>() - cb[i].get<float>()) >= 0.001f) return false;
    }
    return true;
}

void addBuiltinPenPresets(ofkitty::PresetLibrary& lib)
{
    PenSettings standard{};
    standard.penDownZ    = 0.f;
    standard.penUpZ      = 5.f;
    standard.drawSpeed   = 800.f;
    standard.travelSpeed = 3000.f;
    standard.penWidth    = 0.3f;
    lib.addBuiltin("Standard plotter",
                   penPreset(standard, blueDrawColor(), false));

    PenSettings slow = standard;
    slow.slowTravels = true;
    lib.addBuiltin("Slow travels",
                   penPreset(slow, blueDrawColor(), false));
}

} // namespace plotter::kit
