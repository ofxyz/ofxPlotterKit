#pragma once

#include "MachinePrefs.h"
#include "PresetPicker.h"

#include <functional>
#include <string>

namespace plotter::kit {

/// Machine work envelope + axis invert (extracted from Controls).
class PlotterEnvelopePanel {
public:
    void setPrefs(grbl::MachinePrefs* prefs) { m_prefs = prefs; }
    void setPresets(ofkitty::PresetLibrary* presets) { m_presets = presets; }
    void setOnChanged(std::function<void()> cb) { m_onChanged = std::move(cb); }
    void setDrawFooter(std::function<void()> cb) { m_drawFooter = std::move(cb); }

    std::string name() const { return "Machine Envelope"; }
    bool        isVisible() const { return false; }
    void        setImGuiWindowTitle(std::string title) { m_imguiWindowTitle = std::move(title); }

    void draw(bool& visible);
    void drawBody();

private:
    void notifyChanged() { if (m_onChanged) m_onChanged(); }

    grbl::MachinePrefs*       m_prefs = nullptr;
    ofkitty::PresetLibrary*   m_presets = nullptr;
    std::function<void()>     m_onChanged;
    std::function<void()>     m_drawFooter;
    std::string               m_imguiWindowTitle;
};

} // namespace plotter::kit
