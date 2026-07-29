#include "PlotterPlaybackPanel.h"

#include "PlotterGcodeSync.h"
#include "IconsFontAwesome5.h"
#include "ImFonts.h"
#include "imgui.h"

#include <algorithm>

namespace plotter::kit {

void PlotterPlaybackPanel::setPlayback(float t)
{
    m_playback = std::clamp(t, 0.f, 1.f);
}

void PlotterPlaybackPanel::setSpeed(float s)
{
    m_speed = std::max(0.1f, s);
}

void PlotterPlaybackPanel::notifyChanged()
{
    if (m_onPlaybackChanged)
        m_onPlaybackChanged(m_playback);
}

void PlotterPlaybackPanel::update(float dt)
{
    if (!m_playing) return;
    const int paths = m_pathCountFn ? m_pathCountFn() : 0;
    if (paths <= 0) return;
    if (m_busyFn && !m_busyFn().empty()) return;

    const float est = m_estDurationFn ? std::max(1.f, m_estDurationFn()) : 1.f;
    m_playback += (dt / est) * m_speed;
    if (m_playback >= 1.f) {
        m_playback = 1.f;
        m_playing  = false;
    }
    notifyChanged();
}

void PlotterPlaybackPanel::draw(bool& visible)
{
    ImGui::SetNextWindowSize(ImVec2(440.f, 0.f), ImGuiCond_FirstUseEver);
    const char* title = m_imguiWindowTitle.empty()
        ? "Playback###plotter_kit.playback"
        : m_imguiWindowTitle.c_str();
    if (!ImGui::Begin(title, &visible)) {
        ImGui::End();
        return;
    }
    drawBody();
    ImGui::End();
}

void PlotterPlaybackPanel::drawBody()
{
    if (m_busyFn) {
        const std::string busy = m_busyFn();
        if (!busy.empty()) {
            ImGui::ProgressBar(-1.f, ImVec2(-1.f, 0.f), busy.c_str());
            return;
        }
    }

    const int totalPaths = m_pathCountFn ? m_pathCountFn() : 0;
    if (totalPaths <= 0) {
        ImGui::TextDisabled("Generate toolpaths first (Generator → Toolpaths).");
        return;
    }

    int currentPath = (int)(m_playback * totalPaths);
    currentPath     = std::clamp(currentPath, 0, totalPaths);
    const int gcodeLines = m_gcodeLineCountFn ? m_gcodeLineCountFn() : 0;
    const int gcodeLine  = plotterGcodeSync::playbackToLine(m_playback, gcodeLines);

    if (ImFonts::IconButton(ICON_FA_STEP_BACKWARD, "##pb_start")) {
        m_playback = 0.f;
        m_playing  = false;
        notifyChanged();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start");
    ImGui::SameLine();
    if (ImFonts::IconButton(ICON_FA_BACKWARD, "##pb_prev")) {
        m_playback = std::max(0.f, m_playback - 1.f / std::max(1, totalPaths));
        m_playing  = false;
        notifyChanged();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous path");
    ImGui::SameLine();
    if (ImFonts::IconButton(m_playing ? ICON_FA_PAUSE : ICON_FA_PLAY, "##pb_play")) {
        m_playing = !m_playing;
        if (m_playing && m_playback >= 0.999f)
            m_playback = 0.f;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Animate the red playhead along toolpaths");
    ImGui::SameLine();
    if (ImFonts::IconButton(ICON_FA_FORWARD, "##pb_next")) {
        m_playback = std::min(1.f, m_playback + 1.f / std::max(1, totalPaths));
        m_playing  = false;
        notifyChanged();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next path");
    ImGui::SameLine();
    if (ImFonts::IconButton(ICON_FA_STEP_FORWARD, "##pb_end")) {
        m_playback = 1.f;
        m_playing  = false;
        notifyChanged();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("End");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.f);
    if (ImGui::DragFloat("Speed##playback", &m_speed, 0.05f, 0.1f, 8.f, "%.1fx"))
        m_speed = std::max(0.1f, m_speed);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Playback speed multiplier");

    ImGui::TextDisabled("G-code line %d/%d  ·  Path %d/%d  ·  red tip in preview",
                        gcodeLine + 1, gcodeLines, currentPath, totalPaths);

    float pct = m_playback * 100.f;
    ImGui::PushItemWidth(-1.f);
    if (ImGui::SliderFloat("Play head##playback_scrub", &pct, 0.f, 100.f, "%.1f%%")) {
        m_playback = pct / 100.f;
        m_playing  = false;
        notifyChanged();
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Scrub the plot play head. Syncs to the Code Editor highlight.\n"
            "Cyan crosshair = live GRBL position when connected.");
}

} // namespace plotter::kit
