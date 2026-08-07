#pragma once

#include "PlotDoc.h"
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

    /// Optional: provide machine prefs so zone inspector shows alignment buttons
    /// and cropmarks can map zone-local → paper-local correctly.
    void setPrefs(grbl::MachinePrefs* prefs)             { m_prefs = prefs; }

    /// Optional PlotDoc for Generate Cropmarks (writes a paths layer).
    void setPlotDoc(PlotDoc* doc)                        { m_plotDoc = doc; }

    /// Default stroke thickness (mm) when opening the cropmarks modal.
    void setDefaultPenWidthMm(float mm)                  { m_defaultPenWidthMm = mm; }

    /// Called after cropmarks are written into a layer (host: dirty + preview).
    void setOnCropmarksGenerated(std::function<void(entt::entity layer)> cb)
    {
        m_onCropmarksGenerated = std::move(cb);
    }

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

    /// Submit the cropmarks popup (call from the same ImGui window that hosts
    /// the inspector if you embed drawZoneInspector outside drawZones()).
    void drawCropmarksModal();

private:
    entt::entity createZone();
    void syncSelectedFromCallback();
    void openCropmarksModal(entt::entity zoneEntity);

    entt::registry*   m_registry = nullptr;
    PlotterZoneStore* m_zones    = nullptr;
    grbl::MachinePrefs* m_prefs  = nullptr;
    PlotDoc*          m_plotDoc  = nullptr;
    float             m_defaultPenWidthMm = 0.3f;

    entt::entity m_selectedZone { entt::null };

    std::function<void()>               m_onDrawTargetChanged;
    std::function<void(entt::entity)>   m_onSelect;
    std::function<entt::entity()>       m_getSelected;
    std::function<void(entt::entity)>   m_onCropmarksGenerated;

    std::string m_imguiWindowTitle;

    // Cropmarks modal state
    bool         m_cropmarksModalOpen = false;
    entt::entity m_cropmarksZone      { entt::null };
    float        m_cmLength           = 10.f;
    float        m_cmInset            = 5.f;
    float        m_cmThickness        = 0.3f;
    float        m_cmColor[4]         = { 0.f, 0.f, 0.f, 1.f };
    bool         m_cmUseZoneMargins   = true;
    ZoneMarginsMM m_cmMargins;
    int          m_cmLayerCombo       = 0; ///< 0 = New layer, else index into layer list + 1
    std::string  m_cmStatus;
};

} // namespace plotter::kit
