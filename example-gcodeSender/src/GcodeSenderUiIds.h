#pragma once

#include "IconsFontAwesome5.h"

/// ImGui::Begin titles (include ###id for stable docking with ofxKit Runtime).
namespace gcodeSender {

constexpr const char* kControlsWindow = ICON_FA_SLIDERS_H " Controls###gcode_sender_controls";
constexpr const char* kEnvelopeWindow = ICON_FA_EXPAND " Machine Envelope###gcode_sender_envelope";
constexpr const char* kGcodeWindow    = ICON_FA_CODE " G-code###gcode_sender_code";
constexpr const char* kZonesWindow    = ICON_FA_MAP " Zones###gcode_sender_zones";
constexpr const char* kLayersWindow   = ICON_FA_OBJECT_GROUP " Layers###gcode_sender_layers";

} // namespace gcodeSender
