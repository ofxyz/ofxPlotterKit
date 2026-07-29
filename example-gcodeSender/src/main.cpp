#include "ofMain.h"
#include "ofApp.h"
#include "ofxKit.h"

#include <cstdio>
#include <exception>

#ifdef TARGET_WIN32
#include <windows.h>
#endif

// Print the message of an uncaught C++ exception before dying — the default
// Windows fastfail abort is completely silent in the console.
static void fatalTerminateHandler()
{
    if (auto e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "[FATAL] uncaught exception: %s\n", ex.what());
        } catch (...) {
            std::fprintf(stderr, "[FATAL] uncaught non-std exception\n");
        }
        std::fflush(stderr);
    }
    std::abort();
}

int main()
{
    std::set_terminate(fatalTerminateHandler);
#ifdef TARGET_WIN32
    // drmingw crash handler: writes example-gcodeSender.RPT (stack trace) next
    // to the exe on segfault. No-op if the DLL isn't on PATH.
    LoadLibraryA("exchndl.dll");
#endif

    ofGLWindowSettings settings;
    settings.setSize(1440, 900);
    settings.windowMode = OF_WINDOW;
    auto window = ofCreateWindow(settings);
    auto app = std::make_shared<ofApp>();
    ofkitty::runtime().setDataSubdir("gcodeSender");
    ofkitty::runtime().setAppName("G-code Sender");
    ofkitty::runtime().disableBuiltInWindows();
    ofkitty::runtime().setPassthruCentralNode(true);
    ofkitty::Runtime::attach(window, app, app->registry());
    ofRunApp(window, std::move(app));
    ofRunMainLoop();
}
