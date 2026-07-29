#pragma once

#include "ofMain.h"

#include "imgui.h"
#include "ofxImGuiTextEdit.h"
#include "PlotterGCodeInjector.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>

namespace plotter::kit {

/// Data-relative snippet path for components / JSON (`settings/snippets/foo.gcode`).
inline std::string snippetSettingsPath(const std::string& filename)
{
    return plotter::normalizeSnippetResourcePath(filename);
}

inline void writeSnippetFile(const std::string& path, const std::string& text)
{
    if (path.empty()) return;
    const std::string resolved = plotter::resolveSnippetResourcePath(path);
    ofFilePath::createEnclosingDirectory(ofFilePath::getEnclosingDirectory(resolved), false, true);
    ofBuffer buf;
    buf.set(text);
    ofBufferToFile(resolved, buf);
}

/// Host a TextEditor in an ImGui child with native bottom-edge resize.
/// Prefer this over InvisibleButton grips: in a scrollable panel the parent can
/// claim WantCaptureMouse / drag-scroll and the grip never moves height.
inline void drawResizableGcodeEditorHost(TextEditor& editor, float& height,
                                         float minH = 60.f, float maxH = 800.f)
{
    height = std::clamp(height, minH, maxH);
    ImGui::BeginChild("##gcode_host", ImVec2(-1.f, height),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
    // Do not pass parent-window focus — TextEditor only accepts keys when its
    // own child is focused (one active editor among preamble/snippet/postamble).
    editor.Render("##editor", false, ImVec2(-1.f, -1.f), false);
    ImGui::EndChild();
    height = std::clamp(ImGui::GetItemRectSize().y, minH, maxH);
}

/// Inline code editor (syntax-highlighted, drag-to-resize) backed by a file.
struct SnippetEditorState {
    TextEditor  editor;
    std::string editKey;             ///< file path currently loaded
    float       height         = 110.f;
    int         savedUndoIdx   = 0;
    int         watchedUndoIdx = 0;  ///< last undo idx that bumped lastEditTime
    double      lastEditTime   = -1.0;
    bool        initialized    = false;
};

/// In-memory G-code editor (preamble / postamble) — same look as snippet editors.
struct InlineGcodeEditorState {
    TextEditor  editor;
    std::string syncKey;             ///< bump/clear to force reload from host string
    float       height         = 110.f;
    int         savedUndoIdx   = 0;
    int         watchedUndoIdx = 0;
    double      lastEditTime   = -1.0;
    bool        initialized    = false;
};

inline void ensureGcodeEditor(TextEditor& editor, bool& initialized)
{
    if (initialized) return;
    initialized = true;
    editor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::Gcode);
    editor.SetShowWhitespacesEnabled(false);
    editor.SetShowLineNumbersEnabled(true);
    // Reuse a monospace face already in the atlas (loaded by the host app).
    if (ImFontAtlas* atlas = ImGui::GetIO().Fonts) {
        for (int i = 0; i < atlas->Fonts.Size; ++i) {
            ImFont* f = atlas->Fonts[i];
            if (!f || !f->GetDebugName()) continue;
            if (std::strstr(f->GetDebugName(), "JetBrains") != nullptr) {
                editor.SetFont(f);
                break;
            }
        }
    }
}

/// Persist dirty edits after debounce. TextEditor::Render returns focus, not "changed".
inline bool flushEditorIfDebounced(TextEditor& editor,
                                   int& savedUndoIdx,
                                   int& watchedUndoIdx,
                                   double& lastEditTime,
                                   const std::function<void(const std::string&)>& onFlush)
{
    const int undo = editor.GetUndoIndex();
    if (undo != savedUndoIdx) {
        if (undo != watchedUndoIdx) {
            watchedUndoIdx = undo;
            lastEditTime   = ImGui::GetTime();
        }
    } else {
        watchedUndoIdx = savedUndoIdx;
        lastEditTime   = -1.0;
        return false;
    }
    if (lastEditTime < 0.0 || ImGui::GetTime() - lastEditTime <= 0.6)
        return false;
    onFlush(editor.GetText());
    savedUndoIdx   = editor.GetUndoIndex();
    watchedUndoIdx = savedUndoIdx;
    lastEditTime   = -1.0;
    return true;
}

/// Draws @p st and persists edits to @p filePath (debounced ~0.6 s).
/// Returns true when a save happened.
inline bool drawSnippetEditor(const char* id,
                              const std::string& filePath,
                              SnippetEditorState& st)
{
    if (filePath.empty()) {
        ImGui::TextDisabled("No snippet file path.");
        return false;
    }

    ensureGcodeEditor(st.editor, st.initialized);

    auto flushToFile = [&st]() {
        writeSnippetFile(st.editKey, st.editor.GetText());
        st.savedUndoIdx   = st.editor.GetUndoIndex();
        st.watchedUndoIdx = st.savedUndoIdx;
        st.lastEditTime   = -1.0;
    };

    if (st.editKey != filePath) {
        if (!st.editKey.empty() && st.editor.GetUndoIndex() != st.savedUndoIdx)
            flushToFile();
        st.editKey = filePath;
        st.editor.SetText(plotter::loadSnippetText(filePath));
        st.savedUndoIdx   = st.editor.GetUndoIndex();
        st.watchedUndoIdx = st.savedUndoIdx;
        st.lastEditTime   = -1.0;
    }

    ImGui::PushID(id);
    drawResizableGcodeEditorHost(st.editor, st.height);

    const bool saved = flushEditorIfDebounced(
        st.editor, st.savedUndoIdx, st.watchedUndoIdx, st.lastEditTime,
        [&](const std::string& text) { writeSnippetFile(st.editKey, text); });
    ImGui::PopID();
    return saved;
}

/// Same chrome as snippet editors, but bound to an in-memory string.
/// Clear @p st.syncKey (or change it) after externally replacing @p text.
/// Returns true when debounced text was written back into @p text.
inline bool drawInlineGcodeEditor(const char* id,
                                  std::string& text,
                                  InlineGcodeEditorState& st,
                                  const std::string& syncKey)
{
    ensureGcodeEditor(st.editor, st.initialized);

    if (st.syncKey != syncKey) {
        st.syncKey = syncKey;
        st.editor.SetText(text);
        st.savedUndoIdx   = st.editor.GetUndoIndex();
        st.watchedUndoIdx = st.savedUndoIdx;
        st.lastEditTime   = -1.0;
    }

    ImGui::PushID(id);
    drawResizableGcodeEditorHost(st.editor, st.height);

    const bool saved = flushEditorIfDebounced(
        st.editor, st.savedUndoIdx, st.watchedUndoIdx, st.lastEditTime,
        [&](const std::string& t) { text = t; });
    ImGui::PopID();
    return saved;
}

} // namespace plotter::kit
