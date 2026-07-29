#pragma once

#include "PlotterComponents.h"
#include "json/json_helpers.h"

#include "ofColor.h"
#include "ofPath.h"
#include "ofPolyline.h"

// Shared plotter JSON codecs used by project (.json) and document (.ofdoc) serializers.
// Templated so ofJson and nlohmann::ordered_json can share one implementation.

namespace plotterJson {

template <typename Json>
Json pathToJson(const ofPath& path)
{
	Json polys = Json::array();
	for (const auto& poly : path.getOutline()) {
		Json pts = Json::array();
		for (const auto& v : poly.getVertices())
			pts.push_back(Json::array({ v.x, v.y }));
		polys.push_back(std::move(pts));
	}
	return polys;
}

template <typename Json>
ofPath pathFromJson(const Json& j)
{
	ofPath path;
	if (!j.is_array())
		return path;
	for (const auto& polyJ : j) {
		if (!polyJ.is_array())
			continue;
		ofPolyline poly;
		for (const auto& pt : polyJ) {
			if (pt.is_array() && pt.size() >= 2)
				poly.addVertex(pt[0].template get<float>(), pt[1].template get<float>());
		}
		const auto& verts = poly.getVertices();
		if (verts.empty())
			continue;
		path.moveTo(verts.front());
		for (size_t i = 1; i < verts.size(); ++i)
			path.lineTo(verts[i]);
	}
	return path;
}

/// Canonical array layout [r,g,b,a] (same as ecs::json::colorToJson).
template <typename Json>
Json colorToJson(const ofColor& c)
{
	return Json{ c.r, c.g, c.b, c.a };
}

template <typename Json>
ofColor colorFromJson(const Json& j)
{
	return ecs::json::colorFromJson(j);
}

template <typename Json>
Json thresholdToJson(const ecs::greyscale_threshold_settings& s)
{
	return Json{
		{ "valueMin", s.valueMin },
		{ "valueMax", s.valueMax },
		{ "invert", s.invert },
	};
}

template <typename Json>
void thresholdFromJson(const Json& j, ecs::greyscale_threshold_settings& s)
{
	if (!j.is_object())
		return;
	if (j.contains("valueMin"))
		s.valueMin = j["valueMin"].template get<int>();
	if (j.contains("valueMax"))
		s.valueMax = j["valueMax"].template get<int>();
	if (j.contains("invert"))
		s.invert = j["invert"].template get<bool>();
}

template <typename Json>
Json curveTraceToJson(const ecs::curve_trace_settings& s)
{
	return Json{
		{ "turdsize", s.turdsize },
		{ "alphamax", s.alphamax },
		{ "opticurve", s.opticurve },
		{ "opttolerance", s.opttolerance },
		{ "curveResolution", s.curveResolution },
		{ "traceHoles", s.traceHoles },
		{ "traceScale", s.traceScale },
	};
}

template <typename Json>
void curveTraceFromJson(const Json& j, ecs::curve_trace_settings& s)
{
	if (!j.is_object())
		return;
	if (j.contains("turdsize"))
		s.turdsize = j["turdsize"].template get<int>();
	if (j.contains("alphamax"))
		s.alphamax = j["alphamax"].template get<float>();
	if (j.contains("opticurve"))
		s.opticurve = j["opticurve"].template get<bool>();
	if (j.contains("opttolerance"))
		s.opttolerance = j["opttolerance"].template get<float>();
	if (j.contains("curveResolution"))
		s.curveResolution = j["curveResolution"].template get<int>();
	if (j.contains("traceHoles"))
		s.traceHoles = j["traceHoles"].template get<bool>();
	if (j.contains("traceScale"))
		s.traceScale = j["traceScale"].template get<float>();
}

template <typename Json>
Json settingsToJson(const plotter::settings_component& sc)
{
	Json j;
	j["brushIndex"] = sc.brushIndex;
	j["plotFinderType"] = static_cast<int>(sc.plotFinderType);
	j["sketchLines"] = {
		{ "lineMinLength", sc.sketchLines.lineMinLength },
		{ "lineMaxLength", sc.sketchLines.lineMaxLength },
		{ "angleTests", sc.sketchLines.angleTests },
		{ "lineDensity", sc.sketchLines.lineDensity },
		{ "eraseMin", sc.sketchLines.eraseMin },
		{ "eraseMax", sc.sketchLines.eraseMax },
		{ "squiggleMin", sc.sketchLines.squiggleMin },
		{ "squiggleMax", sc.sketchLines.squiggleMax },
		{ "shouldLiftPen", sc.sketchLines.shouldLiftPen },
		{ "plotResolution", sc.sketchLines.plotResolution },
	};
	j["crossHatch"] = {
		{ "angle1", sc.crossHatch.angle1 },
		{ "angle2", sc.crossHatch.angle2 },
		{ "useSecondary", sc.crossHatch.useSecondary },
		{ "lineSpacing", sc.crossHatch.lineSpacing },
		{ "minBrightness", sc.crossHatch.minBrightness },
	};
	j["spiral"] = {
		{ "ringSpacing", sc.spiral.ringSpacing },
		{ "amplitude", sc.spiral.amplitude },
		{ "velocity", sc.spiral.velocity },
		{ "centreX", sc.spiral.centreX },
		{ "centreY", sc.spiral.centreY },
		{ "spiralSize", sc.spiral.spiralSize },
		{ "ignoreWhite", sc.spiral.ignoreWhite },
	};
	j["stippling"] = {
		{ "dotSpacingMin", sc.stippling.dotSpacingMin },
		{ "dotSpacingMax", sc.stippling.dotSpacingMax },
		{ "dotRadius", sc.stippling.dotRadius },
		{ "iterations", sc.stippling.iterations },
	};
	j["contours"] = {
		{ "cannyLow", sc.contours.cannyLow },
		{ "cannyHigh", sc.contours.cannyHigh },
		{ "minContourLen", sc.contours.minContourLen },
	};
	j["potrace"] = {
		{ "threshold", thresholdToJson<Json>(sc.potrace.threshold) },
		{ "curve", curveTraceToJson<Json>(sc.potrace.curve) },
	};
	j["sketchCurves"] = {
		{ "lineMinLength", sc.sketchCurves.lineMinLength },
		{ "lineMaxLength", sc.sketchCurves.lineMaxLength },
		{ "angleTests", sc.sketchCurves.angleTests },
		{ "lineDensity", sc.sketchCurves.lineDensity },
		{ "eraseMin", sc.sketchCurves.eraseMin },
		{ "eraseMax", sc.sketchCurves.eraseMax },
		{ "squiggleMin", sc.sketchCurves.squiggleMin },
		{ "squiggleMax", sc.sketchCurves.squiggleMax },
		{ "shouldLiftPen", sc.sketchCurves.shouldLiftPen },
		{ "curviness", sc.sketchCurves.curviness },
	};
	j["sketchFlowField"] = {
		{ "lineDensity", sc.sketchFlowField.lineDensity },
		{ "stepSizeMm", sc.sketchFlowField.stepSizeMm },
		{ "lineMinLength", sc.sketchFlowField.lineMinLength },
		{ "lineMaxLength", sc.sketchFlowField.lineMaxLength },
		{ "shouldLiftPen", sc.sketchFlowField.shouldLiftPen },
		{ "eraseMin", sc.sketchFlowField.eraseMin },
		{ "eraseMax", sc.sketchFlowField.eraseMax },
		{ "followGradient", sc.sketchFlowField.followGradient },
	};
	j["sketchSobelEdges"] = {
		{ "cannyLow", sc.sketchSobelEdges.cannyLow },
		{ "cannyHigh", sc.sketchSobelEdges.cannyHigh },
		{ "hatchLength", sc.sketchSobelEdges.hatchLength },
		{ "hatchSpacing", sc.sketchSobelEdges.hatchSpacing },
		{ "hatchLayers", sc.sketchSobelEdges.hatchLayers },
		{ "minEdgeMm", sc.sketchSobelEdges.minEdgeMm },
	};
	return j;
}

template <typename Json>
void settingsFromJson(const Json& j, plotter::settings_component& sc)
{
	if (!j.is_object())
		return;
	if (j.contains("brushIndex"))
		sc.brushIndex = j["brushIndex"].template get<int>();
	if (j.contains("plotFinderType"))
		sc.plotFinderType = static_cast<PlotFinderType>(j["plotFinderType"].template get<int>());

	auto loadBlock = [&](const char* key, auto fn) {
		if (j.contains(key) && j[key].is_object())
			fn(j[key]);
	};

	loadBlock("sketchLines", [&](const Json& b) {
		auto& s = sc.sketchLines;
		if (b.contains("lineMinLength"))
			s.lineMinLength = b["lineMinLength"];
		if (b.contains("lineMaxLength"))
			s.lineMaxLength = b["lineMaxLength"];
		if (b.contains("angleTests"))
			s.angleTests = b["angleTests"];
		if (b.contains("lineDensity"))
			s.lineDensity = b["lineDensity"];
		if (b.contains("eraseMin"))
			s.eraseMin = b["eraseMin"];
		if (b.contains("eraseMax"))
			s.eraseMax = b["eraseMax"];
		if (b.contains("squiggleMin"))
			s.squiggleMin = b["squiggleMin"];
		if (b.contains("squiggleMax"))
			s.squiggleMax = b["squiggleMax"];
		if (b.contains("shouldLiftPen"))
			s.shouldLiftPen = b["shouldLiftPen"];
		if (b.contains("plotResolution"))
			s.plotResolution = b["plotResolution"];
	});
	loadBlock("crossHatch", [&](const Json& b) {
		auto& s = sc.crossHatch;
		if (b.contains("angle1"))
			s.angle1 = b["angle1"];
		if (b.contains("angle2"))
			s.angle2 = b["angle2"];
		if (b.contains("useSecondary"))
			s.useSecondary = b["useSecondary"];
		if (b.contains("lineSpacing"))
			s.lineSpacing = b["lineSpacing"];
		if (b.contains("minBrightness"))
			s.minBrightness = b["minBrightness"];
	});
	loadBlock("spiral", [&](const Json& b) {
		auto& s = sc.spiral;
		if (b.contains("ringSpacing"))
			s.ringSpacing = b["ringSpacing"];
		if (b.contains("amplitude"))
			s.amplitude = b["amplitude"];
		if (b.contains("velocity"))
			s.velocity = b["velocity"];
		if (b.contains("centreX"))
			s.centreX = b["centreX"];
		if (b.contains("centreY"))
			s.centreY = b["centreY"];
		if (b.contains("spiralSize"))
			s.spiralSize = b["spiralSize"];
		if (b.contains("ignoreWhite"))
			s.ignoreWhite = b["ignoreWhite"];
	});
	loadBlock("stippling", [&](const Json& b) {
		auto& s = sc.stippling;
		if (b.contains("dotSpacingMin"))
			s.dotSpacingMin = b["dotSpacingMin"];
		if (b.contains("dotSpacingMax"))
			s.dotSpacingMax = b["dotSpacingMax"];
		if (b.contains("dotRadius"))
			s.dotRadius = b["dotRadius"];
		if (b.contains("iterations"))
			s.iterations = b["iterations"];
	});
	loadBlock("contours", [&](const Json& b) {
		auto& s = sc.contours;
		if (b.contains("cannyLow"))
			s.cannyLow = b["cannyLow"];
		if (b.contains("cannyHigh"))
			s.cannyHigh = b["cannyHigh"];
		if (b.contains("minContourLen"))
			s.minContourLen = b["minContourLen"];
	});
	loadBlock("potrace", [&](const Json& b) {
		if (b.contains("threshold"))
			thresholdFromJson(b["threshold"], sc.potrace.threshold);
		if (b.contains("curve"))
			curveTraceFromJson(b["curve"], sc.potrace.curve);
	});
	loadBlock("sketchCurves", [&](const Json& b) {
		auto& s = sc.sketchCurves;
		if (b.contains("lineMinLength"))
			s.lineMinLength = b["lineMinLength"];
		if (b.contains("lineMaxLength"))
			s.lineMaxLength = b["lineMaxLength"];
		if (b.contains("angleTests"))
			s.angleTests = b["angleTests"];
		if (b.contains("lineDensity"))
			s.lineDensity = b["lineDensity"];
		if (b.contains("eraseMin"))
			s.eraseMin = b["eraseMin"];
		if (b.contains("eraseMax"))
			s.eraseMax = b["eraseMax"];
		if (b.contains("squiggleMin"))
			s.squiggleMin = b["squiggleMin"];
		if (b.contains("squiggleMax"))
			s.squiggleMax = b["squiggleMax"];
		if (b.contains("shouldLiftPen"))
			s.shouldLiftPen = b["shouldLiftPen"];
		if (b.contains("curviness"))
			s.curviness = b["curviness"];
	});
	loadBlock("sketchFlowField", [&](const Json& b) {
		auto& s = sc.sketchFlowField;
		if (b.contains("lineDensity"))
			s.lineDensity = b["lineDensity"];
		if (b.contains("stepSizeMm"))
			s.stepSizeMm = b["stepSizeMm"];
		if (b.contains("lineMinLength"))
			s.lineMinLength = b["lineMinLength"];
		if (b.contains("lineMaxLength"))
			s.lineMaxLength = b["lineMaxLength"];
		if (b.contains("shouldLiftPen"))
			s.shouldLiftPen = b["shouldLiftPen"];
		if (b.contains("eraseMin"))
			s.eraseMin = b["eraseMin"];
		if (b.contains("eraseMax"))
			s.eraseMax = b["eraseMax"];
		if (b.contains("followGradient"))
			s.followGradient = b["followGradient"];
	});
	loadBlock("sketchSobelEdges", [&](const Json& b) {
		auto& s = sc.sketchSobelEdges;
		if (b.contains("cannyLow"))
			s.cannyLow = b["cannyLow"];
		if (b.contains("cannyHigh"))
			s.cannyHigh = b["cannyHigh"];
		if (b.contains("hatchLength"))
			s.hatchLength = b["hatchLength"];
		if (b.contains("hatchSpacing"))
			s.hatchSpacing = b["hatchSpacing"];
		if (b.contains("hatchLayers"))
			s.hatchLayers = b["hatchLayers"];
		if (b.contains("minEdgeMm"))
			s.minEdgeMm = b["minEdgeMm"];
	});
}

} // namespace plotterJson
