#pragma once

#include "PlotPipeline.h"
#include "PlotterZones.h"
#include "MachinePrefs.h"
#include "PlotterExporter.h" // pulls in PlotDoc.h

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

namespace plotter::kit {

/// Pipeline + export cache for gcodeSender (and similar apps).
/// Heavy work runs on the main thread inside poll() — OF / plotproc are not
/// safe to call from a worker thread (ofLog, ofPath, registry singletons).
class GcodeExportSession {
public:
    using SourceFn             = std::function<std::string()>;
    using ExportSettingsHashFn = std::function<std::size_t()>;
    using OnPreparedFn         = std::function<void(const std::string& gcode)>;

    GcodeExportSession() = default;
    ~GcodeExportSession();

    GcodeExportSession(const GcodeExportSession&) = delete;
    GcodeExportSession& operator=(const GcodeExportSession&) = delete;

    void bind(PlotDoc* doc,
              plotter::PlotterZoneStore* zones,
              plotproc::PlotPipeline* pipeline,
              grbl::MachinePrefs* prefs,
              entt::registry* registry);

    void setSourceProvider(SourceFn fn) { m_sourceFn = std::move(fn); }
    void setExportSettingsHasher(ExportSettingsHashFn fn) { m_settingsHashFn = std::move(fn); }
    void setOnPrepared(OnPreparedFn fn) { m_onPrepared = std::move(fn); }
    void setPreambleProvider(std::function<std::string()> fn) { m_preambleFn = std::move(fn); }
    void setPostambleProvider(std::function<std::string()> fn) { m_postambleFn = std::move(fn); }
    void setRunPipeline(bool v) { m_runPipeline = v; }
    void setApplyInjections(bool v) { m_applyInjections = v; }
    bool runPipeline() const { return m_runPipeline; }
    bool applyInjections() const { return m_applyInjections; }

    void invalidate();
    bool cacheHit() const;
    /// Queue a prepare for the next poll() — safe to call from ImGui handlers.
    void requestAsync();
    void poll(); ///< Call from update(); runs queued prepare on the main thread.
    void cancelJoin();

    std::string getPostPipelineGcodeBlocking();
    const std::string& cachedGcode() const { return m_cache.postPipelineGcode; }
    bool isExporting() const { return m_exporting.load(std::memory_order_acquire); }

    /// Per-stroke touchdown / lift-off geometry (machine mm) from the last build.
    /// Valid after an onPrepared callback fires (and mirrors the cached G-code).
    const plotter::LandingSink& landings() const { return m_landings; }

    bool syncPlotDocFromSource(const std::string& text);
    plotproc::StrokeDocument buildStrokeDocFromSource(const std::string& text);

    using ReportSink = std::function<void(const plotproc::PipelineRunReport&, bool)>;
    void setReportSink(ReportSink fn) { m_reportSink = std::move(fn); }

private:
    struct ExportCache {
        std::size_t sourceHash         = 0;
        std::size_t pipelineHash       = 0;
        std::size_t exportSettingsHash = 0;
        std::string postPipelineGcode;
        bool        valid = false;
    };

    plotter::ExportOptions buildExportOptions() const;
    std::string finalizeGcodeFromStrokeDoc(plotproc::StrokeDocument& doc,
                                           plotter::ExportOptions& opts,
                                           const plotproc::PipelineRunReport* report);
    void commitCache(const std::string& gcode);
    void runQueuedPrepare();

    std::size_t hashPipelineSource() const;
    std::size_t hashPipelineConfig() const;
    std::size_t hashExportSettings() const;

    PlotDoc*                    m_doc      = nullptr;
    plotter::PlotterZoneStore*  m_zones    = nullptr;
    plotproc::PlotPipeline*     m_pipeline = nullptr;
    grbl::MachinePrefs*         m_prefs    = nullptr;
    entt::registry*             m_registry = nullptr;

    SourceFn               m_sourceFn;
    ExportSettingsHashFn   m_settingsHashFn;
    OnPreparedFn           m_onPrepared;
    std::function<std::string()> m_preambleFn;
    std::function<std::string()> m_postambleFn;
    ReportSink             m_reportSink;
    bool                   m_runPipeline     = true;
    bool                   m_applyInjections = true;

    ExportCache m_cache;
    /// Mutable so the const buildExportOptions() can bind it as the export sink.
    mutable plotter::LandingSink m_landings;

    std::atomic<bool>     m_exporting{false};
    std::atomic<uint64_t> m_exportGeneration{0};
    uint64_t              m_activeExportGeneration = 0;
    bool                  m_prepareQueued = false;
};

} // namespace plotter::kit
