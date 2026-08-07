#include "PlotterInjectionsPanel.h"
#include "PlotterSnippetUi.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace plotter::kit {

namespace {

std::string snippetIdFromPath(const std::string& path)
{
    if (path.empty()) return {};
    const auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

const plotter::machine_zone_component* findZoneById(entt::registry& reg,
                                                    const std::string& zoneId)
{
    if (const entt::entity e = plotter::findZoneEntity(reg, zoneId); e != entt::null)
        return &reg.get<plotter::machine_zone_component>(e);
    return nullptr;
}

const char* whenLabel(plotter::InjectionWhen w)
{
    switch (w) {
        case plotter::InjectionWhen::AtStart: return "At start";
        case plotter::InjectionWhen::AtEnd:   return "At end";
        default:                              return "Every (mm)";
    }
}

std::string ruleHeaderLabel(const plotter::injection_rule_component& r,
                            entt::registry& reg)
{
    std::string label;
    if (!r.enabled) label += "[off] ";
    label += whenLabel(r.when);
    label += " · ";
    label += (r.mode == plotter::InjectionMode::Inline) ? "Inline" : "Detour";
    label += " · ";
    if (!r.snippetCatalogId.empty())
        label += r.snippetCatalogId;
    else if (!r.snippetResourceName.empty())
        label += snippetIdFromPath(r.snippetResourceName);
    else
        label += "(no snippet)";
    if (r.mode == plotter::InjectionMode::Detour && !r.zoneId.empty()) {
        if (const auto* z = findZoneById(reg, r.zoneId))
            label += " → " + (z->name.empty() ? r.zoneId : z->name);
    }
    if (r.when == plotter::InjectionWhen::Interval)
        label += " · " + std::to_string((int)r.intervalMm) + " mm";
    return label;
}

} // namespace

void PlotterInjectionsPanel::drawZoneCombo(const char* label, std::string& zoneId)
{
    if (!m_registry || !m_zones) return;
    const auto zoneEntities = plotter::collectZoneEntities(*m_registry);
    if (zoneEntities.empty()) {
        ImGui::TextDisabled("No zones — add zones in the Zones window.");
        return;
    }
    std::vector<std::string> nameStrs;
    nameStrs.reserve(zoneEntities.size());
    std::vector<const char*> names;
    names.reserve(zoneEntities.size());
    int cur = 0;
    for (int zi = 0; zi < (int)zoneEntities.size(); ++zi) {
        const auto& mc = m_registry->get<plotter::machine_zone_component>(zoneEntities[(size_t)zi]);
        std::string name = mc.name;
        if (plotter::isDrawTargetZone(*m_zones, mc))
            name += " (target)";
        nameStrs.push_back(std::move(name));
        names.push_back(nameStrs.back().c_str());
        if (mc.zoneId == zoneId) cur = zi;
    }
    if (ImGui::Combo(label, &cur, names.data(), (int)names.size())) {
        zoneId = m_registry->get<plotter::machine_zone_component>(
            zoneEntities[(size_t)cur]).zoneId;
        notifyChanged();
    }
}

void PlotterInjectionsPanel::draw(bool& visible)
{
    ImGui::SetNextWindowSize(ImVec2(360.f, 520.f), ImGuiCond_FirstUseEver);
    const char* title = m_imguiWindowTitle.empty()
        ? "Injections###plotter_kit.injections"
        : m_imguiWindowTitle.c_str();
    if (!ImGui::Begin(title, &visible)) {
        ImGui::End();
        return;
    }
    drawBody();
    ImGui::End();
}

void PlotterInjectionsPanel::drawBody()
{
    if (m_penUpFn) m_penUpZ = m_penUpFn();
    if (m_penDownFn) m_penDownZ = m_penDownFn();

    if (!m_registry || !m_zones) {
        ImGui::TextDisabled("No zone store attached.");
        return;
    }

    drawInjectionRules();
}

void PlotterInjectionsPanel::drawRuleSnippetPicker(entt::entity ruleEntity,
                                                   plotter::injection_rule_component& r)
{
    if (!m_snippetCatalog) {
        ImGui::TextDisabled("No snippet catalog.");
        return;
    }

    const std::uintptr_t ruleKey = (std::uintptr_t)ruleEntity;
    if (r.snippetResourceName.empty()) {
        r.snippetResourceName = snippetSettingsPath(
            "inject_" + std::to_string((unsigned long long)ruleKey) + ".gcode");
    }

    const auto& snippets = m_snippetCatalog->listSnippets();
    if (r.snippetCatalogId.empty())
        r.snippetCatalogId = snippetIdFromPath(r.snippetResourceName);

    int cur = 0;
    for (int si = 0; si < (int)snippets.size(); ++si) {
        if (snippets[(size_t)si].id == r.snippetCatalogId) {
            cur = si + 1;
            break;
        }
    }

    std::vector<const char*> labels;
    labels.push_back("(none)");
    for (const auto& s : snippets) labels.push_back(s.label.c_str());
    labels.push_back("New…");
    const int kNewIdx = (int)labels.size() - 1;

    int combo = cur;
    if (ImGui::Combo("Snippet", &combo, labels.data(), (int)labels.size())) {
        if (combo == 0) {
            r.snippetResourceName.clear();
            r.snippetCatalogId.clear();
            r.snippetEntity = entt::null;
            m_ruleSnippetEditors.erase(ruleKey);
            m_snippetEditorOpen.erase(ruleKey);
        } else if (combo == kNewIdx) {
            const std::string name = m_snippetCatalog->createCustomSnippet(
                "inject_" + std::to_string((unsigned long long)ruleKey) + ".gcode");
            r.snippetResourceName = snippetSettingsPath(name);
            r.snippetCatalogId    = name;
            r.snippetEntity       = entt::null;
            m_ruleSnippetEditors.erase(ruleKey);
            m_snippetEditorOpen.erase(ruleKey);
        } else {
            const auto& picked = snippets[(size_t)combo - 1];
            r.snippetEntity    = entt::null;
            r.snippetCatalogId = picked.id;
            m_ruleSnippetEditors.erase(ruleKey);
            m_snippetEditorOpen.erase(ruleKey);
            if (picked.isBuiltin) {
                r.snippetResourceName = snippetSettingsPath(
                    "inject_" + std::to_string((unsigned long long)ruleKey) + ".gcode");
                const auto* zone = findZoneById(*m_registry, r.zoneId);
                m_snippetCatalog->applyBuiltinTemplate(picked.id, r.snippetResourceName, zone);
            } else {
                r.snippetResourceName = snippetSettingsPath(picked.id);
            }
        }
        notifyChanged();
    }

    if (!r.snippetResourceName.empty()) {
        const bool editorOpen = m_snippetEditorOpen.count(ruleKey) > 0;
        if (ImGui::Button(editorOpen ? "Hide snippet G-code" : "Edit snippet G-code")) {
            if (editorOpen) {
                // Flush then tear down so TextEditor cost cannot linger.
                if (auto it = m_ruleSnippetEditors.find(ruleKey); it != m_ruleSnippetEditors.end()) {
                    auto& st = it->second;
                    if (!st.editKey.empty() && st.editor.GetUndoIndex() != st.savedUndoIdx) {
                        writeSnippetFile(st.editKey, st.editor.GetText());
                        notifyChanged();
                    }
                    m_ruleSnippetEditors.erase(it);
                }
                m_snippetEditorOpen.erase(ruleKey);
            } else {
                m_snippetEditorOpen.insert(ruleKey);
            }
        }

        if (editorOpen) {
            auto& editorState = m_ruleSnippetEditors[ruleKey];
            bool createdNew = false;
            auto onNewSnippet = [&]() {
                if (!m_snippetCatalog) return;
                // Flush the open buffer before switching files.
                if (!editorState.editKey.empty()
                    && editorState.editor.GetUndoIndex() != editorState.savedUndoIdx) {
                    writeSnippetFile(editorState.editKey, editorState.editor.GetText());
                }
                const std::string name = m_snippetCatalog->createCustomSnippet(
                    "inject_" + std::to_string((unsigned long long)ruleKey) + ".gcode");
                if (name.empty()) return;
                r.snippetResourceName = snippetSettingsPath(name);
                r.snippetCatalogId    = name;
                r.snippetEntity       = entt::null;
                createdNew = true; // tear down after draw — st is still in use
                notifyChanged();
            };
            if (drawSnippetEditor("##ruleSnippet", r.snippetResourceName, editorState,
                                  onNewSnippet))
                notifyChanged();
            if (createdNew) {
                m_ruleSnippetEditors.erase(ruleKey);
                m_snippetEditorOpen.insert(ruleKey);
            }
        }

        if (ImGui::SmallButton("Duplicate")) {
            const std::string newId =
                m_snippetCatalog->duplicateSnippet(r.snippetResourceName, {});
            if (!newId.empty()) {
                r.snippetCatalogId    = newId;
                r.snippetResourceName = snippetSettingsPath(newId);
                r.snippetEntity       = entt::null;
                m_ruleSnippetEditors.erase(ruleKey);
                m_snippetEditorOpen.erase(ruleKey);
                notifyChanged();
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copy this snippet to a new file and point the rule at the copy.");

        ImGui::SameLine();
        const bool builtinSelected = m_snippetCatalog->isBuiltinId(r.snippetCatalogId);
        if (builtinSelected) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Rename…"))
            ImGui::OpenPopup("##renameSnippet");
        if (builtinSelected) ImGui::EndDisabled();
        if (builtinSelected && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Built-in snippets can't be renamed — Duplicate first.");

        ImGui::SameLine();
        if (ImGui::SmallButton("Rotate 90")) {
            const std::string rotated =
                rotateGcodeText90CCW(plotter::loadSnippetText(r.snippetResourceName));
            writeSnippetFile(r.snippetResourceName, rotated);
            m_ruleSnippetEditors.erase(ruleKey); // force editor reload from file
            notifyChanged();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rotate X/Y (and arc I/J) moves 90\xc2\xb0 CCW: (x,y) \xe2\x86\x92 (-y,x).\n"
                              "Only G91 (relative) lines change; macro lines are left as-is.");

        if (ImGui::BeginPopup("##renameSnippet")) {
            static char s_renameBuf[128] = {};
            if (ImGui::IsWindowAppearing()) {
                std::strncpy(s_renameBuf, r.snippetCatalogId.c_str(), sizeof(s_renameBuf) - 1);
                s_renameBuf[sizeof(s_renameBuf) - 1] = '\0';
            }
            ImGui::SetNextItemWidth(220.f);
            const bool entered = ImGui::InputText(
                "##newname", s_renameBuf, sizeof(s_renameBuf),
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (entered || ImGui::Button("Rename")) {
                const std::string newId =
                    m_snippetCatalog->renameSnippet(r.snippetCatalogId, s_renameBuf);
                if (!newId.empty()) {
                    r.snippetCatalogId    = newId;
                    r.snippetResourceName = snippetSettingsPath(newId);
                    m_ruleSnippetEditors.erase(ruleKey);
                    m_snippetEditorOpen.erase(ruleKey);
                    notifyChanged();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    if (ImGui::InputInt("Loop count", &r.loopCount, 1, 5)) {
        r.loopCount = std::max(1, r.loopCount);
        notifyChanged();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Repeat the snippet body this many times at each injection point.");
}

void PlotterInjectionsPanel::drawInjectionRules()
{
    auto& reg = *m_registry;
    const auto zoneEntities = plotter::collectZoneEntities(reg);
    const auto ruleEntities = plotter::collectInjectionRuleEntities(reg);

    if (ImGui::Button("Add rule")) {
        plotter::injection_rule_component r;
        if (!zoneEntities.empty()) {
            const auto& mc = reg.get<plotter::machine_zone_component>(zoneEntities.front());
            r.zoneId = mc.zoneId;
        }
        const int sortOrder = (int)ruleEntities.size();
        const entt::entity e = plotter::createInjectionRuleEntity(reg, std::move(r), sortOrder);
        auto& created = reg.get<plotter::injection_rule_component>(e);
        created.snippetResourceName = snippetSettingsPath(
            "inject_" + std::to_string((unsigned long long)(uintptr_t)e) + ".gcode");
        notifyChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save rules"))
        m_zones->save(reg);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Persist rules (including order) to the zones store.");

    if (m_drawPresetFooter) {
        ImGui::Spacing();
        m_drawPresetFooter();
    }

    ImGui::TextDisabled("Drag a rule header to reorder. Order is the emission\n"
                        "sequence for At start / At end.");

    auto renumberRules = [&](const std::vector<entt::entity>& order) {
        for (int i = 0; i < (int)order.size(); ++i)
            reg.get<plotter::injection_rule_component>(order[(size_t)i]).sortOrder = i;
    };

    // Fresh list after Add (entity vector above may be stale).
    const auto rules = plotter::collectInjectionRuleEntities(reg);

    m_rulesEditor.setPayloadTag("INJECT_RULE_IDX");
    m_rulesEditor.setShowDragHandle(false); // drag the collapsing header itself
    m_rulesEditor.setShowAddRow(false);
    m_rulesEditor.setDefaultOpen(false);    // keep bodies closed until needed (FPS)
    m_rulesEditor.setFooterHint({});
    m_rulesEditor.setStepCount((int)rules.size());

    m_rulesEditor.setStepLabel([this, &reg, &rules](int i) -> std::string {
        if (i < 0 || i >= (int)rules.size()) return "Rule";
        return ruleHeaderLabel(reg.get<plotter::injection_rule_component>(rules[(size_t)i]), reg);
    });

    m_rulesEditor.setIsEnabled([this, &reg, &rules](int i) {
        if (i < 0 || i >= (int)rules.size()) return false;
        return reg.get<plotter::injection_rule_component>(rules[(size_t)i]).enabled;
    });
    m_rulesEditor.setSetEnabled([this, &reg, &rules](int i, bool on) {
        if (i < 0 || i >= (int)rules.size()) return;
        reg.get<plotter::injection_rule_component>(rules[(size_t)i]).enabled = on;
        notifyChanged();
    });

    m_rulesEditor.setOnMove([this, &reg, &rules, &renumberRules](int from, int to) {
        // `to` is already the post-erase insert index (see ChainEditor).
        if (from < 0 || from >= (int)rules.size()) return;
        auto order = rules;
        const entt::entity moved = order[(size_t)from];
        order.erase(order.begin() + from);
        to = std::clamp(to, 0, (int)order.size());
        order.insert(order.begin() + to, moved);
        renumberRules(order);
        notifyChanged();
    });

    m_rulesEditor.setOnRemove([this, &reg, &rules](int i) {
        if (i < 0 || i >= (int)rules.size()) return;
        const entt::entity e = rules[(size_t)i];
        const auto key = (std::uintptr_t)e;
        m_ruleSnippetEditors.erase(key);
        m_snippetEditorOpen.erase(key);
        if (reg.valid(e))
            reg.destroy(e);
        // Keep sortOrder dense after delete.
        const auto remaining = plotter::collectInjectionRuleEntities(reg);
        for (int ri = 0; ri < (int)remaining.size(); ++ri)
            reg.get<plotter::injection_rule_component>(remaining[(size_t)ri]).sortOrder = ri;
        notifyChanged();
    });

    m_rulesEditor.setDrawStepBody([this, &reg, &rules](int i) {
        if (i < 0 || i >= (int)rules.size()) return;
        const entt::entity ruleEntity = rules[(size_t)i];
        if (!reg.valid(ruleEntity)) return;
        auto& r = reg.get<plotter::injection_rule_component>(ruleEntity);

        int whenIdx = 0;
        if (r.when == plotter::InjectionWhen::AtStart) whenIdx = 1;
        else if (r.when == plotter::InjectionWhen::AtEnd) whenIdx = 2;
        if (ImGui::Combo("When", &whenIdx, "Every (mm)\0At start\0At end\0")) {
            r.when = whenIdx == 1 ? plotter::InjectionWhen::AtStart
                   : whenIdx == 2 ? plotter::InjectionWhen::AtEnd
                                  : plotter::InjectionWhen::Interval;
            notifyChanged();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Every (mm): along the toolpath by distance.\n"
                "At start: once before the first stroke (paint load).\n"
                "At end: once before M2 (cleanup / park).");

        int modeIdx = r.mode == plotter::InjectionMode::Inline ? 1 : 0;
        if (ImGui::Combo("Mode", &modeIdx, "Detour\0Inline\0")) {
            r.mode = modeIdx == 1 ? plotter::InjectionMode::Inline
                                  : plotter::InjectionMode::Detour;
            notifyChanged();
        }

        if (r.when == plotter::InjectionWhen::Interval) {
            ImGui::DragFloat("Every (mm)", &r.intervalMm, 5.f, 1.f, 100000.f, "%.0f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                notifyChanged();
            if (ImGui::Checkbox("Count travel", &r.countTravel))
                notifyChanged();
            if (r.mode == plotter::InjectionMode::Detour) {
                if (ImGui::Checkbox("Between strokes", &r.betweenStrokes))
                    notifyChanged();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Defer injection to the next pen lift before the interval\n"
                        "is reached, rather than cutting mid-stroke.");
            }
        }

        if (r.mode == plotter::InjectionMode::Detour) {
            drawZoneCombo("Zone", r.zoneId);
            ImGui::InputInt("Position index", &r.positionIndex);
            if (ImGui::IsItemDeactivatedAfterEdit())
                notifyChanged();
        }

        drawRuleSnippetPicker(ruleEntity, r);
    });

    m_rulesEditor.draw();

    if (rules.empty())
        ImGui::TextDisabled("No injection rules — Add rule for interval, start, or end.");
}

} // namespace plotter::kit
