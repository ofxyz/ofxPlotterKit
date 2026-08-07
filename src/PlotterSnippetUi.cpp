#include "PlotterSnippetUi.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace plotter::kit {

namespace {

struct MacroHint {
    const char* insert = nullptr;
    const char* menu   = nullptr;
    const char* tip    = nullptr;
};

const MacroHint* snippetMacros(int& count)
{
    static const MacroHint k[] = {
        { "{penUpZ}", "penUpZ",
          "Expands to Controls → Pen → Pen Up.\n"
          "Also accepts {penUp} / {penup} (any case)." },
        { "{penDownZ}", "penDownZ",
          "Expands to Controls → Pen → Pen Down.\n"
          "Also accepts {penDown} / {pendown} (any case)." },
        { "{random(0,1)}", "random(min,max)",
          "New random float each occurrence (up to 3 decimals)." },
        { "{random:id(0,1)}", "random:id(min,max)",
          "Draw once per id for this expansion; reuse the same value later." },
    };
    count = (int)(sizeof(k) / sizeof(k[0]));
    return k;
}

void toLowerAscii(std::string& s)
{
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
}

int columnToOffset(const std::string& line, int column)
{
    if (column <= 0) return 0;
    int col = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        if (col >= column) return (int)i;
        const unsigned char c = (unsigned char)line[i];
        if ((c & 0xC0) == 0x80) continue;
        ++col;
    }
    return (int)line.size();
}

int offsetToColumn(const std::string& line, int offset)
{
    int col = 0;
    const int n = std::min(offset, (int)line.size());
    for (int i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)line[i];
        if ((c & 0xC0) == 0x80) continue;
        ++col;
    }
    return col;
}

void refreshIdleText(TextEditor& ed, std::string& idleText, int& idleTextUndo)
{
    const int undo = ed.GetUndoIndex();
    if (idleTextUndo == undo) return;
    idleText     = ed.GetText();
    idleTextUndo = undo;
}

void insertAtCursor(TextEditor& ed, const char* text)
{
    if (!text || !*text) return;
    int line = 0, col = 0;
    ed.GetCursorPosition(line, col);
    auto lines = ed.GetTextLines();
    if (line < 0) return;
    if (line >= (int)lines.size()) {
        lines.push_back({});
        line = (int)lines.size() - 1;
        col = 0;
    }
    std::string& L = lines[(size_t)line];
    const int off = columnToOffset(L, col);
    L.insert((size_t)off, text);
    ed.SetTextLines(lines);
    ed.SetCursorPosition(line, offsetToColumn(L, off + (int)std::strlen(text)));
}

void applyCompletion(TextEditor& ed, int line, int braceCol, int cursorCol,
                     const char* completion)
{
    auto lines = ed.GetTextLines();
    if (line < 0 || line >= (int)lines.size() || !completion) return;
    std::string& L = lines[(size_t)line];
    const int braceOff  = columnToOffset(L, braceCol);
    const int cursorOff = columnToOffset(L, cursorCol);
    if (braceOff < 0 || cursorOff < braceOff || cursorOff > (int)L.size()) return;
    L.replace((size_t)braceOff, (size_t)(cursorOff - braceOff), completion);
    ed.SetTextLines(lines);
    ed.SetCursorPosition(line, offsetToColumn(L, braceOff + (int)std::strlen(completion)));
}

bool macroPrefixAtCursor(TextEditor& ed, int& outLine, int& outBraceCol,
                         int& outCursorCol, std::string& outFilter)
{
    outLine = outBraceCol = outCursorCol = 0;
    outFilter.clear();
    int line = 0, col = 0;
    ed.GetCursorPosition(line, col);
    auto lines = ed.GetTextLines();
    if (line < 0 || line >= (int)lines.size()) return false;
    const std::string& L = lines[(size_t)line];
    const int cursorOff = columnToOffset(L, col);
    if (cursorOff <= 0) return false;

    int brace = -1;
    for (int i = cursorOff - 1; i >= 0; --i) {
        const char c = L[(size_t)i];
        if (c == '{') { brace = i; break; }
        if (c == '}' || c == ' ' || c == '\t') return false;
    }
    if (brace < 0) return false;

    outLine      = line;
    outBraceCol  = offsetToColumn(L, brace);
    outCursorCol = col;
    outFilter    = L.substr((size_t)brace + 1, (size_t)(cursorOff - brace - 1));
    toLowerAscii(outFilter);
    return true;
}

void drawMacroAutocomplete(TextEditor& ed, bool editorFocused)
{
    if (!editorFocused) return;

    int line = 0, braceCol = 0, cursorCol = 0;
    std::string filter;
    if (!macroPrefixAtCursor(ed, line, braceCol, cursorCol, filter))
        return;

    int n = 0;
    const MacroHint* macros = snippetMacros(n);
    std::vector<int> matches;
    matches.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        std::string key = macros[i].insert ? macros[i].insert : "";
        if (!key.empty() && key.front() == '{') key.erase(key.begin());
        // Strip trailing template args for matching: "penUpZ", "random(0,1)" → "random"
        toLowerAscii(key);
        std::string keyHead = key;
        const auto paren = keyHead.find('(');
        if (paren != std::string::npos) keyHead.resize(paren);
        if (filter.empty()
            || keyHead.find(filter) == 0
            || key.find(filter) == 0
            || keyHead.find(filter) != std::string::npos)
            matches.push_back(i);
    }
    if (matches.empty()) return;

    ImGui::SetNextWindowPos(ImGui::GetCursorScreenPos(), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 6.f));
    ImGui::Begin("##snippetMacroSuggest", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
                     | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize
                     | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
                     | ImGuiWindowFlags_Tooltip);
    ImGui::TextDisabled("Macros — click to insert");
    ImGui::Separator();
    for (int mi : matches) {
        const MacroHint& m = macros[mi];
        if (ImGui::Selectable(m.insert))
            applyCompletion(ed, line, braceCol, cursorCol, m.insert);
        if (ImGui::IsItemHovered() && m.tip)
            ImGui::SetTooltip("%s", m.tip);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void drawSnippetMenuBar(TextEditor& ed,
                        TextEditorFindReplace* findReplace,
                        const std::function<void()>& onSave,
                        const std::function<void()>& onNew,
                        bool* liveEditing)
{
    if (!ImGui::BeginMenuBar()) return;

    if ((onNew || onSave) && ImGui::BeginMenu("File")) {
        if (onNew && ImGui::MenuItem("New", "Ctrl+N"))
            onNew();
        if (onNew && onSave)
            ImGui::Separator();
        if (onSave && ImGui::MenuItem("Save", "Ctrl+S"))
            onSave();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Insert")) {
        int n = 0;
        const MacroHint* macros = snippetMacros(n);
        for (int i = 0; i < n; ++i) {
            if (ImGui::MenuItem(macros[i].menu)) {
                insertAtCursor(ed, macros[i].insert);
                if (liveEditing) *liveEditing = true;
            }
            if (ImGui::IsItemHovered() && macros[i].tip)
                ImGui::SetTooltip("%s", macros[i].tip);
        }
        ImGui::EndMenu();
    }

    if (findReplace && ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Find…", "Ctrl+F"))
            findReplace->showFind(false);
        if (ImGui::MenuItem("Find & Replace…", "Ctrl+Shift+F"))
            findReplace->showFind(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, ed.CanUndo()))
            ed.Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, ed.CanRedo()))
            ed.Redo();
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

} // namespace

void drawResizableGcodeEditorHost(TextEditor& editor, float& height,
                                  float minH, float maxH)
{
    drawResizableGcodeEditorHostEx(editor, height, nullptr, minH, maxH);
}

void drawResizableGcodeEditorHostEx(TextEditor& editor,
                                    float& height,
                                    SnippetEditorChrome* chrome,
                                    float minH,
                                    float maxH)
{
    height = std::clamp(height, minH, maxH);

    const ImGuiWindowFlags hostFlags = chrome ? ImGuiWindowFlags_MenuBar : 0;

    ImGui::BeginChild("##gcode_host", ImVec2(-1.f, height),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY,
                      hostFlags);

    if (chrome) {
        drawSnippetMenuBar(editor, chrome->findReplace, chrome->onSave, chrome->onNew,
                           chrome->liveEditing);
        if (chrome->findReplace
            && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            chrome->findReplace->handleShortcuts();
        if (chrome->findReplace)
            chrome->findReplace->draw(editor, "snip");
    }

    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    if (chrome && chrome->liveEditing && focused)
        *chrome->liveEditing = true;

    // TextEditor::Render is expensive. When unfocused, show a cheap text mirror.
    const bool useLive = !chrome || !chrome->liveEditing || *chrome->liveEditing || focused;
    if (useLive) {
        editor.Render("##editor", false, ImVec2(-1.f, -1.f), false);
        if (chrome)
            drawMacroAutocomplete(editor, focused);
        if (chrome && chrome->liveEditing && !focused && !hovered)
            *chrome->liveEditing = false;
    } else {
        const char* idle = (chrome && chrome->idleText && !chrome->idleText->empty())
            ? chrome->idleText->c_str()
            : "(empty — click to edit)";
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(idle);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if (chrome && chrome->liveEditing) {
            if (ImGui::Button("Start editing")
                || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
                *chrome->liveEditing = true;
            ImGui::SameLine();
            ImGui::TextDisabled("Idle preview (keeps FPS up). Type { for macros once editing.");
        }
    }

    ImGui::EndChild();
    height = std::clamp(ImGui::GetItemRectSize().y, minH, maxH);
}

bool drawSnippetEditor(const char* id,
                       const std::string& filePath,
                       SnippetEditorState& st,
                       std::function<void()> onNew)
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
        st.idleText       = st.editor.GetText();
        st.idleTextUndo   = st.savedUndoIdx;
    };

    if (st.editKey != filePath) {
        if (!st.editKey.empty() && st.editor.GetUndoIndex() != st.savedUndoIdx)
            flushToFile();
        st.editKey = filePath;
        st.editor.SetText(plotter::loadSnippetText(filePath));
        st.savedUndoIdx   = st.editor.GetUndoIndex();
        st.watchedUndoIdx = st.savedUndoIdx;
        st.lastEditTime   = -1.0;
        // Stay idle until clicked — TextEditor::Render alone can tank FPS.
        st.liveEditing    = false;
        st.idleText.clear();
        st.idleTextUndo = -1;
    }

    refreshIdleText(st.editor, st.idleText, st.idleTextUndo);

    ImGui::PushID(id);

    if (st.liveEditing
        && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            flushToFile();
        if (onNew && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
            onNew();
    }

    SnippetEditorChrome chrome;
    chrome.findReplace = &st.findReplace;
    chrome.liveEditing = &st.liveEditing;
    chrome.idleText    = &st.idleText;
    chrome.onSave      = flushToFile;
    chrome.onNew       = std::move(onNew);

    drawResizableGcodeEditorHostEx(st.editor, st.height, &chrome);
    if (st.liveEditing)
        refreshIdleText(st.editor, st.idleText, st.idleTextUndo);

    const bool saved = flushEditorIfDebounced(
        st.editor, st.savedUndoIdx, st.watchedUndoIdx, st.lastEditTime,
        [&](const std::string& text) {
            writeSnippetFile(st.editKey, text);
            st.idleText     = text;
            st.idleTextUndo = st.editor.GetUndoIndex();
        });
    ImGui::PopID();
    return saved;
}

bool drawInlineGcodeEditor(const char* id,
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
        st.liveEditing    = false;
        st.idleText.clear();
        st.idleTextUndo = -1;
    }

    refreshIdleText(st.editor, st.idleText, st.idleTextUndo);

    ImGui::PushID(id);

    SnippetEditorChrome chrome;
    chrome.findReplace = &st.findReplace;
    chrome.liveEditing = &st.liveEditing;
    chrome.idleText    = &st.idleText;
    chrome.onSave      = [&]() {
        text              = st.editor.GetText();
        st.savedUndoIdx   = st.editor.GetUndoIndex();
        st.watchedUndoIdx = st.savedUndoIdx;
        st.lastEditTime   = -1.0;
        st.idleText       = text;
        st.idleTextUndo   = st.savedUndoIdx;
    };

    drawResizableGcodeEditorHostEx(st.editor, st.height, &chrome);

    const bool saved = flushEditorIfDebounced(
        st.editor, st.savedUndoIdx, st.watchedUndoIdx, st.lastEditTime,
        [&](const std::string& t) {
            text            = t;
            st.idleText     = t;
            st.idleTextUndo = st.editor.GetUndoIndex();
        });
    ImGui::PopID();
    return saved;
}

} // namespace plotter::kit
