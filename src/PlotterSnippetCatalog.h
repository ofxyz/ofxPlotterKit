#pragma once

#include "PlotterZoneComponents.h"

#include <string>
#include <vector>

namespace plotter::kit {

struct SnippetCatalogEntry {
    std::string id;
    std::string label;
    bool        isBuiltin = false;
};

/// Built-in + user G-code snippet library for injection rules.
class PlotterSnippetCatalog {
public:
    void ensureDefaults();

    /// Cached directory listing (invalidated by create/duplicate/rename).
    const std::vector<SnippetCatalogEntry>& listSnippets();
    void invalidateListCache() { m_listCacheValid = false; }

    /// File path for a catalog id (built-in or custom filename under settings/snippets/).
    std::string filePathFor(const std::string& id) const;

    std::string loadText(const std::string& id) const;
    void        saveText(const std::string& id, const std::string& text) const;

    /// Write a built-in template into @p ruleFilePath using optional zone geometry.
    void applyBuiltinTemplate(const std::string& builtinId,
                              const std::string& ruleFilePath,
                              const plotter::machine_zone_component* zone) const;

    /// Create a new custom snippet file; returns its catalog id (filename without path).
    /// The name is made unique (foo.gcode → foo_1.gcode …) so existing files are never reused.
    std::string createCustomSnippet(const std::string& basename);

    /// Copy an existing snippet file (any path) into a new custom snippet.
    /// Returns the new catalog id, or empty on failure.
    std::string duplicateSnippet(const std::string& sourcePath,
                                 const std::string& suggestedBasename);

    /// Rename a custom snippet file (builtins refused). Returns the new id, or empty on failure.
    std::string renameSnippet(const std::string& oldId, const std::string& newBasename);

    bool isBuiltinId(const std::string& id) const;

private:
    std::string snippetsDir() const;
    std::string uniqueSnippetName(const std::string& basename) const;
    void        writeDefaultFile(const std::string& filename, const std::string& text) const;

    bool                             m_defaultsEnsured = false;
    bool                             m_listCacheValid  = false;
    std::vector<SnippetCatalogEntry> m_listCache;
};

/// Rotate all X/Y (and arc I/J) coordinates in @p gcode by 90° counter-clockwise:
/// (x,y) → (-y,x). Meant for G91-relative snippets; lines whose coordinates are
/// macros (e.g. X{random(...)}) are left untouched.
std::string rotateGcodeText90CCW(const std::string& gcode);

} // namespace plotter::kit
