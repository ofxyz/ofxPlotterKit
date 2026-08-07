#pragma once

#include "ofMain.h"

#include "imgui.h"
#include "ofxImGuiTextEdit.h"
#include "TextEditorFindReplace.h"
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

/// Optional chrome for the resizable G-code host (menu, find, idle FPS path).
struct SnippetEditorChrome {
    TextEditorFindReplace* findReplace = nullptr;
    bool*                 liveEditing = nullptr;
    std::string*          idleText    = nullptr;
    std::function<void()> onSave;
    std::function<void()> onNew;
};

/// Host a TextEditor in an ImGui child with native bottom-edge resize.
void drawResizableGcodeEditorHost(TextEditor& editor, float& height,
                                  float minH = 60.f, float maxH = 800.f);

/// Same host with optional menu / find / idle (non-focused) cheap draw.
void drawResizableGcodeEditorHostEx(TextEditor& editor,
                                    float& height,
                                    SnippetEditorChrome* chrome,
                                    float minH = 60.f,
                                    float maxH = 800.f);

/// Inline code editor (syntax-highlighted, drag-to-resize) backed by a file.
struct SnippetEditorState {
    TextEditor             editor;
    TextEditorFindReplace  findReplace;
    std::string            editKey;             ///< file path currently loaded
    float                  height         = 110.f;
    int                    savedUndoIdx   = 0;
    int                    watchedUndoIdx = 0;  ///< last undo idx that bumped lastEditTime
    double                 lastEditTime   = -1.0;
    bool                   initialized    = false;
    bool                   liveEditing    = false; ///< false → cheap idle draw (no TextEditor::Render)
    std::string            idleText;
    int                    idleTextUndo   = -1;
};

/// In-memory G-code editor (preamble / postamble) — same look as snippet editors.
struct InlineGcodeEditorState {
    TextEditor             editor;
    TextEditorFindReplace  findReplace;
    std::string            syncKey;             ///< bump/clear to force reload from host string
    float                  height         = 110.f;
    int                    savedUndoIdx   = 0;
    int                    watchedUndoIdx = 0;
    double                 lastEditTime   = -1.0;
    bool                   initialized    = false;
    bool                   liveEditing    = false;
    std::string            idleText;
    int                    idleTextUndo   = -1;
};

/// Draws @p st and persists edits to @p filePath (debounced ~0.6 s).
/// Returns true when a save happened.
/// @p onNew — optional File → New handler (e.g. create a new snippet file).
bool drawSnippetEditor(const char* id,
                       const std::string& filePath,
                       SnippetEditorState& st,
                       std::function<void()> onNew = {});

/// Same chrome as snippet editors, but bound to an in-memory string.
/// Clear @p st.syncKey (or change it) after externally replacing @p text.
/// Returns true when debounced text was written back into @p text.
bool drawInlineGcodeEditor(const char* id,
                           std::string& text,
                           InlineGcodeEditorState& st,
                           const std::string& syncKey);

} // namespace plotter::kit
