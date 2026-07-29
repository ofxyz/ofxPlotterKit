#include "PlotterEditorToolbar.h"

#include "PathEditorPanelRulerToolbar.h"
#include "PathEditorWidget.h"
#include "VectorStrokeToolbar.h"
#include "VectorToolbarGroups.h"

namespace plotter::kit {

void registerPlotterEditorToolbar(const PlotterEditorToolbarOpts& opts) {
    ImVectorEditor::Config& config = opts.pathConfig;
    const std::string& prefix = opts.idPrefix;

    ofkitty::registerVectorToolbarGroups(config, prefix);

    ofkitty::RulerToolbarTargets rulers;
    rulers.mainView = opts.mainView;
    rulers.runtime  = opts.runtime;
    if (opts.panel) {
        auto panelTargets = ofkitty::rulerToolbarTargetsForPanel(*opts.panel);
        rulers.togglePanelRulers = std::move(panelTargets.togglePanelRulers);
        rulers.panelRulersActive = std::move(panelTargets.panelRulersActive);
        rulers.panelFitToView    = std::move(panelTargets.panelFitToView);
    }
    if (rulers.togglePanelRulers || rulers.mainView || rulers.runtime)
        ofkitty::registerRulerToolbarItems(rulers, prefix + ".ruler");

    if (opts.stroke)
        ofkitty::registerStrokeToolbarItems(*opts.stroke, prefix + ".stroke");
}

void drawPlotterEditorToolbarInline(bool horizontal) {
    ofkitty::Runtime::instance().drawToolbarItems(horizontal, {
        ofkitty::ToolbarGroups::Select,
        ofkitty::VectorToolbarGroups::Draw,
        ofkitty::ToolbarGroups::Ruler,
        ofkitty::VectorToolbarGroups::Stroke,
    });
}

} // namespace plotter::kit
