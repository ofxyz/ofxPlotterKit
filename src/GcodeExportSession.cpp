#include "GcodeExportSession.h"

#include "ofxKit.h"

#include <sstream>

namespace plotter::kit {

GcodeExportSession::~GcodeExportSession()
{
    cancelJoin();
}

void GcodeExportSession::bind(PlotDoc* doc,
                              plotter::PlotterZoneStore* zones,
                              plotproc::PlotPipeline* pipeline,
                              grbl::MachinePrefs* prefs,
                              entt::registry* registry)
{
    m_doc = doc;
    m_zones = zones;
    m_pipeline = pipeline;
    m_prefs = prefs;
    m_registry = registry;
}

void GcodeExportSession::invalidate()
{
    m_cache.valid = false;
    m_exportGeneration.fetch_add(1, std::memory_order_relaxed);
}

bool GcodeExportSession::cacheHit() const
{
    if (!m_cache.valid || m_cache.postPipelineGcode.empty()) return false;
    return m_cache.sourceHash == hashPipelineSource()
        && m_cache.pipelineHash == hashPipelineConfig()
        && m_cache.exportSettingsHash == hashExportSettings();
}

plotter::ExportOptions GcodeExportSession::buildExportOptions() const
{
    plotter::ExportOptions opts;
    m_landings.clear();
    opts.landingSink = &m_landings;
    opts.prefs = m_prefs;
    opts.zoneRegistry = m_applyInjections ? m_registry : nullptr;
    opts.pipeline = m_pipeline;
    opts.runPipeline = false;
    opts.writeBackToPaths = false;
    if (m_preambleFn) opts.preamble = m_preambleFn();
    if (m_postambleFn) opts.postamble = m_postambleFn();
    // Boundary rules (At start / At end) need the registry even when only those fire.
    if (m_applyInjections && m_registry)
        opts.preambleRegistry = m_registry;
    return opts;
}

bool GcodeExportSession::syncPlotDocFromSource(const std::string& text)
{
    if (!m_doc || text.empty()) return false;

    PlotDoc::SvgDocument doc;
    GCodeImportOptions importOpts;
    importOpts.fitBeziers = false;
    importOpts.penDownMaxZ = (m_doc->pen.penDownZ + m_doc->pen.penUpZ) * 0.5f;
    if (!m_doc->parseGCodeDocument(text, doc, importOpts, false))
        return false;

    // false: do not seed an empty "Layer 1" — placeSvgDocument creates the
    // content layers. An empty default would otherwise stick around after import.
    m_doc->resetCanvas(/*createDefaultLayer=*/false);
    if (m_zones) m_doc->setDrawTargetSource(m_zones);
    m_doc->placeSvgDocument(doc, PlotDoc::SvgScaleMode::ActualSize);
    if (m_doc->layerOrder.empty())
        m_doc->addLayer("Layer 1");
    m_doc->rebuildFlatPaths();
    m_doc->refreshStats();
    return true;
}

plotproc::StrokeDocument GcodeExportSession::buildStrokeDocFromSource(const std::string& text)
{
    plotproc::StrokeDocument empty;
    if (!m_doc || text.empty()) return empty;

    // Prepare must not mutate the live layer stack. Previously this called
    // syncPlotDocFromSource → resetCanvas(), which recreated "Layer 1" and
    // wiped user deletes on every Update/Generate.
    PlotDoc::SvgDocument parsed;
    GCodeImportOptions importOpts;
    importOpts.fitBeziers = false;
    importOpts.penDownMaxZ = (m_doc->pen.penDownZ + m_doc->pen.penUpZ) * 0.5f;
    if (!m_doc->parseGCodeDocument(text, parsed, importOpts, false))
        return empty;

    return m_doc->strokeDocumentFromParsed(parsed, PlotDoc::SvgScaleMode::ActualSize);
}

std::string GcodeExportSession::finalizeGcodeFromStrokeDoc(plotproc::StrokeDocument& doc,
                                                           plotter::ExportOptions& opts,
                                                           const plotproc::PipelineRunReport* report)
{
    if (report) {
        opts.lastPipelineReport = *report;
        opts.hasPipelineReport = true;
    }
    opts.runPipeline = false;
    const std::string out = plotter::toGCodeFromDoc(doc, *m_doc, opts);
    if (opts.hasPipelineReport && m_reportSink)
        m_reportSink(opts.lastPipelineReport, true);
    return out;
}

void GcodeExportSession::commitCache(const std::string& gcode)
{
    m_cache.postPipelineGcode = gcode;
    m_cache.sourceHash = hashPipelineSource();
    m_cache.pipelineHash = hashPipelineConfig();
    m_cache.exportSettingsHash = hashExportSettings();
    m_cache.valid = true;
}

void GcodeExportSession::requestAsync()
{
    if (cacheHit() || m_exporting.load(std::memory_order_acquire)) return;
    if (m_exporting.exchange(true, std::memory_order_acq_rel)) return;

    m_activeExportGeneration = m_exportGeneration.load(std::memory_order_relaxed);
    m_prepareQueued = true;

    ofkitty::progress().setCancelable(true);
    ofkitty::progress().begin("Preparing G-code");
    ofkitty::progress().tickIndeterminate(
        m_runPipeline ? "Running pipeline…" : "Building toolpath…");
}

void GcodeExportSession::poll()
{
    if (m_prepareQueued)
        runQueuedPrepare();
}

void GcodeExportSession::cancelJoin()
{
    m_exportGeneration.fetch_add(1, std::memory_order_relaxed);
    m_prepareQueued = false;
    m_exporting.store(false, std::memory_order_release);
    ofkitty::progress().hide();
}

void GcodeExportSession::runQueuedPrepare()
{
    m_prepareQueued = false;

    const uint64_t gen = m_activeExportGeneration;
    auto fail = [&](const char* msg = nullptr) {
        m_exporting.store(false, std::memory_order_release);
        ofkitty::progress().hide();
        if (msg) ofLogWarning("GcodeExportSession") << msg;
    };

    if (gen != m_exportGeneration.load(std::memory_order_relaxed)
        || ofkitty::progress().cancelRequested()) {
        fail();
        return;
    }
    if (!m_doc || !m_pipeline || !m_sourceFn) {
        fail("Prepare aborted: session not bound");
        return;
    }

    const std::string source = m_sourceFn();
    if (source.empty()) {
        fail("Prepare aborted: empty source");
        return;
    }

    ofkitty::progress().tickIndeterminate("Parsing source…");
    auto doc = buildStrokeDocFromSource(source);
    if (doc.paths.empty()) {
        fail("Prepare aborted: no paths in source");
        return;
    }

    if (gen != m_exportGeneration.load(std::memory_order_relaxed)
        || ofkitty::progress().cancelRequested()) {
        fail();
        return;
    }

    plotproc::PipelineRunReport report;
    if (m_runPipeline) {
        ofkitty::progress().tickIndeterminate("Running pipeline…");
        report = m_pipeline->runWithReport(doc);
    }

    if (gen != m_exportGeneration.load(std::memory_order_relaxed)
        || ofkitty::progress().cancelRequested()) {
        fail();
        return;
    }

    ofkitty::progress().tickIndeterminate("Writing G-code…");
    ofkitty::progress().setCancelable(false);
    auto opts = buildExportOptions();
    const std::string out = finalizeGcodeFromStrokeDoc(doc, opts, &report);
    if (!out.empty()) {
        commitCache(out);
        if (m_onPrepared) m_onPrepared(out);
    }

    m_exporting.store(false, std::memory_order_release);
    ofkitty::progress().finish(out.empty() ? "Prepare failed" : "Done");
}

std::string GcodeExportSession::getPostPipelineGcodeBlocking()
{
    if (cacheHit()) return m_cache.postPipelineGcode;

    if (!m_exporting.load(std::memory_order_acquire))
        requestAsync();

    // Drain on this thread (may already be mid-queue from a prior requestAsync).
    while (m_exporting.load(std::memory_order_acquire) || m_prepareQueued)
        poll();

    if (cacheHit()) return m_cache.postPipelineGcode;

    if (!m_sourceFn || !m_doc || !m_pipeline) return {};
    const std::string source = m_sourceFn();
    if (source.empty()) return {};

    auto doc = buildStrokeDocFromSource(source);
    if (doc.paths.empty()) return source;

    plotproc::PipelineRunReport report;
    if (m_runPipeline)
        report = m_pipeline->runWithReport(doc);
    auto opts = buildExportOptions();
    const std::string out = finalizeGcodeFromStrokeDoc(doc, opts, &report);
    if (!out.empty()) {
        commitCache(out);
        if (m_onPrepared) m_onPrepared(out);
    }
    return m_cache.postPipelineGcode;
}

std::size_t GcodeExportSession::hashPipelineSource() const
{
    if (!m_sourceFn) return 0;
    return std::hash<std::string>{}(m_sourceFn());
}

std::size_t GcodeExportSession::hashPipelineConfig() const
{
    std::string key;
    key.push_back(m_runPipeline ? '1' : '0');
    key.push_back(m_applyInjections ? '1' : '0');
    key.push_back('\n');
    if (m_runPipeline && m_pipeline) {
        key.reserve(key.size() + m_pipeline->steps.size() * 64);
        for (const auto& step : m_pipeline->steps) {
            key += step.processorId;
            key.push_back(step.enabled ? '1' : '0');
            key += step.options.dump();
            key.push_back('\n');
        }
    }
    return std::hash<std::string>{}(key);
}

std::size_t GcodeExportSession::hashExportSettings() const
{
    if (m_settingsHashFn) return m_settingsHashFn();
    return 0;
}

} // namespace plotter::kit
