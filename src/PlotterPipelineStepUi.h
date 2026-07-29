#pragma once

#include "ofJson.h"
#include "PlotProcessorDocs.h"
#include "ofxImGuiMarkdown.h"
#include "imgui.h"

#include <cctype>
#include <cstring>
#include <string>

namespace detail {

inline std::string pipelineControlId(const std::string& key)
{
    return std::string("##pipeline_") + key;
}

inline void pipelineLabelRow(const std::string& text)
{
    ImGui::TextUnformatted(text.c_str());
}

inline void pipelineFullWidthItem()
{
    ImGui::SetNextItemWidth(-1.f);
}

} // namespace detail

/// Human-readable label for a pipeline option key.
inline std::string pipelineOptionLabel(const std::string& key)
{
    static const struct { const char* key; const char* label; } kKnown[] = {
        {"allow_reverse",        "Allow reverse"},
        {"two_opt",              "Two-opt optimize"},
        {"closed_only",          "Closed paths only"},
        {"open_only",            "Open paths only"},
        {"closed_tolerance_mm",  "Closed tolerance (mm)"},
        {"min_length_mm",        "Minimum length (mm)"},
        {"max_length_mm",        "Maximum length (mm)"},
        {"tolerance_mm",         "Tolerance (mm)"},
        {"merge_tolerance_mm",   "Merge tolerance (mm)"},
        {"rebuild_tol_mm",       "Rebuild tolerance (mm)"},
        {"snap_threshold_mm",    "Snap threshold (mm)"},
        {"min_split_t",          "Minimum split (0-1)"},
        {"corner_angle",         "Corner angle (deg)"},
        {"merge_smooth",         "Merge smooth segments"},
        {"expand_arcs",          "Expand arcs"},
        {"rebuild",              "Rebuild geometry"},
        {"page_preset",          "Page preset"},
        {"page_width_mm",        "Page width (mm)"},
        {"page_height_mm",       "Page height (mm)"},
        {"margin_mm",            "Margin (mm)"},
        {"fit_to_margins",       "Fit to margins"},
        {"landscape",            "Landscape"},
        {"align",                "Horizontal align"},
        {"valign",               "Vertical align"},
        {"offset_x_mm",          "Offset X (mm)"},
        {"offset_y_mm",          "Offset Y (mm)"},
        {"angle_deg",            "Angle (deg)"},
        {"skew_x_deg",           "Skew X (deg)"},
        {"skew_y_deg",           "Skew Y (deg)"},
        {"pitch_mm",             "Grid pitch (mm)"},
        {"max_draw_mm",          "Max draw length (mm)"},
        {"margin_x_mm",          "Margin X (mm)"},
        {"margin_y_mm",          "Margin Y (mm)"},
        {"center_x_mm",          "Center X (mm)"},
        {"center_y_mm",          "Center Y (mm)"},
        {"radius_mm",            "Radius (mm)"},
        {"scale_x",              "Scale X"},
        {"scale_y",              "Scale Y"},
        {"min_points",           "Minimum points"},
        {"clockwise",            "Clockwise"},
        {"count_travel",         "Count travel"},
        {"between_strokes",      "Between strokes"},
        {"algorithm",            "Algorithm"},
        {"tension",              "Tension"},
        {"text",                 "Text"},
        {"font_size",            "Font size"},
        {"line_height",          "Line height"},
    };
    for (const auto& e : kKnown)
        if (key == e.key) return e.label;

    std::string out;
    out.reserve(key.size() + 8);
    for (size_t i = 0; i < key.size(); ++i) {
        if (key[i] == '_') {
            if (i + 3 <= key.size() && key.compare(i, 3, "_mm") == 0) {
                out += " (mm)";
                i += 2;
                continue;
            }
            if (i + 4 <= key.size() && key.compare(i, 4, "_deg") == 0) {
                out += " (deg)";
                i += 3;
                continue;
            }
            out.push_back(' ');
            continue;
        }
        if (out.empty() || out.back() == ' ')
            out.push_back((char)std::toupper((unsigned char)key[i]));
        else
            out.push_back(key[i]);
    }
    return out;
}

/// Inline pipeline-step parameter UI (Generator > G-code > Pipeline steps).
inline void mergePipelineStepOptions(ofJson& options, const ofJson& defaults)
{
    if (!defaults.is_object()) return;
    if (!options.is_object()) options = ofJson::object();
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!options.contains(it.key()))
            options[it.key()] = it.value();
    }
}

inline bool drawPipelineStringCombo(const std::string& label,
                                    const std::string& key,
                                    ofJson& options,
                                    const char* const* items,
                                    int itemCount)
{
    const std::string cur = options.value(key, items[0]);
    int sel = 0;
    for (int i = 0; i < itemCount; ++i) {
        if (cur == items[i]) { sel = i; break; }
    }

    detail::pipelineLabelRow(label);
    detail::pipelineFullWidthItem();
    bool changed = false;
    if (ImGui::Combo(detail::pipelineControlId(key).c_str(), &sel, items, itemCount)) {
        options[key] = items[sel];
        changed = true;
    }
    return changed;
}

/// Collapsible markdown help for a pipeline processor (Generator → G-code → Pipeline steps).
inline void drawPipelineProcessorHelp(const std::string& processorId)
{
    const char* md = plotproc::processorHelpMarkdown(processorId);
    if (!md || !*md) return;

    if (ImGui::TreeNodeEx("About this step", ImGuiTreeNodeFlags_None)) {
        ofxRenderMarkdown(md);
        ImGui::TreePop();
    }
    ImGui::Spacing();
}

inline bool drawPipelineStepOptions(const std::string& processorId,
                                    ofJson& options,
                                    const ofJson& defaults)
{
    drawPipelineProcessorHelp(processorId);
    ImGui::Separator();
    ImGui::Spacing();

    mergePipelineStepOptions(options, defaults);
    if (!defaults.is_object()) {
        ImGui::TextDisabled("(no options)");
        return false;
    }

    bool changed = false;

    auto drawString = [&](const std::string& key, bool multiline) {
        std::string val = options.value(key, defaults.value(key, std::string()));
        char buf[1024];
        std::memset(buf, 0, sizeof(buf));
        std::strncpy(buf, val.c_str(), sizeof(buf) - 1);
        detail::pipelineLabelRow(pipelineOptionLabel(key));
        detail::pipelineFullWidthItem();
        const ImGuiInputTextFlags flags = multiline
            ? ImGuiInputTextFlags_None
            : ImGuiInputTextFlags_EnterReturnsTrue;
        const bool edited = multiline
            ? ImGui::InputTextMultiline(
                detail::pipelineControlId(key).c_str(), buf, sizeof(buf),
                ImVec2(-1, ImGui::GetTextLineHeight() * 3.f))
            : ImGui::InputText(
                detail::pipelineControlId(key).c_str(), buf, sizeof(buf), flags);
        if (edited) {
            options[key] = std::string(buf);
            changed = true;
        }
    };

    auto drawBool = [&](const std::string& key) {
        bool v = options.value(key, defaults.value(key, false));
        detail::pipelineLabelRow(pipelineOptionLabel(key));
        if (ImGui::Checkbox(detail::pipelineControlId(key).c_str(), &v)) {
            options[key] = v;
            changed = true;
        }
    };

    auto drawInt = [&](const std::string& key) {
        int v = options.value(key, defaults.value(key, 0));
        detail::pipelineLabelRow(pipelineOptionLabel(key));
        detail::pipelineFullWidthItem();
        if (ImGui::DragInt(detail::pipelineControlId(key).c_str(), &v, 0.25f)) {
            options[key] = v;
            changed = true;
        }
    };

    auto drawFloat = [&](const std::string& key) {
        float v = options.value(key, defaults.value(key, 0.f));
        detail::pipelineLabelRow(pipelineOptionLabel(key));
        detail::pipelineFullWidthItem();
        const float step = (key.find("angle") != std::string::npos
                         || key.find("_deg") != std::string::npos) ? 1.f : 0.05f;
        const char* fmt = (key.find("min_split") != std::string::npos) ? "%.3f" : "%.4g";
        if (ImGui::DragFloat(detail::pipelineControlId(key).c_str(), &v, step, -1e6f, 1e6f, fmt)) {
            options[key] = v;
            changed = true;
        }
    };

    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        const std::string& key = it.key();
        const ofJson& defVal   = it.value();

        if (processorId == "layout") {
            if (key == "page_preset") {
                static const char* kPresets[] = {
                    "a5", "a4", "a3", "a2", "a1", "a0", "letter", "tight", "custom"
                };
                changed |= drawPipelineStringCombo(
                    "Page preset", key, options, kPresets, IM_ARRAYSIZE(kPresets));
                continue;
            }
            if (key == "align") {
                static const char* kAlign[] = { "left", "center", "right" };
                changed |= drawPipelineStringCombo(
                    "Horizontal align", key, options, kAlign, IM_ARRAYSIZE(kAlign));
                continue;
            }
            if (key == "valign") {
                static const char* kVAlign[] = { "top", "center", "bottom" };
                changed |= drawPipelineStringCombo(
                    "Vertical align", key, options, kVAlign, IM_ARRAYSIZE(kVAlign));
                continue;
            }
        }

        if (processorId == "fit_curves" && key == "algorithm") {
            static const char* kAlgos[] = { "schneider", "catmull_rom" };
            changed |= drawPipelineStringCombo(
                "Algorithm", key, options, kAlgos, IM_ARRAYSIZE(kAlgos));
            continue;
        }

        if (processorId == "text" && key == "align") {
            static const char* kAlign[] = { "left", "center", "right" };
            changed |= drawPipelineStringCombo(
                "Horizontal align", key, options, kAlign, IM_ARRAYSIZE(kAlign));
            continue;
        }

        if (defVal.is_boolean()) {
            drawBool(key);
        } else if (defVal.is_number_integer()) {
            drawInt(key);
        } else if (defVal.is_number()) {
            drawFloat(key);
        } else if (defVal.is_string()) {
            const bool multiline = (key == "text");
            drawString(key, multiline);
        }
    }

    if (defaults.empty())
        ImGui::TextDisabled("(no settings)");

    return changed;
}
