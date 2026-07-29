#pragma once

namespace plotter::kit {

/// ImGui::Begin titles must include ###id matching registerWindow(..., id) for docking.

constexpr const char* kWinIdJogControl     = "plotter_kit.jog";
constexpr const char* kWinIdUsbSerial      = "plotter_kit.usb_serial";
constexpr const char* kWinIdSerialConsole  = "plotter_kit.serial_console";
constexpr const char* kWinIdBedLayout      = "plotter_kit.bed_layout";

constexpr const char* kWinIdLayers         = "plotter_kit.layers";
constexpr const char* kWinIdGcodeGen       = "plotter_kit.gcode_gen";
constexpr const char* kWinIdSwatches       = "plotter_kit.swatches";
constexpr const char* kWinIdPlayback       = "plotter_kit.playback";
constexpr const char* kWinIdResources      = "plotter_kit.resources";
constexpr const char* kWinIdPrintPreview   = "plotter_kit.print_preview";
constexpr const char* kWinIdPipeline       = "plotter_kit.pipeline";
constexpr const char* kWinIdInjections     = "plotter_kit.injections";

constexpr const char* kImGuiTitleJogControl     = "Jog Control###plotter_kit.jog";
constexpr const char* kImGuiTitleUsbSerial      = "USB Serial###plotter_kit.usb_serial";
constexpr const char* kImGuiTitleSerialConsole  = "Serial Console###plotter_kit.serial_console";
constexpr const char* kImGuiTitleBedLayout     = "Bed Layout###plotter_kit.bed_layout";

constexpr const char* kImGuiTitlePrintPreview  = "Print Preview###plotter_kit.print_preview";
constexpr const char* kImGuiTitleLayers        = "Layers###plotter_kit.layers";
constexpr const char* kImGuiTitleGcodeGen      = "Generator###plotter_kit.gcode_gen";
constexpr const char* kImGuiTitleSwatches      = "Swatches###plotter_kit.swatches";
constexpr const char* kImGuiTitlePlayback      = "Playback###plotter_kit.playback";
constexpr const char* kImGuiTitleResources   = "Resources###plotter_kit.resources";
constexpr const char* kImGuiTitlePipeline    = "Pipeline###plotter_kit.pipeline";
constexpr const char* kImGuiTitleInjections  = "Injections###plotter_kit.injections";

/// View menu / setWindowVisible keys (short names).
constexpr const char* kMenuNameJogControl     = "Jog Control";
constexpr const char* kMenuNameUsbSerial      = "USB Serial";
constexpr const char* kMenuNameSerialConsole = "Serial Console";
constexpr const char* kMenuNameBedLayout     = "Bed Layout";
constexpr const char* kMenuNameLayers        = "Layers";
constexpr const char* kMenuNameResources     = "Resources";
constexpr const char* kMenuNameSwatches      = "Swatches";
constexpr const char* kMenuNameGenerator     = "Generator";
constexpr const char* kMenuNamePlayback      = "Playback";
constexpr const char* kMenuNamePrintPreview  = "Print Preview";
constexpr const char* kMenuNamePipeline      = "Pipeline";
constexpr const char* kMenuNameInjections    = "Injections";

} // namespace plotter::kit
