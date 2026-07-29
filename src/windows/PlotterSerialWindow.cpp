#include "PlotterSerialWindow.h"

#include "imgui.h"
#include "ofMain.h"

#include <algorithm>

namespace plotter::kit {

const int PlotterSerialWindow::kBaudPresets[] = {
    9600, 19200, 38400, 57600, 115200, 250000,
};

PlotterSerialWindow::PlotterSerialWindow()
{
    cmdEditor_.SetLanguageDefinition(TextEditor::LanguageDefinitionId::Gcode);
    cmdEditor_.SetShowWhitespacesEnabled(false);
    cmdEditor_.SetLineSpacing(1.0f);
    cmdEditor_.SetText("; type G-code here\n");
}

void PlotterSerialWindow::ensureSelectedPortInRange()
{
    if (deviceList_.empty()) {
        selectedPort_ = 0;
        return;
    }
    if (selectedPort_ < 0 || selectedPort_ >= static_cast<int>(deviceList_.size())) {
        selectedPort_ = 0;
    }
}

void PlotterSerialWindow::refreshDeviceList()
{
    ofSerial tmp;
    deviceList_ = tmp.getDeviceList();

    // Sort so real USB-serial devices come first. On Linux/WSL this puts
    // /dev/ttyUSB* and /dev/ttyACM* (real USB devices) above /dev/ttyS*
    // (virtual legacy COM stubs that almost never connect to anything).
    auto rank = [](const std::string& path) {
        if (path.find("ttyUSB") != std::string::npos) return 0;
        if (path.find("ttyACM") != std::string::npos) return 0;
        if (path.find("cu.usbserial") != std::string::npos) return 0; // macOS
        if (path.find("cu.usbmodem") != std::string::npos) return 0; // macOS
        if (path.find("COM") != std::string::npos) return 0;          // Windows
        if (path.find("ttyS") != std::string::npos) return 2;         // virtual / legacy
        return 1;
    };
    std::sort(deviceList_.begin(), deviceList_.end(),
        [&](ofSerialDeviceInfo a, ofSerialDeviceInfo b) {
            return rank(a.getDevicePath()) < rank(b.getDevicePath());
        });

    ensureSelectedPortInRange();
}

void PlotterSerialWindow::syncSelectionFromPrefs()
{
    if (!prefs_ || prefs_->serialDevicePath.empty()) {
        return;
    }
    for (int i = 0; i < (int)deviceList_.size(); ++i) {
        if (deviceList_[i].getDevicePath() == prefs_->serialDevicePath) {
            selectedPort_ = i;
            break;
        }
    }
    ensureSelectedPortInRange();
}

void PlotterSerialWindow::savePrefs()
{
    if (!prefs_) {
        return;
    }
    std::string path = ofToDataPath(grbl::MachinePrefs::defaultRelativePath(), true);
    prefs_->save(path);
    prefsDirty_ = false;
}

void PlotterSerialWindow::drawUsbSerial(bool& visible)
{
    if (!visible) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(340, 320), ImGuiCond_FirstUseEver);

    const char* winTitle = usbSerialWindowTitle_.empty()
        ? "USB Serial"
        : usbSerialWindowTitle_.c_str();
    if (ImGui::Begin(winTitle, &visible)) {
        drawUsbSerialSection();
    }
    ImGui::End();
}

void PlotterSerialWindow::drawUsbSerialSection()
{
            if (ImGui::SmallButton("Refresh ports")) {
                refreshDeviceList();
            }

            const bool simulation = sender_ && sender_->isSimulationMode();
            const bool usbConnected = sender_ && sender_->isUsbConnected();
            const bool connected = sender_ && sender_->isConnected();

            if (simulation) {
                ImGui::TextColored(ImVec4(1.f, 0.85f, 0.35f, 1.f),
                    "Bench simulation: USB disabled (synthetic ok)");
            }

            if (deviceList_.empty()) {
                ImGui::TextDisabled("No serial ports found");
            } else {
                ensureSelectedPortInRange();
                // getDeviceName() returns std::string by value — keep copies alive while Combo uses c_str().
                std::vector<std::string> labels;
                labels.reserve(deviceList_.size());
                std::vector<const char*> names;
                names.reserve(deviceList_.size());
                for (auto& d : deviceList_) {
                    labels.push_back(d.getDeviceName());
                    names.push_back(labels.back().c_str());
                }
                ImGui::Combo("Port", &selectedPort_, names.data(), (int)names.size());
            }

            if (prefs_) {
                if (ImGui::InputInt("Baud rate", &prefs_->baudRate, 100, 1000)) {
                    prefsDirty_ = true;
                }
            }

            ImGui::TextDisabled("Presets:");
            for (int i = 0; i < kNumBaudPresets; ++i) {
                ImGui::SameLine();
                if (ImGui::SmallButton(ofToString(kBaudPresets[i]).c_str()) && prefs_) {
                    prefs_->baudRate = kBaudPresets[i];
                    prefsDirty_ = true;
                }
            }

            ImGui::Separator();

            bool simToggle = simulation;
            if (sender_ && ImGui::Checkbox("Bench simulation (no USB)", &simToggle)) {
                sender_->setSimulationMode(simToggle);
            }

            ImGui::Separator();

            if (!connected && !simulation) {
                if (ImGui::Button("Connect", ImVec2(-1, 0))) {
                    if (sender_ && prefs_ && !deviceList_.empty()
                        && selectedPort_ < (int)deviceList_.size()) {
                        prefs_->serialDevicePath = deviceList_[selectedPort_].getDevicePath();
                        prefs_->baudRate = std::max(9600, prefs_->baudRate);
                        bool ok = sender_->connectSerial(deviceList_[selectedPort_].getDevicePath(),
                            prefs_->baudRate);
                        if (ok) {
                            savePrefs();
                        }
                    }
                }
            } else if (usbConnected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.25f, 0.25f, 1.f));
                if (ImGui::Button("Disconnect USB", ImVec2(-1, 0))) {
                    if (sender_) {
                        sender_->disconnectSerial();
                    }
                    savePrefs();
                }
                ImGui::PopStyleColor();

                ImGui::TextColored(ImVec4(0.5f, 1.f, 0.55f, 1.f), "USB connected");
            } else if (simulation) {
                ImGui::TextDisabled("Uncheck simulation to use USB");
            }
}

void PlotterSerialWindow::drawSerialConsoleSection()
{
    if (!sender_) {
        return;
    }
            auto& lines = sender_->consoleLines();
            float h = std::max(120.f, ImGui::GetContentRegionAvail().y * 0.35f);

            ImGui::Checkbox("Auto-scroll", &consoleFollowTail_);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Follow new console output. Scroll up to pause.");
            }

            const bool hadNewLines = lines.size() > consoleLineCount_;
            if (lines.size() != consoleLineCount_) {
                consoleText_.clear();
                consoleText_.reserve(lines.size() * 64);
                for (const auto& line : lines) {
                    consoleText_.append(line);
                    consoleText_.push_back('\n');
                }
                consoleLineCount_ = lines.size();
            }

            ImGui::BeginChild("##GrblSerialConsole", ImVec2(-1, h), true,
                ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(consoleText_.c_str(),
                consoleText_.c_str() + consoleText_.size());

            if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.f) {
                consoleFollowTail_ = false;
            }
            if (!hadNewLines && consoleFollowTail_
                && ImGui::GetScrollMaxY() > 0.f
                && ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 2.f) {
                consoleFollowTail_ = false;
            }

            if (consoleFollowTail_) {
                ImGui::SetScrollHereY(1.0f);
            } else {
                const float maxScroll = ImGui::GetScrollMaxY();
                if (maxScroll <= 0.f || ImGui::GetScrollY() >= maxScroll - 2.f) {
                    consoleFollowTail_ = true;
                }
            }

            ImGui::EndChild();

            if (ImGui::Button("Clear console", ImVec2(-1, 0))) {
                sender_->clearConsole();
                consoleText_.clear();
                consoleLineCount_ = 0;
                consoleFollowTail_ = true;
            }

            if (sender_->isUsbConnected()) {
                const float thirdW = (ImGui::GetContentRegionAvail().x
                    - ImGui::GetStyle().ItemSpacing.x * 2.f) / 3.f;
                if (ImGui::Button("Unlock ($X)", ImVec2(thirdW, 0))) {
                    sender_->sendUnlockAlarm();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Clear a GRBL alarm lockout after a limit/homing fault.");
                }
                ImGui::SameLine();
                if (ImGui::Button("Status (?)", ImVec2(thirdW, 0))) {
                    sender_->sendRealtimeStatusQuery();
                    sender_->logStatusReports = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Query position/state once (logged to console).");
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset wait", ImVec2(thirdW, 0))) {
                    sender_->resetWaitState();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Clear a stuck ok-wait so new commands can be sent.");
                }
            }

            // ---- Command editor ---------------------------------------------
            ImGui::Separator();

            // Calculate height: ~6 lines or whatever is left, min 80px.
            const float editorH = std::max(80.f, std::min(
                ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 4.f,
                ImGui::GetTextLineHeightWithSpacing() * 8.f));

            cmdEditor_.Render("##grblcmd",
                ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows),
                ImVec2(-1.f, editorH));

            // Ctrl+Enter sends without clicking the button.
            const bool ctrlEnter = ImGui::IsItemFocused()
                && ImGui::IsKeyPressed(ImGuiKey_Enter, false)
                && ImGui::GetIO().KeyCtrl;

            const float halfW = (ImGui::GetContentRegionAvail().x
                - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

            if (ImGui::Button("Clear##cmdclear", ImVec2(halfW, 0))) {
                cmdEditor_.SetText("");
            }
            ImGui::SameLine();
            const bool canSend = sender_ && sender_->isConnected();
            if (!canSend) ImGui::BeginDisabled();
            if (ctrlEnter || ImGui::Button("Send  [Ctrl+Enter]", ImVec2(halfW, 0))) {
                sender_->enqueueGCodeBlock(cmdEditor_.GetText());
                cmdEditor_.SetText("");
            }
            if (!canSend) ImGui::EndDisabled();
}

void PlotterSerialWindow::drawBedLayoutSection()
{
    if (!prefs_) {
        return;
    }
            ImGui::TextWrapped(
                "Work envelope on the machine bed.");

            static const char* kBedPresetLabels[] = {
                "Custom",
                "A5 plotter (148x210)",
                "A4 plotter (210x297)",
                "A3 plotter (420x297)",
                "A2 plotter (594x420)",
                "A1 plotter (841x594)",
                "A0 plotter (1189x841)",
            };
            static int bedPresetSel = 0;
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::Combo("##bed_preset", &bedPresetSel, kBedPresetLabels, IM_ARRAYSIZE(kBedPresetLabels))) {
                if (bedPresetSel == 1) {
                    prefs_->envelope.minX = 0.f; prefs_->envelope.minY = 0.f; prefs_->envelope.minZ = -10.f;
                    prefs_->envelope.maxX = 148.f; prefs_->envelope.maxY = 210.f; prefs_->envelope.maxZ = 40.f;
                    prefsDirty_ = true;
                } else if (bedPresetSel == 2) {
                    prefs_->envelope.minX = 0.f; prefs_->envelope.minY = 0.f; prefs_->envelope.minZ = -10.f;
                    prefs_->envelope.maxX = 210.f; prefs_->envelope.maxY = 297.f; prefs_->envelope.maxZ = 40.f;
                    prefsDirty_ = true;
                } else if (bedPresetSel == 3) {
                    prefs_->envelope.minX = 0.f; prefs_->envelope.minY = 0.f; prefs_->envelope.minZ = -10.f;
                    prefs_->envelope.maxX = 420.f; prefs_->envelope.maxY = 297.f; prefs_->envelope.maxZ = 40.f;
                    prefsDirty_ = true;
                } else if (bedPresetSel == 4) {
                    prefs_->envelope.minX = 0.f; prefs_->envelope.minY = 0.f; prefs_->envelope.minZ = -10.f;
                    prefs_->envelope.maxX = 594.f; prefs_->envelope.maxY = 420.f; prefs_->envelope.maxZ = 40.f;
                    prefsDirty_ = true;
                } else if (bedPresetSel == 5) {
                    prefs_->envelope.minX = 0.f; prefs_->envelope.minY = 0.f; prefs_->envelope.minZ = -10.f;
                    prefs_->envelope.maxX = 841.f; prefs_->envelope.maxY = 594.f; prefs_->envelope.maxZ = 40.f;
                    prefsDirty_ = true;
                } else if (bedPresetSel == 6) {
                    prefs_->envelope.minX = 0.f; prefs_->envelope.minY = 0.f; prefs_->envelope.minZ = -10.f;
                    prefs_->envelope.maxX = 1189.f; prefs_->envelope.maxY = 841.f; prefs_->envelope.maxZ = 40.f;
                    prefsDirty_ = true;
                }
                bedPresetSel = 0;
            }

            if (ImGui::DragFloat3("Min X/Y/Z", &prefs_->envelope.minX, 0.5f, -500.f, 500.f, "%.2f")) {
                prefsDirty_ = true;
            }
            if (ImGui::DragFloat3("Max X/Y/Z", &prefs_->envelope.maxX, 0.5f, -500.f, 500.f, "%.2f")) {
                prefsDirty_ = true;
            }

            if (prefsDirty_ && ImGui::Button("Save settings to disk")) {
                savePrefs();
            }
}

void PlotterSerialWindow::drawConsole(bool& visible)
{
    if (!visible) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);

    const char* winTitle = consoleWindowTitle_.empty()
        ? "Serial Console"
        : consoleWindowTitle_.c_str();
    if (ImGui::Begin(winTitle, &visible)) {
        drawSerialConsoleSection();
    }
    ImGui::End();
}

} // namespace plotter::kit
