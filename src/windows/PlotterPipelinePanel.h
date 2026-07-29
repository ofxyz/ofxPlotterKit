#pragma once

#include "ChainEditor.h"
#include "PlotterPipelineStepUi.h"
#include "ofxPlotProcessors.h"

#include <functional>
#include <string>

namespace plotter::kit {

/// Reusable post-processor pipeline editor (extracted from example-kit GcodeGeneratorPanel).
class PlotterPipelinePanel {
public:
    void setPipeline(plotproc::PlotPipeline* pipeline) { m_pipeline = pipeline; }
    void setOnChanged(std::function<void()> cb) { m_onChanged = std::move(cb); }
    void setDrawPresetFooter(std::function<void()> cb) { m_drawPresetFooter = std::move(cb); }

    void setLastReport(const plotproc::PipelineRunReport& report, bool valid);

    std::string name()      const { return "Pipeline"; }
    bool        isVisible() const { return false; }
    void        setImGuiWindowTitle(std::string title) { m_imguiWindowTitle = std::move(title); }

    void draw(bool& visible);
    void drawBody();

private:
    void notifyChanged() { if (m_onChanged) m_onChanged(); }

    plotproc::PlotPipeline* m_pipeline = nullptr;
    ofkitty::ChainEditor    m_editor;
    std::function<void()>   m_onChanged;
    std::function<void()>   m_drawPresetFooter;
    std::string             m_imguiWindowTitle;

    bool m_hasReport = false;
    plotproc::PipelineRunReport m_lastReport;
};

} // namespace plotter::kit
