#include "PlotterSnippetCatalog.h"
#include "PlotterSnippetUi.h"

#include "ofFileUtils.h"
#include "ofUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace plotter::kit {

namespace {

constexpr const char* kPenPumpId         = "pen_pump.gcode";
constexpr const char* kZoneLinesId       = "zone_lines.gcode";
constexpr const char* kPaintDishCircleId = "paint_dish_circle.gcode";
constexpr const char* kGoHomeId          = "go_home.gcode";

std::string penPumpTemplate()
{
    return "; Pen pump - one up/down cycle (set Loop count on the rule to repeat)\n"
           "G0 Z{penUpZ} ; Pen Up\n"
           "G1 Z{penDownZ} ; pen down\n"
           "G0 Z{penUpZ} ; Pen Up\n";
}

std::string goHomeTemplate()
{
    return "; Go Home — Z from pen settings ({penUpZ})\n"
           "G0 Z{penUpZ} ; Pen Up\n"
           "G0 X0 Y0 ; Go Home\n";
}

/// Hatch fills relative to the detour landing point (zone position / centre).
/// Relocates once to the zone min corner (machine bed origin), then draws
/// parallel lines from the left edge without returning to centre.
/// Must use G91 — absolute X0/Y0 would jump to machine origin after a detour.
std::string zoneLinesTemplate(float w, float h, float cornerOffX, float cornerOffY)
{
    std::ostringstream os;
    os << "; Horizontal lines across zone (" << w << " x " << h
       << " mm), G91 from zone min corner (machine bed origin)\n"
       << "; Detour lands at zone centre; relocate once to min corner.\n"
       << "; {penUpZ}/{penDownZ} expand from pen settings at inject time.\n"
       << "; Optional: G0 X{random(-2,2)} Y{random(-2,2)} before the hatch to jitter.\n"
       << "G91 ; relative XY from detour position (centre)\n"
       << "G0 X" << cornerOffX << " Y" << cornerOffY << " ; centre → min corner\n"
       << "G90\n"
       << "G1 Z{penDownZ} ; pen down\n"
       << "G91\n";
    const float spacing = 5.f;
    const int   lines   = std::max(1, (int)std::floor(h / spacing) + 1);
    for (int i = 0; i < lines; ++i) {
        if (i > 0) {
            os << "G90\n"
               << "G0 Z{penUpZ} ; pen up\n"
               << "G91\n"
               << "G0 X" << -w << " Y" << spacing << " ; left edge, next line\n"
               << "G90\n"
               << "G1 Z{penDownZ} ; pen down\n"
               << "G91\n";
        }
        const float y = (float)i * spacing;
        if (i == 0 && y > 0.f)
            os << "G0 X0 Y" << y << " ; first line Y\n";
        else if (i > 0)
            os << "G0 X0 Y0 ; draw from left edge\n";
        os << "G1 X" << w << " Y0 ; draw across\n";
    }
    os << "G90\n"
       << "G0 Z{penUpZ} ; pen up\n"
       << "G90 ; restore absolute\n";
    return os.str();
}

/// Circle for paint-dish loading. Detour should land at the dish centre
/// (zone position). Path is G91-relative so it works wherever the zone sits.
/// @param w,h  the zone's INNER size (outer minus margins) - the circle must
///             stay inside the margin rect. Assumes the detour position is
///             centred; asymmetric margins can't be compensated relative-only.
std::string paintDishCircleTemplate(float w, float h)
{
    const float r = std::max(2.f, 0.5f * std::min(w, h));
    std::ostringstream os;
    os << "; Paint dish circle - radius " << r << " mm (zone inner area "
       << w << " x " << h << " mm, margins already subtracted)\n"
       << "; Detour arrival = dish centre. Re-apply template if zone size changes.\n"
       << "; {penUpZ}/{penDownZ} expand from pen settings at inject time.\n"
       << "; Optional jitter / radius macros (expanded at inject time):\n"
       << ";   G0 X{random(-2,2)} Y{random(-2,2)}   ; offset from centre\n"
       << ";   G0 X{random:r(10,12)} ... G2 I-{random:r(10,12)}  ; shared radius\n"
       << "G91 ; relative XY from dish centre\n"
       << "G0 X" << r << " Y0 ; start on +X of circle\n"
       << "G90\n"
       << "G1 Z{penDownZ} ; pen down into paint\n"
       << "G91\n"
       << "G2 X" << (-2.f * r) << " Y0 I" << -r << " J0 ; half circle (CW)\n"
       << "G2 X" << (2.f * r) << " Y0 I" << r << " J0 ; other half (CW)\n"
       << "G90\n"
       << "G0 Z{penUpZ} ; pen up\n"
       << "G91\n"
       << "G0 X" << -r << " Y0 ; return to dish centre\n"
       << "G90 ; restore absolute\n";
    return os.str();
}

} // namespace

std::string PlotterSnippetCatalog::snippetsDir() const
{
    return ofToDataPath("settings/snippets", true);
}

void PlotterSnippetCatalog::writeDefaultFile(const std::string& filename,
                                             const std::string& text) const
{
    const std::string path = snippetsDir() + "/" + filename;
    if (ofFile::doesFileExist(path)) return;
    writeSnippetFile(path, text);
}

void PlotterSnippetCatalog::ensureDefaults()
{
    if (m_defaultsEnsured) return;
    ofFilePath::createEnclosingDirectory(snippetsDir(), false, true);
    writeDefaultFile(kPenPumpId, penPumpTemplate());
    writeDefaultFile(kZoneLinesId, zoneLinesTemplate(50.f, 80.f, -25.f, -40.f));
    writeDefaultFile(kPaintDishCircleId, paintDishCircleTemplate(40.f, 40.f));
    writeDefaultFile(kGoHomeId, goHomeTemplate());
    m_defaultsEnsured = true;
}

const std::vector<SnippetCatalogEntry>& PlotterSnippetCatalog::listSnippets()
{
    if (m_listCacheValid) return m_listCache;

    ensureDefaults();

    m_listCache.clear();
    m_listCache.push_back({ kPenPumpId,         "Pen Pump",          true });
    m_listCache.push_back({ kZoneLinesId,       "Zone Lines",        true });
    m_listCache.push_back({ kPaintDishCircleId, "Paint Dish Circle", true });
    m_listCache.push_back({ kGoHomeId,          "Go Home",           true });

    ofDirectory dir(snippetsDir());
    dir.allowExt("gcode");
    dir.listDir();
    dir.sort();

    for (const auto& f : dir) {
        if (!f.isFile()) continue;
        const std::string id = f.getFileName();
        if (id == kPenPumpId || id == kZoneLinesId || id == kPaintDishCircleId
            || id == kGoHomeId)
            continue;
        SnippetCatalogEntry e;
        e.id    = id;
        e.label = id;
        e.isBuiltin = false;
        m_listCache.push_back(std::move(e));
    }
    m_listCacheValid = true;
    return m_listCache;
}

std::string PlotterSnippetCatalog::filePathFor(const std::string& id) const
{
    if (id.empty()) return {};
    return snippetsDir() + "/" + id;
}

std::string PlotterSnippetCatalog::loadText(const std::string& id) const
{
    return plotter::loadSnippetText(filePathFor(id));
}

void PlotterSnippetCatalog::saveText(const std::string& id, const std::string& text) const
{
    writeSnippetFile(filePathFor(id), text);
}

void PlotterSnippetCatalog::applyBuiltinTemplate(const std::string& builtinId,
                                                 const std::string& ruleFilePath,
                                                 const machine_zone_component* zone) const
{
    std::string text;
    if (builtinId == kPenPumpId) {
        text = penPumpTemplate();
    } else if (builtinId == kZoneLinesId) {
        const float outerW = zone ? zone->w : 50.f;
        const float outerH = zone ? zone->h : 80.f;
        const float innerW = zone ? zone->margins.innerW(zone->w) : outerW;
        const float innerH = zone ? zone->margins.innerH(zone->h) : outerH;
        const float cornerOffX = zone
            ? (zone->margins.left - outerW * 0.5f)
            : (-outerW * 0.5f);
        const float cornerOffY = zone
            ? (zone->margins.bottom - outerH * 0.5f)
            : (-outerH * 0.5f);
        text = zoneLinesTemplate(innerW, innerH, cornerOffX, cornerOffY);
    } else if (builtinId == kPaintDishCircleId) {
        // Use the inner rect so the circle respects the zone margins.
        const float w = zone ? zone->margins.innerW(zone->w) : 30.f;
        const float h = zone ? zone->margins.innerH(zone->h) : 30.f;
        text = paintDishCircleTemplate(w, h);
    } else if (builtinId == kGoHomeId) {
        text = goHomeTemplate();
    } else {
        text = loadText(builtinId);
    }
    writeSnippetFile(ruleFilePath, text);
}

std::string PlotterSnippetCatalog::uniqueSnippetName(const std::string& basename) const
{
    std::string name = basename.empty() ? std::string("snippet.gcode") : basename;
    if (name.find('.') == std::string::npos) name += ".gcode";
    if (!isBuiltinId(name) && !ofFile::doesFileExist(filePathFor(name)))
        return name;
    const std::string ext  = ofFilePath::getFileExt(name);
    const std::string stem = ext.empty()
        ? name
        : name.substr(0, name.size() - ext.size() - 1);
    for (int i = 1; i < 1000; ++i) {
        const std::string candidate = stem + "_" + std::to_string(i) + "." + ext;
        if (!isBuiltinId(candidate) && !ofFile::doesFileExist(filePathFor(candidate)))
            return candidate;
    }
    return name;
}

std::string PlotterSnippetCatalog::createCustomSnippet(const std::string& basename)
{
    ensureDefaults();
    // Always create a fresh unique file — never adopt an existing one, whose
    // contents may be a stale builtin template written for another rule.
    const std::string name = uniqueSnippetName(
        basename.empty() ? "snippet_custom.gcode" : basename);
    writeSnippetFile(filePathFor(name), "; Custom injection snippet\n");
    invalidateListCache();
    return name;
}

std::string PlotterSnippetCatalog::duplicateSnippet(const std::string& sourcePath,
                                                    const std::string& suggestedBasename)
{
    if (sourcePath.empty()) return {};
    ensureDefaults();
    const std::string text = plotter::loadSnippetText(sourcePath);

    std::string base = suggestedBasename;
    if (base.empty()) {
        base = ofFilePath::getFileName(plotter::normalizeSnippetResourcePath(sourcePath));
        const std::string ext = ofFilePath::getFileExt(base);
        if (!ext.empty())
            base = base.substr(0, base.size() - ext.size() - 1) + "_copy." + ext;
        else
            base += "_copy";
    }
    const std::string name = uniqueSnippetName(base);
    writeSnippetFile(filePathFor(name), text);
    invalidateListCache();
    return name;
}

std::string PlotterSnippetCatalog::renameSnippet(const std::string& oldId,
                                                 const std::string& newBasename)
{
    if (oldId.empty() || isBuiltinId(oldId) || newBasename.empty()) return {};
    const std::string oldPath = filePathFor(oldId);
    if (!ofFile::doesFileExist(oldPath)) return {};
    const std::string name = uniqueSnippetName(newBasename);
    if (name == oldId) return oldId;
    if (!ofFile::moveFromTo(oldPath, filePathFor(name), false, false)) return {};
    invalidateListCache();
    return name;
}

bool PlotterSnippetCatalog::isBuiltinId(const std::string& id) const
{
    return id == kPenPumpId || id == kZoneLinesId || id == kPaintDishCircleId
        || id == kGoHomeId;
}

namespace {

std::string formatCoord(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

struct GcodeWord {
    char        letter;
    std::string value;
};

/// Parse "G1 X-40 Y0" into letter/value words. Returns false on anything unexpected.
bool parseGcodeWords(const std::string& code, std::vector<GcodeWord>& out)
{
    size_t i = 0;
    while (i < code.size()) {
        const char c = code[i];
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        if (!std::isalpha((unsigned char)c)) return false;
        size_t j = i + 1;
        while (j < code.size()
               && (std::isdigit((unsigned char)code[j]) || code[j] == '.'
                   || code[j] == '-' || code[j] == '+'))
            ++j;
        out.push_back({ (char)std::toupper((unsigned char)c), code.substr(i + 1, j - i - 1) });
        i = j;
    }
    return true;
}

/// Rotate the (a,b) axis pair in-place: (v_a, v_b) -> (-v_b, v_a).
/// A missing word counts as 0 (correct for G91 relative moves).
void rotateWordPair(std::vector<GcodeWord>& words, char a, char b)
{
    int ia = -1, ib = -1;
    for (int k = 0; k < (int)words.size(); ++k) {
        if (words[(size_t)k].letter == a && ia < 0) ia = k;
        if (words[(size_t)k].letter == b && ib < 0) ib = k;
    }
    if (ia < 0 && ib < 0) return;

    const float va = ia >= 0 ? std::strtof(words[(size_t)ia].value.c_str(), nullptr) : 0.f;
    const float vb = ib >= 0 ? std::strtof(words[(size_t)ib].value.c_str(), nullptr) : 0.f;
    const std::string newA = formatCoord(-vb);
    const std::string newB = formatCoord(va);

    const int slot = ia >= 0 ? ia : ib;
    // Remove the second word of the pair (if both present), then rewrite the
    // first slot as "A<newA>" and insert "B<newB>" right after it.
    if (ia >= 0 && ib >= 0) {
        const int later = std::max(ia, ib);
        words.erase(words.begin() + later);
    }
    const int at = std::min(slot, (int)words.size() - 1);
    words[(size_t)at] = { a, newA };
    words.insert(words.begin() + at + 1, { b, newB });
}

} // namespace

std::string rotateGcodeText90CCW(const std::string& gcode)
{
    std::istringstream in(gcode);
    std::ostringstream out;
    std::string line;
    bool relative = false; // assume absolute until a G91 appears
    bool first = true;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!first) out << '\n';
        first = false;

        const size_t commentPos = line.find(';');
        const std::string code = line.substr(0, commentPos);
        const std::string comment = commentPos == std::string::npos
            ? std::string{} : line.substr(commentPos);

        std::string upper = code;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return (char)std::toupper(c); });
        auto hasMode = [&](const char* w) {
            const size_t pos = upper.find(w);
            if (pos == std::string::npos) return false;
            const size_t after = pos + std::strlen(w);
            return after >= upper.size()
                || (!std::isdigit((unsigned char)upper[after]) && upper[after] != '.');
        };
        if (hasMode("G90")) relative = false;
        if (hasMode("G91")) relative = true;

        // Only relative moves rotate cleanly; macro lines are left for the user.
        std::vector<GcodeWord> words;
        const bool hasXYIJ = upper.find_first_of("XYIJ") != std::string::npos;
        if (!relative || !hasXYIJ || code.find('{') != std::string::npos
            || !parseGcodeWords(code, words)) {
            out << line;
            continue;
        }

        rotateWordPair(words, 'X', 'Y');
        rotateWordPair(words, 'I', 'J');

        std::string rebuilt;
        for (const auto& w : words) {
            if (!rebuilt.empty()) rebuilt += ' ';
            rebuilt += w.letter;
            rebuilt += w.value;
        }
        out << rebuilt;
        if (!comment.empty()) out << ' ' << comment;
    }
    if (!gcode.empty() && gcode.back() == '\n') out << '\n';
    return out.str();
}

} // namespace plotter::kit
