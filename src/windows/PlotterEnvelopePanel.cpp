#include "PlotterEnvelopePanel.h"
#include "PlotterPresetData.h"

#include "imgui.h"

namespace plotter::kit {
namespace {

float fieldWidth(int chars = 15)
{
    return ImGui::CalcTextSize("0").x * static_cast<float>(chars)
         + ImGui::GetStyle().FramePadding.x * 2.f;
}

} // namespace

void PlotterEnvelopePanel::draw(bool& visible)
{
    ImGui::SetNextWindowSize(ImVec2(360.f, 420.f), ImGuiCond_FirstUseEver);
    const char* title = m_imguiWindowTitle.empty()
        ? "Machine Envelope###plotter_kit.envelope"
        : m_imguiWindowTitle.c_str();
    if (!ImGui::Begin(title, &visible)) { ImGui::End(); return; }
    drawBody();
    ImGui::End();
}

void PlotterEnvelopePanel::drawBody()
{
    if (!m_prefs) {
        ImGui::TextDisabled("No machine prefs attached.");
        return;
    }

    auto& prefs = *m_prefs;
    const float fieldW = fieldWidth(15);
    bool changed = false;

    if (m_presets) {
        if (m_presets->drawPicker(
                "envelope",
                [&prefs] {
                    return envelopePresetJson(
                        prefs.envelope.minX, prefs.envelope.minY,
                        prefs.envelope.maxX, prefs.envelope.maxY);
                },
                [&prefs, &changed](const ofJson& j) {
                    prefs.envelope.minX = j["minX"].get<float>();
                    prefs.envelope.minY = j["minY"].get<float>();
                    prefs.envelope.maxX = j["maxX"].get<float>();
                    prefs.envelope.maxY = j["maxY"].get<float>();
                    prefs.save();
                    changed = true;
                },
                envelopePresetEquals)) {
            changed = true;
        }
    }

    const float envW = prefs.envelope.maxX - prefs.envelope.minX;
    const float envH = prefs.envelope.maxY - prefs.envelope.minY;
    ImGui::TextDisabled("Size %.0f x %.0f mm", envW, envH);

    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::DragFloat("Min X", &prefs.envelope.minX, 1.f, -5000.f, 5000.f, "%.0f mm");
    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::DragFloat("Min Y", &prefs.envelope.minY, 1.f, -5000.f, 5000.f, "%.0f mm");
    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::DragFloat("Max X", &prefs.envelope.maxX, 1.f, -5000.f, 5000.f, "%.0f mm");
    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::DragFloat("Max Y", &prefs.envelope.maxY, 1.f, -5000.f, 5000.f, "%.0f mm");
    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::DragFloat("Min Z", &prefs.envelope.minZ, 0.5f, -500.f, 500.f, "%.1f mm");
    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::DragFloat("Max Z", &prefs.envelope.maxZ, 0.5f, -500.f, 500.f, "%.1f mm");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Software Z travel limits (machine mm). Sends are refused when any\n"
            "move (incl. pen up/down and snippets) would leave this range.");

    bool invX = prefs.axes.signX < 0.f;
    bool invY = prefs.axes.signY < 0.f;
    if (ImGui::Checkbox("Invert +X", &invX)) {
        prefs.axes.signX = invX ? -1.f : 1.f;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "When checked, positive paper X travels toward decreasing machine X.\n"
            "Paper origin still sets where paper (0,0) sits in machine mm.");
    if (ImGui::Checkbox("Invert +Y", &invY)) {
        prefs.axes.signY = invY ? -1.f : 1.f;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "When checked, positive paper Y travels toward decreasing machine Y.\n"
            "Paper origin still sets where paper (0,0) sits in machine mm.");
    bool invZ = prefs.axes.signZ < 0.f;
    if (ImGui::Checkbox("Invert +Z", &invZ)) {
        prefs.axes.signZ = invZ ? -1.f : 1.f;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "For machines whose Z zero is the top of travel (pen up = negative Z).\n"
            "The app keeps working with pen up = +Z; every Z in the outgoing\n"
            "G-code (send/export) gets its sign flipped.");

    if (m_drawFooter) {
        ImGui::Spacing();
        m_drawFooter();
    }

    if (changed)
        notifyChanged();
}

} // namespace plotter::kit
