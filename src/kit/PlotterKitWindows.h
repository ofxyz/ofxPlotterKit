#pragma once

#include "ofxKit.h"
#include <string>
#include <type_traits>
#include <utility>

namespace plotter::kit {

/// Options for plotter::kit::registerWindow (defaults match most kit panels).
struct RegisterWindowOpts {
    bool        editModeOnly = true;
    bool        visible      = false; ///< when true, overrides Window::isVisible()
    bool        useVisibleOverride = false;
    std::string menuName;             ///< empty → Window::name()
    std::string id;                   ///< RuntimeWindow.id (stable prefs key)
};

/// Register any window object with an ofkitty::Runtime.
///
/// Requirements on Window (legacy API):
///   std::string  name()      const  — window title / menu label
///   bool         isVisible() const  — initial visibility
///   void         draw(bool& visible)— ImGui draw call
///
/// Or PlotterSerialWindow-style:
///   void drawUsbSerial(bool&); void drawConsole(bool&);
namespace detail {

template<typename>
struct always_false : std::false_type {};

template<typename T, typename = void>
struct has_legacy_runtime_window_api : std::false_type {};

template<typename T>
struct has_legacy_runtime_window_api<T, std::void_t<
    decltype(std::declval<T&>().name()),
    decltype(std::declval<T&>().isVisible()),
    decltype(std::declval<T&>().draw(std::declval<bool&>()))
>> : std::true_type {};

template<typename T, typename = void>
struct has_plotter_serial_api : std::false_type {};

template<typename T>
struct has_plotter_serial_api<T, std::void_t<
    decltype(std::declval<T&>().drawUsbSerial(std::declval<bool&>())),
    decltype(std::declval<T&>().drawConsole(std::declval<bool&>()))
>> : std::true_type {};

} // namespace detail

template<typename Window>
inline ofkitty::Runtime::RuntimeWindow* registerWindow(
    ofkitty::Runtime& runtime,
    Window&           window,
    RegisterWindowOpts opts = {})
{
    if constexpr (detail::has_legacy_runtime_window_api<Window>::value) {
        const std::string menu = opts.menuName.empty() ? window.name() : opts.menuName;
        const bool visible = opts.useVisibleOverride ? opts.visible : window.isVisible();
        return runtime.registerWindow({
            menu,
            "View",
            visible,
            opts.editModeOnly,
            [&window](bool& visible) {
                window.draw(visible);
            },
            opts.id,
        });
    } else if constexpr (detail::has_plotter_serial_api<Window>::value) {
        auto* usb = runtime.registerWindow({
            opts.menuName.empty() ? std::string("USB Serial") : opts.menuName,
            "View",
            opts.useVisibleOverride ? opts.visible : false,
            opts.editModeOnly,
            [&window](bool& visible) {
                window.drawUsbSerial(visible);
            },
            opts.id.empty() ? std::string("plotter_kit.usb_serial") : opts.id,
        });
        runtime.registerWindow({
            "Serial Console",
            "View",
            false,
            opts.editModeOnly,
            [&window](bool& visible) {
                window.drawConsole(visible);
            },
            "plotter_kit.serial_console",
        });
        return usb;
    } else {
        static_assert(detail::always_false<Window>::value,
                      "registerWindow(Window): Window must provide either "
                      "name()/isVisible()/draw(bool&) or "
                      "drawUsbSerial(bool&)/drawConsole(bool&).");
        return nullptr;
    }
}

/// Register USB Serial + Serial Console with independent menu names / ids.
template<typename SerialWindow>
inline void registerSerialWindows(
    ofkitty::Runtime& runtime,
    SerialWindow&     window,
    RegisterWindowOpts usbOpts = {},
    RegisterWindowOpts consoleOpts = {})
{
    runtime.registerWindow({
        usbOpts.menuName.empty() ? std::string("USB Serial") : usbOpts.menuName,
        "View",
        usbOpts.useVisibleOverride ? usbOpts.visible : false,
        usbOpts.editModeOnly,
        [&window](bool& visible) { window.drawUsbSerial(visible); },
        usbOpts.id.empty() ? std::string("plotter_kit.usb_serial") : usbOpts.id,
    });
    runtime.registerWindow({
        consoleOpts.menuName.empty() ? std::string("Serial Console") : consoleOpts.menuName,
        "View",
        consoleOpts.useVisibleOverride ? consoleOpts.visible : false,
        consoleOpts.editModeOnly,
        [&window](bool& visible) { window.drawConsole(visible); },
        consoleOpts.id.empty() ? std::string("plotter_kit.serial_console") : consoleOpts.id,
    });
}

} // namespace plotter::kit
