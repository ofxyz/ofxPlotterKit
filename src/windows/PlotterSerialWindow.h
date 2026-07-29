#pragma once

#include "ofSerial.h"
#include "ofxGrbl.h"
#include "ofxImGuiTextEdit.h"

#include <string>
#include <vector>

namespace plotter::kit {

/// USB connect, serial console, and bed envelope UI for shared GrblSender + MachinePrefs.
class PlotterSerialWindow {
public:
    PlotterSerialWindow();

    void setSender(grbl::GrblSender* sender) { sender_ = sender; }
    void setPrefs(grbl::MachinePrefs* prefs) { prefs_ = prefs; }

    /// USB port / baud / connect controls (standalone window).
    void drawUsbSerial(bool& visible);

    /// Serial console + command editor (standalone window).
    void drawConsole(bool& visible);

    /// Machine bed envelope controls (no ImGui window — embed in Bed Layout panel).
    void drawBedLayoutSection();

    /// Refresh device list. Call once at startup after prefs are loaded.
    void refreshDeviceList();

    /// Match Combo index to prefs serialDevicePath after refresh.
    void syncSelectionFromPrefs();

    void setUsbSerialWindowTitle(std::string title) { usbSerialWindowTitle_ = std::move(title); }
    void setConsoleWindowTitle(std::string title) { consoleWindowTitle_ = std::move(title); }

    /// Monospace font for embedded G-code editors (JetBrains Mono from ofxKit).
    void setEditorFont(ImFont* font) { cmdEditor_.SetFont(font); }

private:
    void savePrefs();
    void ensureSelectedPortInRange();
    void drawUsbSerialSection();
    void drawSerialConsoleSection();

    std::string usbSerialWindowTitle_;
    std::string consoleWindowTitle_;

    grbl::GrblSender* sender_ {nullptr};
    grbl::MachinePrefs* prefs_ {nullptr};

    std::vector<ofSerialDeviceInfo> deviceList_;
    int selectedPort_ {0};

    TextEditor cmdEditor_;

    std::string consoleText_;
    size_t consoleLineCount_ {0};
    bool consoleFollowTail_ {true};

    static constexpr int kNumBaudPresets = 6;
    static const int kBaudPresets[kNumBaudPresets];

    bool prefsDirty_ {false};
};

} // namespace plotter::kit
