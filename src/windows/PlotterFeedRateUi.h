#pragma once

#include "GrblSender.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace plotter::kit {

/// Feed-rate override UI shared by jog + send panels (GRBL realtime 0x90–0x94).
inline void drawPlotFeedRateControl(grbl::GrblSender* sender,
                                    float referenceFeedMmMin,
                                    const char* idSuffix = "")
{
    if (!sender) return;

    referenceFeedMmMin = std::max(1.f, referenceFeedMmMin);
    const int uiMaxPct = grbl::GrblSender::kGrblMaxFeedOverridePercent;
    const int uiMinPct = grbl::GrblSender::kGrblMinFeedOverridePercent;

    int pct = sender->feedRatePercent();
    float feedMmMin = referenceFeedMmMin * pct / 100.f;

    ImGui::PushID(idSuffix);

    const float preSW = ImGui::GetFontSize() * 3.2f;
    const float preSH = ImGui::GetFrameHeight();
    auto speedBtn = [&](const char* lbl, int btnPct) {
        const bool active = pct == btnPct;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.70f, 1.f));
        if (ImGui::Button(lbl, ImVec2(preSW, preSH))) {
            sender->setFeedRatePercent(btnPct);
        }
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    speedBtn("25%", 25);
    speedBtn("50%", 50);
    speedBtn("100%", 100);
    speedBtn("200%", 200);
    ImGui::BeginDisabled();
    ImGui::Button("500%", ImVec2(preSW, preSH));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("GRBL realtime feed override is limited to 200%%.");
    ImGui::NewLine();

    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::DragFloat("##feedmm", &feedMmMin, referenceFeedMmMin * 0.02f,
            referenceFeedMmMin * uiMinPct / 100.f,
            referenceFeedMmMin * uiMaxPct / 100.f, "Draw feed %.0f mm/min")) {
        const int newPct = (int)std::lround(feedMmMin / referenceFeedMmMin * 100.f);
        sender->setFeedRatePercent(newPct);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Target draw feed from G-code.\n"
            "GRBL: realtime feed override (0x90–0x94) — applies immediately,\n"
            "including motion already in the planner. Requires GRBL 1.1+.\n"
            "Range 10–200%%.");
    }

    float pctF = (float)sender->feedRatePercent();
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::SliderFloat("##feedpct", &pctF, (float)uiMinPct, (float)uiMaxPct, "Override %.0f%%",
            ImGuiSliderFlags_Logarithmic)) {
        sender->setFeedRatePercent((int)std::lround(pctF));
    }

    pct = sender->feedRatePercent();
    feedMmMin = referenceFeedMmMin * pct / 100.f;
    if (pct != 100) {
        ImGui::TextColored(ImVec4(1.f, 0.75f, 0.35f, 1.f),
            "Override active: %d%%  (click 100%% for programmed speed)", pct);
    }
    ImGui::TextDisabled("Draw F ref: %.0f mm/min  ·  effective %.0f mm/min",
        referenceFeedMmMin, feedMmMin);
    ImGui::TextDisabled("GRBL realtime feed override (%d%%)", pct);

    ImGui::PopID();
}

inline float effectiveFeedMmMin(grbl::GrblSender* sender, float referenceFeedMmMin)
{
    if (!sender) return referenceFeedMmMin;
    return referenceFeedMmMin * sender->feedRatePercent() / 100.f;
}

} // namespace plotter::kit
