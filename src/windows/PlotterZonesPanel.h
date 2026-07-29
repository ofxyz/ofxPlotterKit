#pragma once

#include "PlotterZones.h"

#include <MachinePrefs.h>
#include <entt.hpp>

#include <functional>
#include <string>

namespace plotter::kit {

/// Reusable zone-editor panel — extracted from example-kit's GcodeGeneratorPanel.
///
/// Provides drawTargetZone(), drawZones(), and drawZoneInspector() as embeddable
/// ImGui sections, and a full standalone window via draw().
///
/// Dependencies: ofxPlotter (PlotterZones, ZoneComponents), ofxGrbl (MachinePrefs).
/// Does NOT depend on ofxKit — entity selection and the runtime registry are
/// supplied via setter/callback.
///
/// Usage without ofxKit (example-gcodeViewer pattern):
///   m_zonesPanel.setRegistry(&m_registry);
///   m_zonesPanel.setZoneStore(&m_zones);
///
/// Usage with ofxKit (example-kit pattern):
///   m_zonesPanel.setRegistry(&ofkitty::runtime().registry());
///   m_zonesPanel.setOnSelectEntity([](entt::entity e){ ofkitty::runtime().select(e); });
///   m_zonesPanel.setGetSelectedEntity([]{ return ofkitty::runtime().selected(); });
///   plotter::kit::registerWindow(runtime, m_zonesPanel);
class PlotterZonesPanel {
public:
    void setRegistry(entt::registry* reg)               { m_registry = reg; }
    void setZoneStore(PlotterZoneStore* zones)           { m_zones = zones; }

    /// Optional: provide machine prefs so zone inspector shows alignment buttons.
    void setPrefs(grbl::MachinePrefs* prefs)             { m_prefs = prefs; }

    /// Called when the draw-target zone selection or its geometry changes.
    void setOnDrawTargetChanged(std::function<void()> cb){ m_onDrawTargetChanged = std::move(cb); }

    /// Called when the user selects a zone entity (select in entity tree etc.).
    void setOnSelectEntity(std::function<void(entt::entity)> cb) { m_onSelect = std::move(cb); }

    /// Returns the currently selected entity so drawZones() can highlight it.
    void setGetSelectedEntity(std::function<entt::entity()> cb)  { m_getSelected = std::move(cb); }

    // ── Window concept ────────────────────────────────────────────────────────
    std::string name()       const { return "Zones"; }
    bool        isVisible()  const { return false; }
    void draw(bool& visible);

    /// Optional ImGui::Begin title (include ###id for ofxKit docking). Default: name().
    void setImGuiWindowTitle(std::string title) { m_imguiWindowTitle = std::move(title); }

    // ── Embeddable sections (no Begin/End) ────────────────────────────────────
    /// Draw-target picker for Controls → Paper (combo only).
    void drawTargetZonePicker();
    void drawZones();
    void drawZoneInspector(entt::entity zoneEntity = entt::null);

private:
    entt::entity createZone();
    void syncSelectedFromCallback();

    entt::registry*   m_registry = nullptr;
    PlotterZoneStore* m_zones    = nullptr;
    grbl::MachinePrefs* m_prefs  = nullptr;

    entt::entity m_selectedZone { entt::null };

    std::function<void()>               m_onDrawTargetChanged;
    std::function<void(entt::entity)>   m_onSelect;
    std::function<entt::entity()>       m_getSelected;

    std::string m_imguiWindowTitle;
};

} // namespace plotter::kit
