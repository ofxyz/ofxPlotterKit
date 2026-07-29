#pragma once

#include "ofxVectorKit.h"

#include <string>

namespace plotter::kit {

/// One-call toolbar registration for plotter / path editor apps.
struct PlotterEditorToolbarOpts {
    ImVectorEditor::Config&       pathConfig;
    std::string                   idPrefix = "ofxkit.editor";
    ofkitty::PathEditorPanelView* panel    {nullptr};
    ofkitty::StrokeToolbarState*  stroke   {nullptr};
    ofkitty::Runtime::MainView2D* mainView {nullptr};
    ofkitty::Runtime*             runtime  {nullptr};
};

void registerPlotterEditorToolbar(const PlotterEditorToolbarOpts& opts);

/// Inline icon row matching the plotter/path Toolbar palette (select, pen, ruler, stroke).
void drawPlotterEditorToolbarInline(bool horizontal = true);

} // namespace plotter::kit
