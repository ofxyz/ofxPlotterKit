#pragma once

#include <functional>
#include <string>

namespace plotter::kit {

/// Toolpath playback transport (playhead + scrub) for View → Playback.
/// Host owns path geometry / G-code editor sync via providers and callbacks.
class PlotterPlaybackPanel {
public:
    /// Number of flat toolpaths (0 = empty / not ready).
    void setPathCountProvider(std::function<int()> fn) { m_pathCountFn = std::move(fn); }

    /// G-code editor line count for status + sync mapping.
    void setGcodeLineCountProvider(std::function<int()> fn) { m_gcodeLineCountFn = std::move(fn); }

    /// Fired when the user (or auto-play) changes the playhead; host syncs the editor.
    void setOnPlaybackChanged(std::function<void(float playback01)> fn) {
        m_onPlaybackChanged = std::move(fn);
    }

    /// Non-empty message shows a progress bar instead of transport (e.g. "Generating…").
    void setBusyMessageProvider(std::function<std::string()> fn) { m_busyFn = std::move(fn); }

    /// Estimated job duration in seconds for auto-play (from PlotDoc::estimatedTime).
    void setEstimatedDurationProvider(std::function<float()> fn) {
        m_estDurationFn = std::move(fn);
    }

    float playback() const { return m_playback; }
    void  setPlayback(float t);
    bool  playing() const { return m_playing; }
    void  setPlaying(bool p) { m_playing = p; }
    float speed() const { return m_speed; }
    void  setSpeed(float s);

    /// Advance auto-play; call once per frame from the host update().
    void update(float dt);

    // ── Window concept ────────────────────────────────────────────────────────
    std::string name() const { return "Playback"; }
    bool        isVisible() const { return false; }
    void        setImGuiWindowTitle(std::string title) { m_imguiWindowTitle = std::move(title); }

    void draw(bool& visible);
    void drawBody();

private:
    void notifyChanged();

    std::function<int()>                 m_pathCountFn;
    std::function<int()>                 m_gcodeLineCountFn;
    std::function<void(float)>           m_onPlaybackChanged;
    std::function<std::string()>         m_busyFn;
    std::function<float()>               m_estDurationFn;

    float       m_playback = 1.f;
    bool        m_playing  = false;
    float       m_speed    = 1.f;
    std::string m_imguiWindowTitle;
};

} // namespace plotter::kit
