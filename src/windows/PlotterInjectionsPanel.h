#pragma once

#include "PlotterZones.h"
#include "PlotterSnippetCatalog.h"
#include "PlotterSnippetUi.h"

#include <entt.hpp>
#include <functional>
#include <string>
#include <unordered_map>

namespace plotter::kit {

/// Injection rules (interval / at start / at end) + optional Go Home on end.
class PlotterInjectionsPanel {
public:
    void setRegistry(entt::registry* reg) { m_registry = reg; }
    void setZoneStore(plotter::PlotterZoneStore* zones) { m_zones = zones; }
    void setSnippetCatalog(PlotterSnippetCatalog* catalog) { m_snippetCatalog = catalog; }
    void setPenUpZ(float z) { m_penUpZ = z; }
    void setPenDownZ(float z) { m_penDownZ = z; }
    void setOnChanged(std::function<void()> cb) { m_onChanged = std::move(cb); }

    /// Optional live pen Z providers (called each draw). Overrides last setPenUpZ/DownZ.
    void setPenZProviders(std::function<float()> up, std::function<float()> down)
    {
        m_penUpFn   = std::move(up);
        m_penDownFn = std::move(down);
    }

    // ── Window concept ────────────────────────────────────────────────────────
    std::string name() const { return "Injections"; }
    bool        isVisible() const { return false; }
    void        setImGuiWindowTitle(std::string title) { m_imguiWindowTitle = std::move(title); }

    void draw(bool& visible);
    void drawBody();

private:
    void drawInjectionRules();
    void drawRuleSnippetPicker(entt::entity ruleEntity, plotter::injection_rule_component& r);
    void drawZoneCombo(const char* label, std::string& zoneId);
    void notifyChanged() { if (m_onChanged) m_onChanged(); }

    entt::registry*            m_registry = nullptr;
    plotter::PlotterZoneStore* m_zones = nullptr;
    PlotterSnippetCatalog*     m_snippetCatalog = nullptr;
    std::function<void()>      m_onChanged;
    float                      m_penUpZ   = 5.f;
    float                      m_penDownZ = 0.f;
    std::function<float()>     m_penUpFn;
    std::function<float()>     m_penDownFn;
    std::string                m_imguiWindowTitle;

    std::unordered_map<std::uintptr_t, SnippetEditorState> m_ruleSnippetEditors;
};

} // namespace plotter::kit
