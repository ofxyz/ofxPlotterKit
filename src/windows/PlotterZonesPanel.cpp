#include "PlotterZonesPanel.h"
#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace plotter::kit {

// ── helpers ───────────────────────────────────────────────────────────────────

entt::entity PlotterZonesPanel::createZone()
{
    if (!m_registry) return entt::null;
    machine_zone_component z;
    z.zoneId = makeUniqueZoneId(*m_registry);
    z.name   = "Zone";
    if (m_prefs) {
        z.x = m_prefs->envelope.maxX - 60.f;
        z.y = m_prefs->envelope.minY + 10.f;
        z.w = 50.f;
        z.h = 80.f;
    }
    const entt::entity e = createZoneEntity(*m_registry, z);
    m_selectedZone = e;
    if (m_onSelect) m_onSelect(e);
    return e;
}

void PlotterZonesPanel::syncSelectedFromCallback()
{
    if (!m_registry || !m_getSelected) return;
    const entt::entity sel = m_getSelected();
    if (sel == entt::null) return;
    if (m_registry->all_of<machine_zone_component>(sel))
        m_selectedZone = sel;
}

// ── standalone window ─────────────────────────────────────────────────────────

void PlotterZonesPanel::draw(bool& visible)
{
    if (!ImGui::Begin(name().c_str(), &visible)) { ImGui::End(); return; }
    drawTargetZone();
    ImGui::Separator();
    drawZones();
    ImGui::End();
}

// ── embeddable sections ───────────────────────────────────────────────────────

void PlotterZonesPanel::drawTargetZone()
{
    if (!m_registry || !m_zones) {
        ImGui::TextDisabled("No zone store attached.");
        return;
    }

    const auto zoneEnts = collectZoneEntities(*m_registry);

    std::vector<std::string> nameStrs;
    nameStrs.reserve(zoneEnts.size() + 1);
    std::vector<const char*> names;
    names.reserve(zoneEnts.size() + 1);
    int cur = 0;
    for (int i = 0; i < (int)zoneEnts.size(); ++i) {
        const auto& mc = m_registry->get<machine_zone_component>(zoneEnts[i]);
        nameStrs.push_back(mc.name);
        names.push_back(nameStrs.back().c_str());
        if (mc.zoneId == m_zones->drawTargetZoneId) cur = i;
    }
    const int newIdx = (int)zoneEnts.size();
    nameStrs.emplace_back("New Zone...");
    names.push_back(nameStrs.back().c_str());

    if (ImGui::CollapsingHeader("Target Zone", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Combo("Target zone", &cur, names.data(), (int)names.size())) {
            if (cur == newIdx) {
                const entt::entity e = createZone();
                if (e != entt::null) {
                    m_zones->drawTargetZoneId =
                        m_registry->get<machine_zone_component>(e).zoneId;
                    if (m_onDrawTargetChanged) m_onDrawTargetChanged();
                }
            } else if (cur >= 0 && cur < (int)zoneEnts.size()) {
                m_zones->drawTargetZoneId =
                    m_registry->get<machine_zone_component>(zoneEnts[cur]).zoneId;
                m_selectedZone = zoneEnts[cur];
                if (m_onSelect) m_onSelect(m_selectedZone);
                if (m_onDrawTargetChanged) m_onDrawTargetChanged();
            }
        }

        const entt::entity target = findZoneEntity(*m_registry, m_zones->drawTargetZoneId);
        if (target != entt::null) {
            ImGui::PushID(m_registry->get<machine_zone_component>(target).zoneId.c_str());
            drawZoneInspector(target);
            ImGui::PopID();
        } else {
            ImGui::TextDisabled("Choose a target zone or create one with New Zone...");
        }
    }
}

void PlotterZonesPanel::drawZones()
{
    if (!m_registry || !m_zones) return;

    syncSelectedFromCallback();

    if (ImGui::Button("Add zone")) createZone();
    ImGui::SameLine();
    if (ImGui::Button("Save zones")) m_zones->save(*m_registry);

    int idx = 0;
    for (auto e : collectZoneEntities(*m_registry)) {
        const auto& z       = m_registry->get<machine_zone_component>(e);
        const bool isTarget = isDrawTargetZone(*m_zones, z);
        std::string header  = z.name + (isTarget ? " (target)" : "");
        header += "###";
        header += z.zoneId;

        ImGui::PushID(idx);
        if (ImGui::CollapsingHeader(header.c_str())) {
            if (ImGui::IsItemClicked()) {
                m_selectedZone = e;
                if (m_onSelect) m_onSelect(e);
            }
            drawZoneInspector(e);
        }
        ImGui::PopID();
        ++idx;
    }
}

void PlotterZonesPanel::drawZoneInspector(entt::entity zoneEntity)
{
    if (!m_registry || !m_zones) return;

    const entt::entity entity = (zoneEntity != entt::null)
        ? zoneEntity : m_selectedZone;

    if (entity == entt::null || !m_registry->valid(entity)
        || !m_registry->all_of<machine_zone_component>(entity))
        return;

    auto& z              = m_registry->get<machine_zone_component>(entity);
    const bool isTarget  = isDrawTargetZone(*m_zones, z);

    // Name edit
    static std::string s_editId;
    static char        s_nameBuf[128] = {};
    if (s_editId != z.zoneId) {
        s_editId = z.zoneId;
        std::strncpy(s_nameBuf, z.name.c_str(), sizeof(s_nameBuf) - 1);
        s_nameBuf[sizeof(s_nameBuf) - 1] = '\0';
    }
    ImGui::InputText("Name##zone", s_nameBuf, sizeof(s_nameBuf));
    if (ImGui::IsItemDeactivatedAfterEdit())
        z.name = s_nameBuf;

    ImGui::ColorEdit4("Colour##zone", z.color, ImGuiColorEditFlags_AlphaBar);
    ImGui::Checkbox("Show grid##zone",    &z.showGrid);
    ImGui::Checkbox("Margin guides##zone",&z.showMargins);

    // Standard paper-size presets
    {
        struct SizePreset { const char* label; float w, h; };
        static constexpr SizePreset kPresets[] = {
            { "A5 Portrait",  148.f, 210.f },
            { "A5 Landscape", 210.f, 148.f },
            { "A4 Portrait",  210.f, 297.f },
            { "A4 Landscape", 297.f, 210.f },
            { "A3 Portrait",  297.f, 420.f },
            { "A3 Landscape", 420.f, 297.f },
            { "A2 Portrait",  420.f, 594.f },
            { "A2 Landscape", 594.f, 420.f },
        };
        const char* preview = "Custom";
        for (const auto& p : kPresets) {
            if (std::abs(z.w - p.w) < 0.5f && std::abs(z.h - p.h) < 0.5f) {
                preview = p.label; break;
            }
        }
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::BeginCombo("##szpreset", preview)) {
            for (const auto& p : kPresets) {
                if (ImGui::Selectable(p.label)) {
                    z.w = p.w; z.h = p.h;
                    if (isTarget && m_onDrawTargetChanged) m_onDrawTargetChanged();
                }
            }
            ImGui::EndCombo();
        }
    }

    if (ImGui::DragFloat4("Rect X/Y/W/H##zone", &z.x, 0.5f, -500.f, 2000.f, "%.1f")) {
        if (isTarget && m_onDrawTargetChanged) m_onDrawTargetChanged();
    }
    ImGui::TextDisabled("Machine coordinates (mm)");

    if (ImGui::DragFloat4("Margin L/R/T/B mm##zone", &z.margins.left,
                          0.25f, 0.f, 200.f, "%.1f")) {
        if (isTarget && m_onDrawTargetChanged) m_onDrawTargetChanged();
    }
    ImGui::TextDisabled("Left / right / top / bottom canvas margin");

    if (m_prefs) {
        const auto& env = m_prefs->envelope;
        ImGui::Spacing();
        ImGui::TextDisabled("Align to envelope:");
        auto snap = [&](auto action){ action(); if (isTarget && m_onDrawTargetChanged) m_onDrawTargetChanged(); };
        if (ImGui::Button("Min corner"))
            snap([&]{ z.x = env.minX; z.y = env.minY; });
        ImGui::SameLine();
        if (ImGui::Button("Max corner"))
            snap([&]{ z.x = env.maxX - z.w; z.y = env.maxY - z.h; });
        ImGui::SameLine();
        if (ImGui::Button("Centre"))
            snap([&]{ z.x = env.minX + (env.spanX() - z.w) * 0.5f;
                      z.y = env.minY + (env.spanY() - z.h) * 0.5f; });
        ImGui::SameLine();
        if (ImGui::Button("Max X edge"))
            snap([&]{ z.x = env.maxX - z.w; });
    }

    ImGui::Separator();

    // Delete button
    if (ImGui::Button("Delete zone##zone")) {
        const std::string deletedId = z.zoneId;
        m_registry->destroy(entity);
        if (m_selectedZone == entity) m_selectedZone = entt::null;
        if (m_zones && m_zones->drawTargetZoneId == deletedId) {
            const auto rem = collectZoneEntities(*m_registry);
            m_zones->drawTargetZoneId = rem.empty()
                ? std::string{}
                : m_registry->get<machine_zone_component>(rem.front()).zoneId;
            if (m_onDrawTargetChanged) m_onDrawTargetChanged();
        }
    }
}

} // namespace plotter::kit
