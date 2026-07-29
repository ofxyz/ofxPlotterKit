#pragma once

#include "GeneratorRegistry.h"

#include <string>

namespace plotter::kit {

/// True when @p name is the displayName() of a registered path generator.
/// Generators register themselves with plotgen::GeneratorRegistry — there is
/// no hard-coded name list here.
inline bool isGeneratorLayerName(const std::string& name)
{
	return plotgen::GeneratorRegistry::instance()
		.hasDisplayName(name, plotgen::GeneratorOutputKind::Path);
}

/// Layer name for a registered generator (= displayName()). Empty if unknown /
/// not linked (e.g. optional addon generator not registered).
inline const char* generatorLayerName(const plotgen::PlotGeneratorId& id)
{
	if (const plotgen::IPlotGenerator* g = plotgen::GeneratorRegistry::instance().get(id))
		return g->displayName();
	return "";
}

inline const char* generatorLayerName(const char* family, const char* name)
{
	return generatorLayerName(plotgen::PlotGeneratorId{ family, name });
}

} // namespace plotter::kit
