#include "PlotterPipelinePanel.h"

#include "IconsFontAwesome5.h"

#include "imgui.h"



#include <algorithm>

#include <functional>



namespace plotter::kit {



void PlotterPipelinePanel::setLastReport(const plotproc::PipelineRunReport& report, bool valid)

{

    m_lastReport = report;

    m_hasReport  = valid;

}



void PlotterPipelinePanel::draw(bool& visible)
{
    ImGui::SetNextWindowSize(ImVec2(360.f, 520.f), ImGuiCond_FirstUseEver);
    const char* title = m_imguiWindowTitle.empty()
        ? "Pipeline###plotter_kit.pipeline"
        : m_imguiWindowTitle.c_str();
    if (!ImGui::Begin(title, &visible)) { ImGui::End(); return; }
    drawBody();
    ImGui::End();
}



void PlotterPipelinePanel::drawBody()

{

    if (!m_pipeline) {

        ImGui::TextDisabled("No pipeline attached.");

        return;

    }



    if (m_hasReport) {

        const float saved = m_lastReport.initial.travelLengthMM

                          - m_lastReport.final.travelLengthMM;

        ImGui::Text("Travel before: %.1f mm", m_lastReport.initial.travelLengthMM);

        ImGui::Text("Travel after:  %.1f mm", m_lastReport.final.travelLengthMM);

        ImGui::Text("Travel saved:  %.1f mm", saved);

    }



    ImGui::Separator();



    m_editor.setPayloadTag("PLOTTER_PIPELINE");

    m_editor.setShowDragHandle(false);

    m_editor.setSectionTitle("Processors");

    m_editor.setFooterHint(

        "Order matters — top runs first. Typical: Dedupe → Merge → Order strokes.");

    m_editor.setSectionFooter([this]() {

        if (ImGui::Button("Reset defaults", ImVec2(-1.f, 0.f))) {

            *m_pipeline = plotproc::PlotPipeline::defaults();

            notifyChanged();

        }

        if (m_drawPresetFooter)

            m_drawPresetFooter();

    });

    m_editor.setStepCount((int)m_pipeline->steps.size());

    m_editor.setStepLabel([this](int i) {

        const auto& step = m_pipeline->steps[(size_t)i];

        if (auto* proc = plotproc::ProcessorRegistry::instance().get(step.processorId))

            return std::string(proc->displayName());

        return step.processorId;

    });

    m_editor.setIsEnabled([this](int i) { return m_pipeline->steps[(size_t)i].enabled; });

    m_editor.setSetEnabled([this](int i, bool on) {

        m_pipeline->steps[(size_t)i].enabled = on;

        notifyChanged();

    });

    m_editor.setDrawStepBody([this](int i) {

        auto& step = m_pipeline->steps[(size_t)i];

        if (auto* proc = plotproc::ProcessorRegistry::instance().get(step.processorId)) {

            if (drawPipelineStepOptions(step.processorId, step.options, proc->defaultOptions()))

                notifyChanged();

        } else {

            ImGui::TextDisabled("Unknown processor: %s", step.processorId.c_str());

        }

    });

    m_editor.setOnMove([this](int from, int to) {

        if (from < 0 || from >= (int)m_pipeline->steps.size()) return;

        to = std::clamp(to, 0, (int)m_pipeline->steps.size());

        plotproc::PipelineStep tmp = std::move(m_pipeline->steps[(size_t)from]);

        m_pipeline->steps.erase(m_pipeline->steps.begin() + from);

        if (to > from) --to;

        m_pipeline->steps.insert(m_pipeline->steps.begin() + to, std::move(tmp));

        notifyChanged();

    });

    m_editor.setOnRemove([this](int i) {

        if (i >= 0 && i < (int)m_pipeline->steps.size())

            m_pipeline->steps.erase(m_pipeline->steps.begin() + i);

        notifyChanged();

    });



    const auto ids = plotproc::ProcessorRegistry::instance().ids();

    std::vector<std::string> addTypes;

    addTypes.push_back("(add processor)");

    for (const auto& id : ids) addTypes.push_back(id);

    m_editor.setAddTypes(addTypes);

    m_editor.setOnAdd([this, ids](int typeIndex) {

        if (typeIndex <= 0 || typeIndex > (int)ids.size()) return;

        plotproc::PipelineStep step;

        step.processorId = ids[(size_t)typeIndex - 1];

        step.enabled     = true;

        if (auto* p = plotproc::ProcessorRegistry::instance().get(step.processorId))

            step.options = p->defaultOptions();

        m_pipeline->steps.push_back(std::move(step));

        notifyChanged();

    });

    m_editor.draw();

}



} // namespace plotter::kit

