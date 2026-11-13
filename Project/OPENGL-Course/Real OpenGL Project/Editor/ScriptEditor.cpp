#include "Editor/ScriptEditor.h"
#include "imgui_internal.h"
#include "External Libs/ImGuiColorTextEdit/TextEditor.h"
#include "Core/ScriptEngine.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Scene/LuaScriptComponent.h"
#include "Scene/SceneSerializer.h"
#include "Lighting/LightObject.h"

#include "External Libs/nlohmann/json.hpp"

#include <fstream>
#include <sstream>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <unordered_set>

using ScriptApi::Kind;
using ScriptAnalysis::Completion;
using ScriptAnalysis::Diagnostic;

std::vector<std::string> ScriptEditor::s_PendingOpen;
ImFont* ScriptEditor::s_CodeFont = nullptr;

namespace
{
	// ---------------------------------------------------------------------------------------
	// Look & feel
	// ---------------------------------------------------------------------------------------
	const ImVec4 kAccent = ImVec4(0.61f, 0.42f, 0.95f, 1.0f);
	const ImVec4 kErrorColor = ImVec4(0.95f, 0.36f, 0.36f, 1.0f);
	const ImVec4 kWarningColor = ImVec4(0.98f, 0.74f, 0.25f, 1.0f);
	const ImVec4 kInfoColor = ImVec4(0.45f, 0.70f, 1.0f, 1.0f);
	const ImVec4 kOkColor = ImVec4(0.40f, 0.85f, 0.55f, 1.0f);
	const ImVec4 kCodeColor = ImVec4(0.62f, 0.80f, 1.0f, 1.0f);

	ImVec4 SeverityColor(Diagnostic::Severity s)
	{
		return s == Diagnostic::Severity::Error ? kErrorColor : s == Diagnostic::Severity::Warning ? kWarningColor : kInfoColor;
	}

	const TextEditor::Palette& EnginePalette()
	{
		static TextEditor::Palette palette = [] {
			TextEditor::Palette p = TextEditor::GetDarkPalette();
			auto set = [&](TextEditor::Color c, ImU32 v) { p[static_cast<size_t>(c)] = v; };
			set(TextEditor::Color::text, IM_COL32(220, 220, 228, 255));
			set(TextEditor::Color::keyword, IM_COL32(198, 140, 255, 255));
			set(TextEditor::Color::declaration, IM_COL32(78, 201, 176, 255));
			set(TextEditor::Color::number, IM_COL32(181, 206, 168, 255));
			set(TextEditor::Color::string, IM_COL32(230, 160, 120, 255));
			set(TextEditor::Color::punctuation, IM_COL32(200, 200, 215, 255));
			set(TextEditor::Color::identifier, IM_COL32(170, 215, 255, 255));
			set(TextEditor::Color::knownIdentifier, IM_COL32(90, 190, 255, 255));
			set(TextEditor::Color::comment, IM_COL32(110, 150, 100, 255));
			set(TextEditor::Color::background, IM_COL32(20, 20, 26, 255));
			set(TextEditor::Color::cursor, IM_COL32(230, 230, 255, 255));
			set(TextEditor::Color::selection, IM_COL32(110, 75, 180, 120));
			set(TextEditor::Color::lineNumber, IM_COL32(95, 95, 115, 255));
			set(TextEditor::Color::currentLineNumber, IM_COL32(210, 200, 240, 255));
			return p;
		}();
		return palette;
	}

	// Lua + the engine's types (declarations) and globals (known identifiers)
	const TextEditor::Language* EngineLua()
	{
		static TextEditor::Language language;
		static int builtRevision = -1;
		int revision = ScriptApi::Registry::Get().Revision();
		if (builtRevision != revision)
		{
			language = *TextEditor::Language::Lua();
			language.name = "Lua (engine)";
			for (const auto& t : ScriptApi::Registry::Get().Types())
				if (!t.isTable) language.declarations.insert(t.name);
			for (const auto& g : ScriptApi::Registry::Get().Globals())
				language.identifiers.insert(g.name);
			language.identifiers.insert("gameObject");
			builtRevision = revision;
		}
		return &language;
	}

	ImU32 KindColor(Kind k)
	{
		switch (k)
		{
		case Kind::Method: return IM_COL32(190, 130, 255, 255);
		case Kind::Function: return IM_COL32(230, 200, 90, 255);
		case Kind::Property: return IM_COL32(90, 180, 255, 255);
		case Kind::Constant: return IM_COL32(120, 210, 160, 255);
		case Kind::Global: return IM_COL32(78, 201, 176, 255);
		case Kind::Local: return IM_COL32(150, 200, 255, 255);
		case Kind::Keyword: return IM_COL32(150, 150, 165, 255);
		case Kind::Snippet: return IM_COL32(120, 200, 120, 255);
		case Kind::ScriptMember: return IM_COL32(255, 150, 110, 255);
		default: return IM_COL32(180, 180, 180, 255);
		}
	}

	const char* KindLetter(Kind k)
	{
		switch (k)
		{
		case Kind::Method: return "m";
		case Kind::Function: return "f";
		case Kind::Property: return "p";
		case Kind::Constant: return "c";
		case Kind::Global: return "G";
		case Kind::Local: return "v";
		case Kind::Keyword: return "k";
		case Kind::Snippet: return "s";
		case Kind::ScriptMember: return "S";
		case Kind::Type: return "T";
		default: return "?";
		}
	}

	void Badge(ImDrawList* dl, ImVec2 pos, float size, ImU32 color, const char* letter)
	{
		dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), (color & 0x00FFFFFF) | 0x40000000, 3.0f);
		dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), color, 3.0f);
		ImVec2 ts = ImGui::CalcTextSize(letter);
		dl->AddText(ImVec2(pos.x + (size - ts.x) * 0.5f, pos.y + (size - ts.y) * 0.5f), color, letter);
	}

	// Draggable divider between two panes; returns true while dragged
	bool Splitter(const char* id, bool vertical, float* size, float minSize, float maxSize, float sign = 1.0f)
	{
		const float thickness = 5.0f;
		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImVec2 buttonSize = vertical ? ImVec2(thickness, std::max(1.0f, avail.y)) : ImVec2(std::max(1.0f, avail.x), thickness);
		ImGui::InvisibleButton(id, buttonSize);
		bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
		if (hovered || active) ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
		if (active)
		{
			float delta = vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
			*size = std::clamp(*size + delta * sign, minSize, maxSize);
		}
		ImU32 color = ImGui::GetColorU32(active ? kAccent : hovered ? ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.5f) : ImGui::GetStyleColorVec4(ImGuiCol_Separator));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		if (vertical) dl->AddLine(ImVec2(cursor.x + thickness * 0.5f, cursor.y), ImVec2(cursor.x + thickness * 0.5f, cursor.y + buttonSize.y), color, active || hovered ? 2.0f : 1.0f);
		else dl->AddLine(ImVec2(cursor.x, cursor.y + thickness * 0.5f), ImVec2(cursor.x + buttonSize.x, cursor.y + thickness * 0.5f), color, active || hovered ? 2.0f : 1.0f);
		return active;
	}

	// Rounded status chip ("2 errors")
	void Chip(const char* text, ImVec4 color)
	{
		ImVec2 pad(8.0f, 2.0f);
		ImVec2 ts = ImGui::CalcTextSize(text);
		ImVec2 pos = ImGui::GetCursorScreenPos();
		float h = ImGui::GetFrameHeight();
		ImVec2 size(ts.x + pad.x * 2.0f, h);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.18f)), h * 0.5f);
		dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.6f)), h * 0.5f);
		dl->AddText(ImVec2(pos.x + pad.x, pos.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(color), text);
		ImGui::Dummy(size);
	}

	std::string LeadingWhitespace(const std::string& s)
	{
		size_t i = 0;
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
		return s.substr(0, i);
	}

	bool IsBlank(const std::string& s)
	{
		return std::all_of(s.begin(), s.end(), [](char c) { return c == ' ' || c == '\t' || c == '\r'; });
	}

	std::string Trimmed(std::string s)
	{
		while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
		size_t i = 0;
		while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
		return s.substr(i);
	}

	bool EndsWith(const std::string& s, const std::string& suffix)
	{
		return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	bool OpensBlock(const std::string& line)
	{
		// strip a trailing comment
		std::string code = line;
		size_t comment = code.find("--");
		if (comment != std::string::npos) code = code.substr(0, comment);
		code = Trimmed(code);
		if (code.empty()) return false;
		if (EndsWith(code, "then") || EndsWith(code, "do") || EndsWith(code, "else") || EndsWith(code, "repeat") || EndsWith(code, "{")) return true;
		if (code.find("function") != std::string::npos && code.back() == ')' && code.find("end") == std::string::npos) return true;
		return false;
	}

	std::string LowerCopy(std::string s)
	{
		for (char& c : s) c = (char)std::tolower((unsigned char)c);
		return s;
	}

	std::string FileNameOf(const std::string& path)
	{
		size_t slash = path.find_last_of("/\\");
		return slash == std::string::npos ? path : path.substr(slash + 1);
	}

	void ZigZag(ImDrawList* dl, float x0, float x1, float y, ImU32 color)
	{
		const float step = 3.0f, amp = 1.6f;
		ImVec2 pts[512];
		int n = 0;
		for (float x = x0; x < x1 && n < 511; x += step) pts[n++] = ImVec2(x, y + ((n % 2) ? -amp : amp));
		pts[n++] = ImVec2(x1, y);
		if (n >= 2) dl->AddPolyline(pts, n, color, ImDrawFlags_None, 1.2f);
	}

	double Now() { return ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0; }
}

// =========================================================================
// ScriptTab implementation
// =========================================================================

void ScriptEditor::ScriptTab::MarkChanged()
{
	isDirty = buffer != savedContent;
	compileDue = Now() + 0.3;
}

bool ScriptEditor::ScriptTab::Save(const std::string& targetPath)
{
	std::string savePath = targetPath.empty() ? path : targetPath;
	if (savePath.empty()) return false;

	std::ofstream file(savePath, std::ios::out | std::ios::trunc | std::ios::binary);
	if (!file.is_open()) return false;

	file << buffer;
	file.close();

	path = savePath;
	std::filesystem::path p(savePath);
	title = p.filename().string();
	savedContent = buffer;
	isDirty = false;
	std::error_code ec;
	diskTime = std::filesystem::last_write_time(savePath, ec);
	return true;
}

bool ScriptEditor::ScriptTab::Load(const std::string& targetPath)
{
	if (targetPath.empty()) return false;

	std::ifstream file(targetPath, std::ios::in | std::ios::binary);
	if (!file.is_open()) return false;

	std::stringstream ss;
	ss << file.rdbuf();
	buffer = ss.str();
	buffer.erase(std::remove(buffer.begin(), buffer.end(), '\r'), buffer.end());
	file.close();

	path = targetPath;
	std::filesystem::path p(targetPath);
	title = p.filename().string();
	savedContent = buffer;
	isDirty = false;
	cursorPos = 0;
	cursorLine = 1;
	cursorColumn = 1;
	selectionStart = 0;
	selectionEnd = 0;
	pushToEditor = true;
	compiled = false;
	compileDue = 0.0;
	std::error_code ec;
	diskTime = std::filesystem::last_write_time(targetPath, ec);
	return true;
}

void ScriptEditor::ScriptTab::InsertText(size_t pos, const std::string& text)
{
	if (pos > buffer.size()) pos = buffer.size();
	buffer.insert(pos, text);
	cursorPos = static_cast<int>(pos + text.size());
	pushToEditor = true;
	MarkChanged();
}

void ScriptEditor::ScriptTab::AppendText(const std::string& text)
{
	buffer.append(text);
	cursorPos = static_cast<int>(buffer.size());
	pushToEditor = true;
	MarkChanged();
}

// =========================================================================
// ScriptEditor: lifetime & documents
// =========================================================================

ScriptEditor::ScriptEditor() {}

ScriptEditor::~ScriptEditor()
{
	if (m_AssetScan.valid()) m_AssetScan.wait();
	if (!m_SessionSignature.empty()) SaveSession();
}

namespace { const char* kSessionFile = "script_editor.json"; }

std::string ScriptEditor::SessionSignature() const
{
	std::string s = std::to_string(m_LastOpen) + "|" + std::to_string(m_ActiveTabIndex) + "|" + std::to_string((int)m_ExplorerWidth) + "|" +
		std::to_string((int)m_ProblemsHeight) + "|" + std::to_string((int)m_FontSize) + "|" + std::to_string(m_PopOut) + std::to_string(m_ShowExplorer) + std::to_string(m_ShowProblems);
	for (const auto& t : m_Tabs) s += "|" + t.path;
	return s;
}

void ScriptEditor::SaveSession() const
{
	nlohmann::json j;
	j["open"] = m_LastOpen;
	j["explorerWidth"] = m_ExplorerWidth;
	j["problemsHeight"] = m_ProblemsHeight;
	j["fontSize"] = m_FontSize;
	j["popOut"] = m_PopOut;
	j["showExplorer"] = m_ShowExplorer;
	j["showProblems"] = m_ShowProblems;
	j["tabs"] = nlohmann::json::array();
	int active = -1, index = 0;
	for (int i = 0; i < (int)m_Tabs.size(); i++)
	{
		if (m_Tabs[i].path.empty()) continue;
		j["tabs"].push_back(m_Tabs[i].path);
		if (i == m_ActiveTabIndex) active = index;
		index++;
	}
	j["active"] = active;
	std::ofstream f(kSessionFile);
	if (f.is_open()) f << j.dump(2);
}

void ScriptEditor::LoadSession()
{
	std::ifstream f(kSessionFile);
	if (!f.is_open()) return;
	nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
	if (j.is_discarded() || !j.is_object()) return;
	m_RestoreOpen = j.value("open", false);
	m_ExplorerWidth = j.value("explorerWidth", m_ExplorerWidth);
	m_ProblemsHeight = j.value("problemsHeight", m_ProblemsHeight);
	m_FontSize = j.value("fontSize", m_FontSize);
	m_PopOut = j.value("popOut", m_PopOut);
	m_ShowExplorer = j.value("showExplorer", m_ShowExplorer);
	m_ShowProblems = j.value("showProblems", m_ShowProblems);
	if (j.contains("tabs") && j["tabs"].is_array())
		for (const auto& p : j["tabs"]) if (p.is_string()) OpenFile(p.get<std::string>());
	int active = j.value("active", -1);
	if (active >= 0 && active < (int)m_Tabs.size()) SetActiveTabIndex(active);
}

void ScriptEditor::Init()
{
	// The API metadata (autocomplete, checks) is filled while the bindings are registered
	ScriptEngine::GetInstance().Init();

	LoadSession();
	if (m_Tabs.empty())
	{
		std::error_code ec;
		if (std::filesystem::exists("Assets/scripts/test_script.lua", ec))
			OpenFile("Assets/scripts/test_script.lua");
		else
			NewScript();
	}
	m_AssetScan = std::async(std::launch::async, &ScriptEditor::ScanAssets);
}

void ScriptEditor::RequestOpen(const std::string& path)
{
	if (!path.empty()) s_PendingOpen.push_back(path);
}

int ScriptEditor::NewScript(const std::string& initialContent)
{
	ScriptTab tab;
	tab.title = "untitled_" + std::to_string(m_UntitledCounter++) + ".lua";
	tab.path = "";
	tab.buffer = initialContent;
	tab.savedContent = initialContent;
	tab.isDirty = false;
	tab.cursorPos = static_cast<int>(initialContent.size());
	tab.cursorLine = 1;
	tab.cursorColumn = 1;
	tab.pushToEditor = true;
	tab.compileDue = 0.0;
	tab.uid = m_NextTabUid++;

	m_Tabs.push_back(std::move(tab));
	m_ActiveTabIndex = static_cast<int>(m_Tabs.size()) - 1;
	m_PendingTabSelect = m_ActiveTabIndex;
	return m_ActiveTabIndex;
}

int ScriptEditor::OpenFile(const std::string& filepath)
{
	std::string norm = NormalizePath(filepath);
	for (int i = 0; i < static_cast<int>(m_Tabs.size()); ++i)
	{
		if (!m_Tabs[i].path.empty() && NormalizePath(m_Tabs[i].path) == norm)
		{
			m_ActiveTabIndex = i;
			m_PendingTabSelect = i;
			return i;
		}
	}

	ScriptTab newTab;
	if (newTab.Load(filepath))
	{
		newTab.uid = m_NextTabUid++;
		m_Tabs.push_back(std::move(newTab));
		m_ActiveTabIndex = static_cast<int>(m_Tabs.size()) - 1;
		m_PendingTabSelect = m_ActiveTabIndex;
		return m_ActiveTabIndex;
	}
	return -1;
}

bool ScriptEditor::SaveActiveScript()
{
	ScriptTab* tab = GetActiveTab();
	if (!tab) return false;
	if (tab->path.empty()) return SaveScriptAs(m_ActiveTabIndex);
	bool ok = tab->Save();
	if (ok) AfterSave(*tab);
	else { m_StatusMessage = "Could not write " + tab->path; m_StatusColor = kErrorColor; m_StatusUntil = Now() + 5.0; }
	return ok;
}

bool ScriptEditor::SaveScriptAs(int tabIndex)
{
	int idx = (tabIndex >= 0) ? tabIndex : m_ActiveTabIndex;
	if (idx < 0 || idx >= static_cast<int>(m_Tabs.size())) return false;

	std::string path = SceneSerializer::SaveFileDialog("Lua Script (*.lua)\0*.lua\0All Files (*.*)\0*.*\0", "Save Lua Script");
	if (path.empty()) return false;

	std::filesystem::path p(path);
	if (!p.has_extension() || p.extension() == ".json")
	{
		p.replace_extension(".lua");
		path = p.string();
	}
	bool ok = m_Tabs[idx].Save(NormalizePath(path));
	if (ok) { m_AssetsScannedAt = -100.0; AfterSave(m_Tabs[idx]); } // a new script: rescan Assets
	return ok;
}

bool ScriptEditor::SaveAll()
{
	bool allOk = true;
	for (size_t i = 0; i < m_Tabs.size(); ++i)
	{
		if (!m_Tabs[i].isDirty) continue;
		if (m_Tabs[i].path.empty()) { if (!SaveScriptAs(static_cast<int>(i))) allOk = false; }
		else if (!m_Tabs[i].Save()) allOk = false;
		else AfterSave(m_Tabs[i]);
	}
	return allOk;
}

int ScriptEditor::HotReload(const std::string& path)
{
	if (!m_Scene || path.empty()) return 0;
	int reloaded = 0;
	for (GameObject* go : m_Scene->GetObjects())
	{
		if (!go) continue;
		for (const auto& comp : go->GetComponents())
		{
			auto* script = dynamic_cast<LuaScriptComponent*>(comp.get());
			if (script && script->IsRunning() && script->MatchesScript(path))
			{
				script->ReloadScript();
				reloaded++;
			}
		}
	}
	return reloaded;
}

void ScriptEditor::AfterSave(ScriptTab& tab)
{
	Recompile(tab);
	double now = Now();
	m_StatusUntil = now + 4.0;
	if (tab.compile.errors > 0)
	{
		// Do not swap a running script for one that cannot load
		m_StatusMessage = "Saved " + tab.title + " - not reloaded: fix the errors first";
		m_StatusColor = kWarningColor;
		return;
	}
	int reloaded = HotReload(tab.path);
	m_StatusMessage = "Saved " + tab.title + (reloaded > 0 ? "  -  reloaded live on " + std::to_string(reloaded) + (reloaded == 1 ? " object" : " objects") : "");
	m_StatusColor = kOkColor;
}

void ScriptEditor::CloseTab(int tabIndex)
{
	if (tabIndex < 0 || tabIndex >= static_cast<int>(m_Tabs.size())) return;

	m_Tabs.erase(m_Tabs.begin() + tabIndex);
	m_Completion.open = false;
	if (m_Tabs.empty()) m_ActiveTabIndex = -1;
	else if (m_ActiveTabIndex >= static_cast<int>(m_Tabs.size())) m_ActiveTabIndex = static_cast<int>(m_Tabs.size()) - 1;
	else if (tabIndex < m_ActiveTabIndex) m_ActiveTabIndex--;
	m_PendingTabSelect = m_ActiveTabIndex;
}

void ScriptEditor::CloseActiveTab()
{
	CloseTab(m_ActiveTabIndex);
}

void ScriptEditor::SetActiveTabIndex(int index)
{
	if (index >= 0 && index < static_cast<int>(m_Tabs.size()))
	{
		m_ActiveTabIndex = index;
		m_PendingTabSelect = index;
	}
}

ScriptEditor::ScriptTab* ScriptEditor::GetActiveTab()
{
	return (m_ActiveTabIndex >= 0 && m_ActiveTabIndex < static_cast<int>(m_Tabs.size())) ? &m_Tabs[m_ActiveTabIndex] : nullptr;
}

const ScriptEditor::ScriptTab* ScriptEditor::GetActiveTab() const
{
	return (m_ActiveTabIndex >= 0 && m_ActiveTabIndex < static_cast<int>(m_Tabs.size())) ? &m_Tabs[m_ActiveTabIndex] : nullptr;
}

ScriptEditor::ScriptTab* ScriptEditor::GetTab(int index)
{
	return (index >= 0 && index < static_cast<int>(m_Tabs.size())) ? &m_Tabs[index] : nullptr;
}

const ScriptEditor::ScriptTab* ScriptEditor::GetTab(int index) const
{
	return (index >= 0 && index < static_cast<int>(m_Tabs.size())) ? &m_Tabs[index] : nullptr;
}

void ScriptEditor::InsertAtCursor(const std::string& text)
{
	ScriptTab* tab = GetActiveTab();
	if (!tab)
	{
		NewScript();
		tab = GetActiveTab();
		if (!tab) return;
	}

	if (tab->editor && !tab->pushToEditor)
	{
		// Live editor: undoable insert
		TextEditor& e = *tab->editor;
		auto pos = e.GetMainCursorPosition();
		bool statement = !text.empty() && text.back() == '\n';
		if (statement && pos.index > 0) pos.index = 0; // own line, above the caret's line
		e.ReplaceSectionText(pos, pos, text);
		int lines = (int)std::count(text.begin(), text.end(), '\n');
		size_t lastNl = text.find_last_of('\n');
		int col = lastNl == std::string::npos ? (int)(pos.index + text.size()) : (int)(text.size() - lastNl - 1);
		e.SetCursor(TextEditor::DocPos(pos.line + lines, col));
		SyncFromEditor(*tab);
		return;
	}

	if (tab->cursorPos < 0) tab->cursorPos = 0;
	if (tab->cursorPos > static_cast<int>(tab->buffer.size())) tab->cursorPos = static_cast<int>(tab->buffer.size());

	// A whole statement (ends with a newline) goes on its own line: insert it at the start of the
	// caret's line instead of splitting that line in two
	if (!text.empty() && text.back() == '\n' && tab->cursorPos > 0 && tab->buffer[tab->cursorPos - 1] != '\n')
	{
		size_t lineStart = tab->buffer.rfind('\n', static_cast<size_t>(tab->cursorPos - 1));
		tab->cursorPos = (lineStart == std::string::npos) ? 0 : static_cast<int>(lineStart + 1);
	}

	tab->buffer.insert(tab->cursorPos, text);
	tab->cursorPos += static_cast<int>(text.size());
	tab->pushToEditor = true;
	tab->MarkChanged();

	int line = 1, lastLineStart = 0;
	for (int i = 0; i < tab->cursorPos && i < static_cast<int>(tab->buffer.size()); ++i)
		if (tab->buffer[i] == '\n') { line++; lastLineStart = i + 1; }
	tab->cursorLine = line;
	tab->cursorColumn = (tab->cursorPos - lastLineStart) + 1;
}

// =========================================================================
// Static helpers (snippets / paths)
// =========================================================================

std::string ScriptEditor::NormalizePath(const std::string& path)
{
	std::string res = path;
	for (char& c : res) if (c == '\\') c = '/';
	size_t pos = res.find("Assets/");
	if (pos == std::string::npos) pos = LowerCopy(res).find("assets/");
	if (pos != std::string::npos && pos > 0) res = res.substr(pos);
	return res;
}

std::string ScriptEditor::SanitizeVarName(const std::string& name)
{
	std::string res;
	bool lastWasUnderscore = false;
	for (char c : name)
	{
		if (std::isalnum(static_cast<unsigned char>(c))) { res.push_back(c); lastWasUnderscore = false; }
		else if (!lastWasUnderscore && !res.empty()) { res.push_back('_'); lastWasUnderscore = true; }
	}
	while (!res.empty() && res.back() == '_') res.pop_back();
	if (res.empty() || std::isdigit(static_cast<unsigned char>(res[0]))) res = "res_" + res;
	return res;
}

std::string ScriptEditor::FormatAssetSnippet(const std::string& rawPath, const std::string& varName)
{
	std::string normPath = NormalizePath(rawPath);
	std::string v = varName;
	if (v.empty())
	{
		size_t lastSlash = normPath.find_last_of('/');
		std::string fname = (lastSlash != std::string::npos) ? normPath.substr(lastSlash + 1) : normPath;
		size_t dot = fname.find_last_of('.');
		v = SanitizeVarName((dot != std::string::npos) ? fname.substr(0, dot) : fname);
	}
	return "local " + v + " = Resource.Get(\"" + normPath + "\")\n";
}

std::string ScriptEditor::FormatGameObjectSnippet(const std::string& objName, const std::string& varName)
{
	std::string v = varName.empty() ? SanitizeVarName(objName) : varName;
	return "local " + v + " = Resource.GameObject(\"" + objName + "\")\n";
}

// =========================================================================
// Environment (scene objects, project files) for the analysis
// =========================================================================

ScriptEditor::AssetLists ScriptEditor::ScanAssets()
{
	namespace fs = std::filesystem;
	AssetLists lists;
	std::error_code ec;
	if (!fs::is_directory("Assets", ec)) return lists;
	for (auto it = fs::recursive_directory_iterator("Assets", fs::directory_options::skip_permission_denied, ec);
		 it != fs::recursive_directory_iterator(); it.increment(ec))
	{
		if (ec) break;
		if (!it->is_regular_file(ec)) continue;
		std::string ext = LowerCopy(it->path().extension().string());
		std::string path = it->path().generic_string();
		if (ext == ".lua") lists.scripts.push_back(path);
		else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr") lists.textures.push_back(path);
		else if (ext == ".mat") lists.materials.push_back(path);
		else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae") lists.models.push_back(path);
		else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") lists.audio.push_back(path);
	}
	for (auto* v : { &lists.scripts, &lists.textures, &lists.materials, &lists.models, &lists.audio }) std::sort(v->begin(), v->end());
	return lists;
}

void ScriptEditor::RefreshEnvironment(SceneManager& scene, const ScriptTab* tab, bool force)
{
	double now = ImGui::GetTime();
	bool changed = false;

	// Project files: scanned on a worker thread every few seconds
	if (m_AssetScan.valid() && m_AssetScan.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		AssetLists lists = m_AssetScan.get();
		changed |= lists.scripts != m_Env.scriptFiles || lists.textures != m_Env.textureFiles;
		m_Env.scriptFiles = std::move(lists.scripts);
		m_Env.textureFiles = std::move(lists.textures);
		m_Env.materialFiles = std::move(lists.materials);
		m_Env.modelFiles = std::move(lists.models);
		m_Env.audioFiles = std::move(lists.audio);
		m_AssetsScannedAt = now;
	}
	if (!m_AssetScan.valid() && now - m_AssetsScannedAt > 8.0)
		m_AssetScan = std::async(std::launch::async, &ScriptEditor::ScanAssets);

	// Scene objects + which of them run the current script
	if (force || now - m_EnvBuiltAt > 0.75)
	{
		m_EnvBuiltAt = now;
		std::vector<std::string> names, selfObjects;
		for (GameObject* go : scene.GetObjects())
		{
			if (!go) continue;
			names.push_back(go->GetName());
			if (tab && !tab->path.empty())
			{
				for (const auto& comp : go->GetComponents())
				{
					auto* script = dynamic_cast<LuaScriptComponent*>(comp.get());
					if (script && script->MatchesScript(tab->path)) { selfObjects.push_back(go->GetName()); break; }
				}
			}
		}
		std::vector<std::string> lightNames;
		for (LightObject* l : scene.GetLights()) if (l) lightNames.push_back(l->GetName());
		changed |= names != m_Env.objectNames || lightNames != m_Env.lightNames;
		m_Env.objectNames = std::move(names);
		m_Env.lightNames = std::move(lightNames);
		m_Env.selfObjects = std::move(selfObjects);
		m_Env.selfScriptPath = tab ? tab->path : "";
	}

	if (changed)
		for (auto& t : m_Tabs) if (t.compiled) t.compileDue = now; // scene-dependent checks
}

void ScriptEditor::CheckDiskChanges()
{
	double now = ImGui::GetTime();
	if (now < m_DiskCheckAt) return;
	m_DiskCheckAt = now + 2.0;
	for (auto& tab : m_Tabs)
	{
		if (tab.path.empty() || tab.isDirty) continue;
		std::error_code ec;
		auto t = std::filesystem::last_write_time(tab.path, ec);
		if (ec || t == tab.diskTime) continue;
		// Changed outside the editor and no local edits: reload, keep the caret line
		int line = tab.cursorLine;
		if (tab.Load(tab.path))
		{
			EnsureEditor(tab);
			GoToLine(tab, line);
		}
	}
}

// =========================================================================
// Editor plumbing
// =========================================================================

void ScriptEditor::EnsureEditor(ScriptTab& tab)
{
	if (!tab.editor)
	{
		tab.editor = std::make_shared<TextEditor>();
		TextEditor& e = *tab.editor;
		e.SetLanguage(EngineLua());
		e.SetPalette(EnginePalette());
		e.SetTabSize(4);
		e.SetShowLineNumbersEnabled(true);
		e.SetShowMatchingBrackets(true);
		e.SetCompletePairedGlyphs(true);
		e.SetAutoIndentEnabled(true);
		e.SetShowScrollbarMiniMapEnabled(true);
		e.SetLineSpacing(1.15f);
		e.SetShowWhitespacesEnabled(false);
		e.SetKeyboardInterceptor([this]() { return InterceptKeys(); });
		TextEditor* raw = tab.editor.get();
		e.SetTextOverlayCallback([this, raw](ImDrawList* dl) {
			for (auto& t : m_Tabs) if (t.editor.get() == raw) { DrawTextOverlay(t, dl); break; }
		});
		tab.pushToEditor = true;
	}
	if (tab.pushToEditor)
	{
		tab.editor->SetText(tab.buffer);
		tab.pushToEditor = false;
		tab.lastUndoIndex = tab.editor->GetUndoIndex();
		tab.model.Build(tab.buffer, &m_Env);
		tab.compileDue = Now();
	}
}

void ScriptEditor::SyncFromEditor(ScriptTab& tab)
{
	if (!tab.editor) return;
	size_t undo = tab.editor->GetUndoIndex();
	if (undo != tab.lastUndoIndex)
	{
		tab.lastUndoIndex = undo;
		tab.buffer = tab.editor->GetText();
		tab.model.Build(tab.buffer, &m_Env);
		tab.MarkChanged();
		tab.compileDue = Now() + 0.35;
	}
	auto pos = tab.editor->GetMainCursorPosition();
	tab.cursorLine = (int)pos.line + 1;
	tab.cursorColumn = (int)pos.index + 1;
}

void ScriptEditor::Recompile(ScriptTab& tab)
{
	tab.model.Build(tab.buffer, &m_Env);
	tab.compile = ScriptAnalysis::Compile(tab.buffer, tab.title, tab.model);

	// The identifier under the caret is still being typed: semantic warnings about it are noise
	if (tab.editor && &tab == GetActiveTab() && m_EditorFocused)
	{
		auto caret = tab.editor->GetMainCursorPosition();
		const auto& lines = tab.model.Lines();
		if ((int)caret.line < (int)lines.size())
		{
			const std::string& text = lines[caret.line];
			int start = std::min((int)caret.index, (int)text.size());
			while (start > 0 && (std::isalnum((unsigned char)text[start - 1]) || text[start - 1] == '_')) start--;
			auto& diags = tab.compile.diagnostics;
			diags.erase(std::remove_if(diags.begin(), diags.end(), [&](const Diagnostic& d) {
				return d.severity == Diagnostic::Severity::Warning && d.line - 1 == (int)caret.line && d.column - 1 == start && start < (int)caret.index;
			}), diags.end());
			tab.compile.errors = tab.compile.warnings = tab.compile.infos = 0;
			for (const auto& d : diags)
				(d.severity == Diagnostic::Severity::Error ? tab.compile.errors : d.severity == Diagnostic::Severity::Warning ? tab.compile.warnings : tab.compile.infos)++;
		}
	}
	tab.compiled = true;
	tab.compileDue = -1.0;
	ApplyDiagnostics(tab);
}

void ScriptEditor::ApplyDiagnostics(ScriptTab& tab)
{
	if (!tab.editor) return;
	TextEditor& e = *tab.editor;
	e.ClearMarkers();

	// One marker per line: worst severity, all messages in the tooltip
	std::vector<std::pair<int, std::pair<Diagnostic::Severity, std::string>>> lines;
	for (const auto& d : tab.compile.diagnostics)
	{
		auto it = std::find_if(lines.begin(), lines.end(), [&](const auto& l) { return l.first == d.line; });
		if (it == lines.end()) lines.push_back({ d.line, { d.severity, d.message } });
		else
		{
			if ((int)d.severity < (int)it->second.first) it->second.first = d.severity;
			it->second.second += "\n" + d.message;
		}
	}
	for (const auto& l : lines)
	{
		ImVec4 c = SeverityColor(l.second.first);
		ImU32 numberColor = ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, 0.55f));
		ImU32 textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, l.second.first == Diagnostic::Severity::Error ? 0.10f : 0.06f));
		if (l.first - 1 < (int)e.GetLineCount())
			e.AddMarker((size_t)(l.first - 1), numberColor, textColor, l.second.second, "");
	}
}

void ScriptEditor::GoToLine(ScriptTab& tab, int line, int column)
{
	if (!tab.editor) return;
	int l = std::clamp(line - 1, 0, std::max(0, (int)tab.editor->GetLineCount() - 1));
	tab.editor->SetCursor(TextEditor::DocPos(l, std::max(0, column - 1)));
	tab.editor->ScrollToLine(l, TextEditor::Scroll::alignMiddle);
	tab.editor->SetFocus();
}

void ScriptEditor::DrawTextOverlay(ScriptTab& tab, ImDrawList* dl)
{
	// Runs inside the editor's child window
	m_EditorFocused = ImGui::IsWindowFocused();
	m_EditorHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	ImGuiWindow* w = ImGui::GetCurrentWindow();
	m_EditorRect = ImVec4(w->InnerRect.Min.x, w->InnerRect.Min.y, w->InnerRect.GetWidth(), w->InnerRect.GetHeight());

	TextEditor& e = *tab.editor;
	const float lineHeight = e.GetLineHeight();
	const float glyph = e.GetGlyphWidth();
	const int firstRow = (int)e.GetFirstVisibleRow();
	const int lastRow = (int)e.GetLastVisibleRow();

	// Squiggles under problems
	if (tab.compiled)
	{
		const auto& lines = tab.model.Lines();
		for (const auto& d : tab.compile.diagnostics)
		{
			int line = d.line - 1;
			if (line < 0 || line >= (int)lines.size() || !e.IsLineVisible(line)) continue;
			int row = (int)e.DocPos2VisPos(TextEditor::DocPos(line, 0)).row;
			if (row < firstRow - 1 || row > lastRow + 1) continue;
			const std::string& text = lines[line];
			int start, end;
			if (d.column > 0)
			{
				start = std::min(d.column - 1, (int)text.size());
				end = start;
				while (end < (int)text.size() && (std::isalnum((unsigned char)text[end]) || text[end] == '_' || text[end] == '"')) end++;
				if (end == start) end = start + 1;
				// a string argument: underline up to its closing quote
				if (start < (int)text.size() && (text[start] == '"' || text[start] == '\''))
				{
					size_t close = text.find(text[start], start + 1);
					end = close == std::string::npos ? (int)text.size() : (int)close + 1;
				}
			}
			else
			{
				start = (int)LeadingWhitespace(text).size();
				end = std::max(start + 1, (int)Trimmed(text).size() + start);
			}
			ImVec2 p0 = e.DocPosToScreenPos(TextEditor::DocPos(line, start));
			float x1 = p0.x + (end - start) * glyph;
			ImVec4 c = SeverityColor(d.severity);
			ZigZag(dl, p0.x, x1, p0.y + lineHeight - 2.0f, ImGui::ColorConvertFloat4ToU32(c));
		}
	}

	// Flash the line a drop / completion just wrote
	double now = ImGui::GetTime();
	if (m_FlashLine >= 0 && now < m_FlashUntil && m_FlashLine < (int)e.GetLineCount())
	{
		float alpha = (float)((m_FlashUntil - now) / 1.2) * 0.35f;
		ImVec2 p = e.DocPosToScreenPos(TextEditor::DocPos(m_FlashLine, 0));
		dl->AddRectFilled(ImVec2(p.x - 4.0f, p.y), ImVec2(w->InnerRect.Max.x, p.y + lineHeight), ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, alpha)));
	}

	// Drop indicator while dragging something code can reference
	const ImGuiPayload* payload = ImGui::GetDragDropPayload();
	ScriptReference::Payload ref;
	if (payload && m_Scene && ImGui::IsMouseHoveringRect(w->InnerRect.Min, w->InnerRect.Max, false) && PayloadForDrop(*m_Scene, payload, ref))
	{
		ImVec2 mouse = ImGui::GetMousePos();
		DropTarget target = ComputeDrop(tab, mouse);
		bool append = target.append;
		ImU32 accent = ImGui::GetColorU32(kAccent);
		auto label = [&](ImVec2 at, const std::string& hint) {
			ImVec2 ts = ImGui::CalcTextSize(hint.c_str());
			ImVec2 hp(std::min(at.x, w->InnerRect.Max.x - ts.x - 18.0f), at.y - ts.y - 3.0f);
			if (hp.y < w->InnerRect.Min.y) hp.y = at.y + lineHeight + 3.0f;
			dl->AddRectFilled(ImVec2(hp.x - 6.0f, hp.y - 1.0f), ImVec2(hp.x + ts.x + 6.0f, hp.y + ts.y + 1.0f), IM_COL32(40, 30, 60, 235), 4.0f);
			dl->AddText(hp, IM_COL32(220, 210, 255, 255), hint.c_str());
		};
		if (target.inlineExpression)
		{
			ImVec2 p = e.DocPosToScreenPos(TextEditor::DocPos(target.line, target.column));
			dl->AddRectFilled(ImVec2(p.x - 1.0f, p.y), ImVec2(p.x + 2.0f, p.y + lineHeight), accent);
			label(p, ResolveSelfReference(ref.expression, ref.objectName) + "   (Ctrl: new line instead)");
		}
		else
		{
			int line = append ? (int)e.GetLineCount() - 1 : target.line;
			ImVec2 p = e.DocPosToScreenPos(TextEditor::DocPos(line, 0));
			bool blank = target.blankLine;
			if (blank)
				dl->AddRectFilled(ImVec2(p.x - 4.0f, p.y), ImVec2(w->InnerRect.Max.x, p.y + lineHeight), ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f)));
			else
			{
				float y = append ? p.y + lineHeight : p.y;
				dl->AddLine(ImVec2(p.x - 4.0f, y), ImVec2(w->InnerRect.Max.x - 8.0f, y), accent, 2.0f);
				dl->AddTriangleFilled(ImVec2(p.x - 10.0f, y - 5.0f), ImVec2(p.x - 10.0f, y + 5.0f), ImVec2(p.x - 3.0f, y), accent);
			}
			label(ImVec2(w->InnerRect.Max.x, append ? p.y + lineHeight : p.y),
				"local " + UniqueVariable(tab, ref.variable) + " = " + ResolveSelfReference(ref.expression, ref.objectName) + (append || blank ? "" : "   (Ctrl: inline)"));
		}
	}
}

// =========================================================================
// Render
// =========================================================================

void ScriptEditor::Render(SceneManager& scene, bool& isOpen)
{
	m_Scene = &scene;

	if (m_RestoreOpen) { isOpen = true; m_RestoreOpen = false; }
	if (!s_PendingOpen.empty())
	{
		for (const auto& p : s_PendingOpen) OpenFile(p);
		s_PendingOpen.clear();
		isOpen = true;
		m_FocusRequested = true;
	}

	// Persist tabs / layout / visibility when they change (checked twice a second)
	m_LastOpen = isOpen;
	if (ImGui::GetTime() >= m_SessionCheckAt)
	{
		m_SessionCheckAt = ImGui::GetTime() + 0.5;
		std::string signature = SessionSignature();
		if (signature != m_SessionSignature)
		{
			if (!m_SessionSignature.empty()) SaveSession();
			m_SessionSignature = signature;
		}
	}

	if (!isOpen) return;

	ImGui::SetNextWindowSize(ImVec2(1100.0f, 700.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f, 300.0f), ImVec2(FLT_MAX, FLT_MAX));
	if (m_PendingMonitor >= 0)
	{
		const auto& monitors = ImGui::GetPlatformIO().Monitors;
		if (m_PendingMonitor < monitors.Size)
		{
			const ImGuiPlatformMonitor& m = monitors[m_PendingMonitor];
			ImGui::SetNextWindowPos(ImVec2(m.WorkPos.x + m.WorkSize.x * 0.06f, m.WorkPos.y + m.WorkSize.y * 0.06f));
			ImGui::SetNextWindowSize(ImVec2(m.WorkSize.x * 0.88f, m.WorkSize.y * 0.88f));
			m_FocusRequested = true;
		}
		m_PendingMonitor = -1;
	}
	if (m_PopOut)
	{
		// Its own OS window even when it overlaps the main window
		ImGuiWindowClass windowClass;
		windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
		ImGui::SetNextWindowClass(&windowClass);
	}
	if (m_FocusRequested)
	{
		ImGui::SetNextWindowFocus();
		m_FocusRequested = false;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
	bool visible = ImGui::Begin("Script Editor###ScriptEditor", &isOpen, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	if (!visible)
	{
		ImGui::End();
		return;
	}

	ScriptTab* active = GetActiveTab();
	RefreshEnvironment(scene, active);
	CheckDiskChanges();

	// Debounced compile of every tab that changed
	double now = ImGui::GetTime();
	for (auto& tab : m_Tabs)
		if (tab.compileDue >= 0.0 && now >= tab.compileDue) Recompile(tab);

	RenderMenuBar(scene);
	HandleShortcuts();
	RenderToolbar();

	const float statusHeight = ImGui::GetFrameHeight();
	ImVec2 avail = ImGui::GetContentRegionAvail();
	float bodyHeight = std::max(100.0f, avail.y - statusHeight - ImGui::GetStyle().ItemSpacing.y);

	ImGui::BeginChild("##ScriptBody", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	{
		// Panes sit edge to edge (no spacing between them); their content uses the normal spacing.
		// Every push / pop stays inside one window: ImGui checks the style stack per window.
		const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		if (m_ShowExplorer)
		{
			float maxWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x - 320.0f);
			m_ExplorerWidth = std::clamp(m_ExplorerWidth, 160.0f, maxWidth);
			ImGui::BeginChild("##Explorer", ImVec2(m_ExplorerWidth, 0.0f), ImGuiChildFlags_None);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spacing);
			RenderExplorer(scene);
			ImGui::PopStyleVar();
			ImGui::EndChild();
			ImGui::SameLine();
			Splitter("##ExplorerSplit", true, &m_ExplorerWidth, 160.0f, maxWidth);
			ImGui::SameLine();
		}

		ImGui::BeginChild("##Main", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spacing);
		{
			float mainHeight = ImGui::GetContentRegionAvail().y;
			float maxProblems = std::max(60.0f, mainHeight - 150.0f);
			m_ProblemsHeight = std::clamp(m_ProblemsHeight, 60.0f, maxProblems);
			float editorHeight = m_ShowProblems ? mainHeight - m_ProblemsHeight - 5.0f : mainHeight;

			ImGui::BeginChild("##EditorHost", ImVec2(0.0f, editorHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			RenderTabBar();
			RenderEditorCanvas(scene);
			ImGui::EndChild();

			if (m_ShowProblems)
			{
				Splitter("##ProblemsSplit", false, &m_ProblemsHeight, 60.0f, maxProblems, -1.0f);
				ImGui::BeginChild("##Problems", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
				RenderProblems();
				ImGui::EndChild();
			}
		}
		ImGui::PopStyleVar();
		ImGui::EndChild();
		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	RenderStatusBar();
	RenderCloseConfirm();
	ImGui::End();
}

void ScriptEditor::RenderMenuBar(SceneManager& scene)
{
	(void)scene;
	if (!ImGui::BeginMenuBar()) return;

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New Script", "Ctrl+N")) NewScript();
		if (ImGui::MenuItem("Open Script...", "Ctrl+O"))
		{
			std::string path = SceneSerializer::OpenFileDialog("Lua Script (*.lua)\0*.lua\0All Files (*.*)\0*.*\0", "Open Lua Script");
			if (!path.empty()) OpenFile(NormalizePath(path));
		}
		if (ImGui::BeginMenu("Open Project Script", !m_Env.scriptFiles.empty()))
		{
			for (const auto& f : m_Env.scriptFiles)
				if (ImGui::MenuItem(f.c_str())) OpenFile(f);
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Save", "Ctrl+S", false, GetActiveTab() != nullptr)) SaveActiveScript();
		if (ImGui::MenuItem("Save As...", nullptr, false, GetActiveTab() != nullptr)) SaveScriptAs();
		if (ImGui::MenuItem("Save All", "Ctrl+Shift+S", false, !m_Tabs.empty())) SaveAll();
		ImGui::Separator();
		if (ImGui::MenuItem("Close Script", "Ctrl+W", false, GetActiveTab() != nullptr))
		{
			if (GetActiveTab()->isDirty) m_CloseConfirmTab = m_ActiveTabIndex; else CloseActiveTab();
		}
		ImGui::EndMenu();
	}

	ScriptTab* tab = GetActiveTab();
	TextEditor* e = tab ? tab->editor.get() : nullptr;
	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, e && e->CanUndo())) e->Undo();
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, e && e->CanRedo())) e->Redo();
		ImGui::Separator();
		if (ImGui::MenuItem("Find / Replace", "Ctrl+F", false, e != nullptr)) e->OpenFindReplaceWindow();
		if (ImGui::MenuItem("Toggle Comment", "Ctrl+/", false, e != nullptr)) e->ToggleComments();
		if (ImGui::MenuItem("Trigger Completion", "Ctrl+Space", false, tab != nullptr)) { if (tab) { OpenCompletion(*tab, true); if (e) e->SetFocus(); } }
		ImGui::Separator();
		if (ImGui::BeginMenu("Insert"))
		{
			if (ImGui::MenuItem("Start() callback")) InsertAtCursor("function Start()\n\nend\n");
			if (ImGui::MenuItem("Update(dt) callback")) InsertAtCursor("function Update(dt)\n\nend\n");
			ImGui::Separator();
			if (ImGui::MenuItem("This object (gameObject)")) InsertAtCursor("local self = gameObject\n");
			if (ImGui::MenuItem("Texture file..."))
			{
				std::string path = SceneSerializer::OpenFileDialog("Texture Files (*.png;*.jpg;*.jpeg;*.tga;*.hdr)\0*.png;*.jpg;*.jpeg;*.tga;*.hdr\0All Files (*.*)\0*.*\0", "Select Texture");
				ScriptReference::Payload ref;
				if (!path.empty() && ScriptReference::DescribeAsset(path, ref) && tab)
				{
					EnsureEditor(*tab);
					InsertReference(*tab, ref, (int)tab->editor->GetMainCursorPosition().line, 0, false);
				}
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("View"))
	{
		ImGui::MenuItem("Explorer", nullptr, &m_ShowExplorer);
		ImGui::MenuItem("Problems", nullptr, &m_ShowProblems);
		ImGui::Separator();
		if (ImGui::MenuItem("Zoom In", "Ctrl+=")) m_FontSize = std::min(36.0f, m_FontSize + 1.0f);
		if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) m_FontSize = std::max(10.0f, m_FontSize - 1.0f);
		if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) m_FontSize = 16.0f;
		if (e)
		{
			bool ws = e->IsShowWhitespacesEnabled();
			if (ImGui::MenuItem("Show Whitespace", nullptr, &ws)) for (auto& t : m_Tabs) if (t.editor) t.editor->SetShowWhitespacesEnabled(ws);
			bool minimap = e->IsShowMiniMapEnabled();
			if (ImGui::MenuItem("Minimap", nullptr, &minimap)) for (auto& t : m_Tabs) if (t.editor) t.editor->SetShowMiniMapEnabled(minimap);
		}
		ImGui::Separator();
		ImGui::MenuItem("Separate OS Window", nullptr, &m_PopOut);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep the Script Editor in its own window,\neven when it overlaps the main window.");
		const auto& monitors = ImGui::GetPlatformIO().Monitors;
		if (ImGui::BeginMenu("Move to Monitor", monitors.Size > 0))
		{
			for (int i = 0; i < monitors.Size; i++)
			{
				char label[96];
				snprintf(label, sizeof(label), "Monitor %d  (%dx%d)", i + 1, (int)monitors[i].MainSize.x, (int)monitors[i].MainSize.y);
				if (ImGui::MenuItem(label)) m_PendingMonitor = i;
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Build"))
	{
		if (ImGui::MenuItem("Compile", "F7", false, tab != nullptr) && tab) Recompile(*tab);
		if (ImGui::MenuItem("Compile All Open Scripts", "Ctrl+F7", false, !m_Tabs.empty())) for (auto& t : m_Tabs) Recompile(t);
		ImGui::EndMenu();
	}
	ImGui::EndMenuBar();
}

void ScriptEditor::RenderToolbar()
{
	ScriptTab* tab = GetActiveTab();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

	if (ImGui::Button("New")) NewScript();
	ImGui::SameLine();
	if (ImGui::Button("Open"))
	{
		std::string path = SceneSerializer::OpenFileDialog("Lua Script (*.lua)\0*.lua\0All Files (*.*)\0*.*\0", "Open Lua Script");
		if (!path.empty()) OpenFile(NormalizePath(path));
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!tab || !tab->isDirty);
	if (ImGui::Button("Save")) SaveActiveScript();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x * 0.55f, kAccent.y * 0.55f, kAccent.z * 0.55f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(kAccent.x * 0.75f, kAccent.y * 0.75f, kAccent.z * 0.75f, 1.0f));
	ImGui::BeginDisabled(!tab);
	if (ImGui::Button("Compile  F7") && tab) Recompile(*tab);
	ImGui::EndDisabled();
	ImGui::PopStyleColor(2);
	ImGui::SameLine();

	if (tab && tab->compiled)
	{
		const auto& r = tab->compile;
		char buf[96];
		if (r.errors > 0)
		{
			snprintf(buf, sizeof(buf), "%d error%s%s", r.errors, r.errors == 1 ? "" : "s", r.warnings ? "" : "");
			Chip(buf, kErrorColor);
			if (r.warnings) { ImGui::SameLine(); snprintf(buf, sizeof(buf), "%d warning%s", r.warnings, r.warnings == 1 ? "" : "s"); Chip(buf, kWarningColor); }
		}
		else if (r.warnings > 0)
		{
			snprintf(buf, sizeof(buf), "Compiles  -  %d warning%s", r.warnings, r.warnings == 1 ? "" : "s");
			Chip(buf, kWarningColor);
		}
		else
			Chip("Compiled  -  no problems", kOkColor);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lua syntax + engine checks, %.2f ms\nChecked automatically while you type.", r.milliseconds);
	}
	else if (tab)
		Chip("Not compiled yet", ImVec4(0.6f, 0.6f, 0.65f, 1.0f));

	// Right side: view toggles + zoom
	const float width = 300.0f;
	ImGui::SameLine();
	float freeSpace = ImGui::GetContentRegionAvail().x;
	if (freeSpace > width) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + freeSpace - width);
	if (ImGui::Button(m_ShowExplorer ? "Explorer <" : "Explorer >")) m_ShowExplorer = !m_ShowExplorer;
	ImGui::SameLine();
	if (ImGui::Button("Problems")) m_ShowProblems = !m_ShowProblems;
	ImGui::SameLine();
	if (ImGui::Button("A-")) m_FontSize = std::max(10.0f, m_FontSize - 1.0f);
	ImGui::SameLine();
	if (ImGui::Button("A+")) m_FontSize = std::min(36.0f, m_FontSize + 1.0f);
	ImGui::SameLine();
	if (ImGui::Button(m_PopOut ? "Docked" : "Pop out")) m_PopOut = !m_PopOut;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_PopOut ? "Let the editor merge back into the main window" : "Make the editor a separate OS window (drag it to another monitor)");

	ImGui::PopStyleVar(2);
	ImGui::Separator();
}

// =========================================================================
// Explorer: scene references, scripts, API
// =========================================================================

void ScriptEditor::RenderExplorer(SceneManager& scene)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
	if (ImGui::BeginTabBar("##ExplorerTabs", ImGuiTabBarFlags_FittingPolicyShrink))
	{
		if (ImGui::BeginTabItem("Scene")) { m_ExplorerTab = 0; ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Scripts")) { m_ExplorerTab = 1; ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("API")) { m_ExplorerTab = 2; ImGui::EndTabItem(); }
		ImGui::EndTabBar();
	}
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##ExplorerFilter", "Filter...", m_ExplorerFilter, sizeof(m_ExplorerFilter));
	ImGui::PopStyleVar();

	ImGui::BeginChild("##ExplorerList", ImVec2(0, 0), ImGuiChildFlags_None);
	if (m_ExplorerTab == 0) RenderSceneTree(scene);
	else if (m_ExplorerTab == 1)
	{
		ImGui::TextDisabled("Drag into code: Script.Require(...)");
		std::string filter = LowerCopy(m_ExplorerFilter);
		for (const auto& f : m_Env.scriptFiles)
		{
			if (!filter.empty() && LowerCopy(f).find(filter) == std::string::npos) continue;
			ImVec2 p = ImGui::GetCursorScreenPos();
			float sz = ImGui::GetTextLineHeight();
			ImGui::Dummy(ImVec2(sz + 4.0f, sz));
			Badge(ImGui::GetWindowDrawList(), p, sz, KindColor(Kind::ScriptMember), "S");
			ImGui::SameLine();
			bool isOpenTab = std::any_of(m_Tabs.begin(), m_Tabs.end(), [&](const ScriptTab& t) { return NormalizePath(t.path) == f; });
			if (isOpenTab) ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
			ImGui::Selectable(FileNameOf(f).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
			if (isOpenTab) ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s\nDouble click: open   Drag: reference in code", f.c_str()); if (ImGui::IsMouseDoubleClicked(0)) OpenFile(f); }
			ScriptReference::Payload ref;
			if (ScriptReference::DescribeAsset(f, ref)) ScriptReference::DragSource(ref);
		}
	}
	else RenderApiReference();
	ImGui::EndChild();
}

void ScriptEditor::RenderSceneTree(SceneManager& scene)
{
	ImGui::TextDisabled("Drag anything into the code");
	std::string filter = LowerCopy(m_ExplorerFilter);
	ScriptTab* tab = GetActiveTab();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float sz = ImGui::GetTextLineHeight();
	auto& reg = ScriptApi::Registry::Get();

	// One row of the tree: badge, label, type on the right; draggable; click / double click actions.
	// Returns true when an expandable row is open (caller must TreePop).
	auto row = [&](const char* id, const char* letter, ImU32 color, const std::string& label, const std::string& detail,
	               const ScriptReference::Payload& ref, bool expandable, const std::function<void()>& onClick, bool highlight = false) -> bool {
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
		if (!expandable) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (highlight) ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
		bool open = ImGui::TreeNodeEx(id, flags, "      %s", label.c_str());
		if (highlight) ImGui::PopStyleColor();
		ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
		Badge(dl, ImVec2(rmin.x + ImGui::GetTreeNodeToLabelSpacing(), rmin.y + (rmax.y - rmin.y - sz) * 0.5f), sz, color, letter);
		if (!detail.empty())
		{
			ImVec2 ts = ImGui::CalcTextSize(detail.c_str());
			float labelEnd = rmin.x + ImGui::GetTreeNodeToLabelSpacing() + ImGui::CalcTextSize(("      " + label).c_str()).x + 12.0f;
			float x = rmax.x - ts.x - 6.0f;
			if (x > labelEnd) dl->AddText(ImVec2(x, rmin.y + (rmax.y - rmin.y - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), detail.c_str());
		}
		ScriptReference::DragSource(ref);
		if (ImGui::IsItemHovered())
		{
			std::string expr = ResolveSelfReference(ref.expression, ref.objectName);
			ImGui::SetTooltip("%s\n\nDrag onto an empty line:  local %s = ...\nDrag onto code:  inserted right there\nDouble click: insert at the caret%s",
				expr.c_str(), ref.variable, onClick ? "\nClick: open" : "");
			bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 16.0f;
			bool onArrow = ImGui::GetMousePos().x < rmin.x + ImGui::GetTreeNodeToLabelSpacing();
			if (onClick && released && !onArrow) onClick();
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && tab && !onClick)
			{
				EnsureEditor(*tab);
				InsertReference(*tab, ref, (int)tab->editor->GetMainCursorPosition().line, 0, false);
			}
		}
		return expandable && open;
	};

	// Types worth expanding: registered value types with properties (not other objects: no cycles)
	auto expandableType = [&](const std::string& type) {
		if (type.empty() || type == "GameObject" || type == "Texture" || type == "Model" || type == "Component" || !ScriptApi::ElementType(type).empty()) return false;
		const ScriptApi::Type* t = reg.FindType(type);
		if (!t || t->isTable) return false;
		std::vector<const ScriptApi::Member*> members;
		reg.CollectMembers(type, members);
		return std::any_of(members.begin(), members.end(), [](const ScriptApi::Member* m) { return m->kind == Kind::Property; });
	};

	// Properties of 'type' reached through 'expr', recursively (transform > rotation > x)
	std::function<void(const std::string&, const std::string&, const std::string&, const std::string&, int)> properties;
	properties = [&](const std::string& expr, const std::string& varBase, const std::string& type, const std::string& objectName, int depth) {
		std::vector<ScriptApi::Member> members;
		ScriptAnalysis::CollectMembers(type, &m_Env, members);
		for (const auto& m : members)
		{
			if (m.kind != Kind::Property || !ScriptApi::ElementType(m.type).empty() || m.type == "GameObject" || m.name == "gameObject") continue;
			std::string childExpr = expr + "." + m.name;
			ScriptReference::Payload ref = ScriptReference::Make(childExpr, ScriptReference::MakeVariableName(varBase, m.name), childExpr, objectName);
			bool expandable = depth < 3 && expandableType(m.type);
			if (row(m.name.c_str(), "p", KindColor(Kind::Property), m.name, ScriptAnalysis::DisplayType(m.type), ref, expandable, nullptr))
			{
				properties(childExpr, ScriptReference::MakeVariableName(varBase, m.name), m.type, objectName, depth + 1);
				ImGui::TreePop();
			}
		}
	};

	auto registryType = [](Component* c) -> std::string {
		if (dynamic_cast<LuaScriptComponent*>(c)) return "LuaScript";
		const ScriptApi::Type* t = ScriptApi::Registry::Get().FindType(c->GetName());
		return t ? t->name : "Component";
	};

	std::function<bool(GameObject*)> matches = [&](GameObject* go) -> bool {
		if (filter.empty() || LowerCopy(go->GetName()).find(filter) != std::string::npos) return true;
		for (GameObject* c : go->GetChildren()) if (c && matches(c)) return true;
		return false;
	};

	std::function<void(GameObject*)> node = [&](GameObject* go) {
		if (!go || !matches(go)) return;
		ImGui::PushID(go);
		if (!filter.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		const std::string name = go->GetName();
		const std::string objExpr = ScriptReference::ObjectExpression(go);
		bool selfObject = std::find(m_Env.selfObjects.begin(), m_Env.selfObjects.end(), name) != m_Env.selfObjects.end();
		ScriptReference::Payload objRef = ScriptReference::Make(objExpr, ScriptReference::MakeVariableName(name), name, name);
		if (row("##obj", "O", KindColor(Kind::Global), name + (selfObject ? "  (this script)" : ""), "GameObject", objRef, true, nullptr, selfObject))
		{
			// Transform / Material / layers, each with its properties
			if (row("##transform", "T", KindColor(Kind::Property), "Transform", "Transform",
					ScriptReference::Make(objExpr + ".transform", ScriptReference::MakeVariableName(name, "Transform"), name + " > Transform", name), true, nullptr))
			{
				properties(objExpr + ".transform", ScriptReference::MakeVariableName(name), "Transform", name, 1);
				ImGui::TreePop();
			}
			if (go->GetMaterial() && row("##material", "M", KindColor(Kind::Global), "Material", "Material",
					ScriptReference::Make(objExpr + ".material", ScriptReference::MakeVariableName(name, "Material"), name + " > Material", name), true, nullptr))
			{
				properties(objExpr + ".material", ScriptReference::MakeVariableName(name, "Material"), "Material", name, 1);
				ImGui::TreePop();
			}
			for (int i = 0; i < (int)go->GetTextureLayers().size(); i++)
			{
				const TextureLayer& layer = go->GetTextureLayers()[i];
				std::string label = "Layer " + std::to_string(i) + (layer.texturePath.empty() ? "" : "  " + FileNameOf(layer.texturePath));
				std::string expr = objExpr + ":GetLayer(" + std::to_string(i) + ")";
				std::string var = ScriptReference::MakeVariableName(name, "Layer" + std::to_string(i));
				ImGui::PushID(i);
				if (row("##layer", "L", KindColor(Kind::Constant), label, "TextureLayer", ScriptReference::Make(expr, var, name + " > " + label, name), true, nullptr))
				{
					properties(expr, var, "TextureLayer", name, 1);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// Components: their properties; scripts: their functions / variables, click opens the file
			for (const auto& comp : go->GetComponents())
			{
				ScriptReference::Payload cr;
				if (!ScriptReference::DescribeComponent(go, comp.get(), cr)) continue;
				ImGui::PushID(comp.get());
				if (auto* script = dynamic_cast<LuaScriptComponent*>(comp.get()))
				{
					std::string path = ScriptEngine::ResolveScriptPath(script->GetScriptPath());
					if (path.empty()) path = script->GetScriptPath();
					std::string scriptName = script->GetScriptName();
					if (row("##script", "S", KindColor(Kind::ScriptMember), scriptName + ".lua", "script", cr, true, [this, path] { OpenFile(path); m_FocusRequested = true; }))
					{
						if (const auto* members = ScriptAnalysis::ScriptMembers(path))
						{
							for (const auto& m : *members)
							{
								std::string expr = std::string(cr.expression) + "." + m.name;
								ScriptReference::Payload mr = ScriptReference::Make(expr, ScriptReference::MakeVariableName(scriptName, m.name), expr, name);
								row(m.name.c_str(), m.IsCallable() ? "f" : "v", KindColor(m.IsCallable() ? Kind::Function : Kind::Local), m.name,
									m.IsCallable() ? m.params : ScriptAnalysis::DisplayType(m.type), mr, false, nullptr);
							}
						}
						ImGui::TreePop();
					}
				}
				else
				{
					std::string type = registryType(comp.get());
					bool expandable = expandableType(type);
					if (row("##comp", "C", KindColor(Kind::Method), comp->GetName(), type, cr, expandable, nullptr))
					{
						properties(cr.expression, cr.variable, type, name, 1);
						ImGui::TreePop();
					}
				}
				ImGui::PopID();
			}
			for (GameObject* child : go->GetChildren()) node(child);
			ImGui::TreePop();
		}
		ImGui::PopID();
	};

	for (GameObject* go : scene.GetObjects())
		if (go && !go->GetParent()) node(go);

	// Lights
	auto& lights = scene.GetLights();
	if (!lights.empty())
	{
		ImGui::Spacing();
		ImGui::TextDisabled("Lights");
		for (LightObject* light : lights)
		{
			if (!light) continue;
			if (!filter.empty() && LowerCopy(light->GetName()).find(filter) == std::string::npos) continue;
			ImGui::PushID(light);
			const char* kind = light->GetLightType() == LightType::Directional ? "directional" : light->GetLightType() == LightType::Point ? "point light" : "spot light";
			std::string expr = ScriptReference::LightExpression(light);
			std::string var = ScriptReference::MakeVariableName(light->GetName(), "Light");
			if (row("##light", "*", IM_COL32(255, 210, 90, 255), light->GetName(), kind, ScriptReference::Make(expr, var, light->GetName(), ""), true, nullptr))
			{
				// only the properties this kind of light has
				std::vector<ScriptApi::Member> members;
				ScriptAnalysis::CollectMembers("Light", &m_Env, members);
				for (const auto& m : members)
				{
					if (m.kind != Kind::Property) continue;
					bool directional = light->GetLightType() == LightType::Directional;
					bool point = light->GetLightType() == LightType::Point;
					if (directional && (m.name == "position" || m.name == "constant" || m.name == "linear" || m.name == "exponent" || m.name == "edge")) continue;
					if (!directional && (m.name == "pitch" || m.name == "yaw")) continue;
					if (point && (m.name == "direction" || m.name == "edge")) continue;
					std::string childExpr = expr + "." + m.name;
					std::string childVar = ScriptReference::MakeVariableName(light->GetName(), m.name);
					bool expandable = expandableType(m.type);
					if (row(m.name.c_str(), "p", KindColor(Kind::Property), m.name, ScriptAnalysis::DisplayType(m.type),
							ScriptReference::Make(childExpr, childVar, childExpr, ""), expandable, nullptr))
					{
						properties(childExpr, childVar, m.type, "", 2);
						ImGui::TreePop();
					}
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}
}

void ScriptEditor::RenderApiReference()
{
	ImGui::TextDisabled("Everything scripts can use");
	std::string filter = LowerCopy(m_ExplorerFilter);
	auto& reg = ScriptApi::Registry::Get();

	auto memberRow = [&](const ScriptApi::Member& m) {
		ImVec2 p = ImGui::GetCursorScreenPos();
		float sz = ImGui::GetTextLineHeight();
		ImGui::Dummy(ImVec2(sz + 4.0f, sz));
		Badge(ImGui::GetWindowDrawList(), p, sz, KindColor(m.kind), KindLetter(m.kind));
		ImGui::SameLine();
		ImGui::Selectable(m.Signature().c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushStyleColor(ImGuiCol_Text, kCodeColor);
			ImGui::TextUnformatted(m.Signature().c_str());
			ImGui::PopStyleColor();
			if (!m.doc.empty()) { ImGui::PushTextWrapPos(380.0f); ImGui::TextUnformatted(m.doc.c_str()); ImGui::PopTextWrapPos(); }
			ImGui::TextDisabled("Double click: insert at the caret");
			ImGui::EndTooltip();
			if (ImGui::IsMouseDoubleClicked(0)) InsertAtCursor(m.name);
		}
	};

	if (ImGui::TreeNodeEx("Globals", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (const auto& g : reg.Globals())
			if (filter.empty() || LowerCopy(g.name).find(filter) != std::string::npos) memberRow(g);
		ImGui::TreePop();
	}
	for (const auto& t : reg.Types())
	{
		bool typeMatches = filter.empty() || LowerCopy(t.name).find(filter) != std::string::npos;
		bool anyMember = typeMatches || std::any_of(t.members.begin(), t.members.end(), [&](const ScriptApi::Member& m) { return LowerCopy(m.name).find(filter) != std::string::npos; });
		if (!anyMember) continue;
		if (!filter.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		bool open = ImGui::TreeNodeEx(t.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s%s", t.name.c_str(), t.base.empty() ? "" : ("  : " + t.base).c_str());
		if (ImGui::IsItemHovered() && !t.doc.empty()) ImGui::SetTooltip("%s", t.doc.c_str());
		if (open)
		{
			for (const auto& m : t.members)
				if (typeMatches || LowerCopy(m.name).find(filter) != std::string::npos) memberRow(m);
			ImGui::TreePop();
		}
	}
}

// =========================================================================
// Tabs, editor canvas, problems, status
// =========================================================================

void ScriptEditor::RenderTabBar()
{
	if (m_Tabs.empty()) return;

	if (ImGui::BeginTabBar("##ScriptTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
	{
		for (int i = 0; i < static_cast<int>(m_Tabs.size()); ++i)
		{
			ScriptTab& t = m_Tabs[i];
			bool tabOpen = true;
			std::string label = t.title + "###ScriptTab_" + std::to_string(t.uid);
			ImGuiTabItemFlags flags = t.isDirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
			if (m_PendingTabSelect == i) flags |= ImGuiTabItemFlags_SetSelected;

			bool hasErrors = t.compiled && t.compile.errors > 0;
			bool hasWarnings = t.compiled && t.compile.warnings > 0;
			if (hasErrors) ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
			else if (hasWarnings) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
			bool selected = ImGui::BeginTabItem(label.c_str(), &tabOpen, flags);
			if (hasErrors || hasWarnings) ImGui::PopStyleColor();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s%s", t.path.empty() ? "(not saved yet)" : t.path.c_str(),
					hasErrors ? "\nHas errors" : hasWarnings ? "\nHas warnings" : "");
			if (selected)
			{
				if (m_PendingTabSelect < 0 || m_PendingTabSelect == i)
				{
					if (m_ActiveTabIndex != i) m_Completion.open = false;
					m_ActiveTabIndex = i;
				}
				ImGui::EndTabItem();
			}
			if (!tabOpen)
			{
				if (t.isDirty) m_CloseConfirmTab = i;
				else { CloseTab(i); break; }
			}
		}

		// Accept script drops on the tab bar to open as a new tab
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_PATH"))
			{
				const char* scriptPath = static_cast<const char*>(payload->Data);
				if (scriptPath && scriptPath[0] != '\0') OpenFile(scriptPath);
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::EndTabBar();
	}
	m_PendingTabSelect = -1;
}

void ScriptEditor::RenderEditorCanvas(SceneManager& scene)
{
	ScriptTab* tabPtr = GetActiveTab();
	if (!tabPtr)
	{
		ImVec2 avail = ImGui::GetContentRegionAvail();
		const char* msg = "No script open.  Ctrl+N: new   Ctrl+O: open   or drop a script here";
		ImVec2 ts = ImGui::CalcTextSize(msg);
		ImGui::SetCursorPos(ImVec2(std::max(0.0f, (avail.x - ts.x) * 0.5f), avail.y * 0.45f));
		ImGui::TextDisabled("%s", msg);
		ImGui::SetCursorPos(ImVec2(0, 0));
		ImGui::InvisibleButton("##EmptyDrop", ImVec2(std::max(1.0f, avail.x), std::max(1.0f, avail.y)));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_PATH"))
			{
				const char* scriptPath = static_cast<const char*>(payload->Data);
				if (scriptPath && scriptPath[0] != '\0') OpenFile(scriptPath);
			}
			ImGui::EndDragDropTarget();
		}
		return;
	}

	ScriptTab& tab = *tabPtr;
	EnsureEditor(tab);
	TextEditor& e = *tab.editor;

	// Characters typed this frame (the editor consumes the queue)
	m_TypedThisFrame.assign(ImGui::GetIO().InputQueueCharacters.begin(), ImGui::GetIO().InputQueueCharacters.end());

	// Ctrl + wheel over the code: zoom
	if (m_EditorHovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f)
	{
		m_FontSize = std::clamp(m_FontSize + ImGui::GetIO().MouseWheel, 10.0f, 36.0f);
		ImGui::GetIO().MouseWheel = 0.0f;
	}

	m_EditorFocused = false;
	m_EditorHovered = false;
	ImGui::PushFont(s_CodeFont, m_FontSize);
	e.Render("##code", ImGui::GetContentRegionAvail());
	ImGui::PopFont();

	bool changedBefore = tab.lastUndoIndex != e.GetUndoIndex();
	SyncFromEditor(tab);

	HandleDrop(scene, tab);
	UpdateCompletion(tab, changedBefore);
	RenderCompletionPopup(tab);
	RenderHoverAndSignature(tab);
}

void ScriptEditor::RenderProblems()
{
	ScriptTab* tab = GetActiveTab();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("PROBLEMS");
	if (tab && tab->compiled)
	{
		const auto& r = tab->compile;
		ImGui::SameLine();
		ImGui::TextColored(r.errors ? kErrorColor : ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "%d errors", r.errors);
		ImGui::SameLine();
		ImGui::TextColored(r.warnings ? kWarningColor : ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "%d warnings", r.warnings);
		if (r.infos) { ImGui::SameLine(); ImGui::TextColored(kInfoColor, "%d info", r.infos); }
	}
	ImGui::Separator();
	ImGui::PopStyleVar();

	if (!tab || !tab->compiled) { ImGui::TextDisabled("Compiling..."); return; }
	if (tab->compile.diagnostics.empty())
	{
		ImGui::TextColored(kOkColor, "No problems in %s.", tab->title.c_str());
		ImGui::TextDisabled("Syntax is checked by the Lua compiler; scene objects, assets, scripts and members are checked against the engine.");
		return;
	}

	ImGui::BeginChild("##ProblemList", ImVec2(0, 0), ImGuiChildFlags_None);
	int i = 0;
	for (const auto& d : tab->compile.diagnostics)
	{
		ImGui::PushID(i++);
		ImVec2 p = ImGui::GetCursorScreenPos();
		float sz = ImGui::GetTextLineHeight();
		ImU32 color = ImGui::GetColorU32(SeverityColor(d.severity));
		char label[64];
		snprintf(label, sizeof(label), "##p");
		bool clicked = ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddCircleFilled(ImVec2(p.x + sz * 0.5f + 2.0f, p.y + sz * 0.5f), sz * 0.3f, color);
		char where[32];
		snprintf(where, sizeof(where), d.column > 0 ? "Ln %d, Col %d" : "Ln %d", d.line, d.column);
		dl->AddText(ImVec2(p.x + sz + 8.0f, p.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), where);
		dl->AddText(ImVec2(p.x + sz + 120.0f, p.y), ImGui::GetColorU32(ImGuiCol_Text), d.message.c_str());
		if (clicked) GoToLine(*tab, d.line, d.column);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nClick to go to line %d", d.message.c_str(), d.line);
		ImGui::PopID();
	}
	ImGui::EndChild();
}

void ScriptEditor::RenderStatusBar()
{
	ScriptTab* tab = GetActiveTab();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetCursorScreenPos();
	float h = ImGui::GetFrameHeight();
	float w = ImGui::GetContentRegionAvail().x;
	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(kAccent.x * 0.25f, kAccent.y * 0.25f, kAccent.z * 0.3f, 1.0f)), 3.0f);
	ImGui::SetCursorScreenPos(ImVec2(p.x + 8.0f, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f));
	if (tab)
	{
		ImGui::Text("Ln %d, Col %d", tab->cursorLine, tab->cursorColumn);
		ImGui::SameLine(0, 18);
		ImGui::TextDisabled("%d lines", (int)tab->model.Lines().size());
		ImGui::SameLine(0, 18);
		ImGui::TextDisabled("Lua 5.4");
		ImGui::SameLine(0, 18);
		ImGui::TextDisabled("%s", tab->path.empty() ? "unsaved" : tab->path.c_str());
		if (!m_Env.selfObjects.empty())
		{
			ImGui::SameLine(0, 18);
			std::string objs;
			for (const auto& o : m_Env.selfObjects) objs += (objs.empty() ? "" : ", ") + o;
			ImGui::TextColored(kAccent, "gameObject = %s", objs.c_str());
		}
		ImGui::SameLine(0, 18);
		if (!m_StatusMessage.empty() && ImGui::GetTime() < m_StatusUntil)
			ImGui::TextColored(m_StatusColor, "%s", m_StatusMessage.c_str());
		else
			ImGui::TextDisabled("%s", tab->isDirty ? "Modified  (Ctrl+S)" : "Saved");
		if (ImGui::GetPlatformIO().Monitors.Size > 1)
		{
			ImGui::SameLine(0, 18);
			ImGui::TextDisabled("Drag the title bar to move it to another monitor");
		}
	}
	else ImGui::TextDisabled("No active script");
	ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
	ImGui::Dummy(ImVec2(0, 0));
}

void ScriptEditor::RenderCloseConfirm()
{
	if (m_CloseConfirmTab >= 0 && !ImGui::IsPopupOpen("Unsaved changes###ScriptCloseConfirm"))
		ImGui::OpenPopup("Unsaved changes###ScriptCloseConfirm");
	if (ImGui::BeginPopupModal("Unsaved changes###ScriptCloseConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ScriptTab* t = GetTab(m_CloseConfirmTab);
		if (!t) { m_CloseConfirmTab = -1; ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
		ImGui::Text("Save changes to %s before closing?", t->title.c_str());
		ImGui::Spacing();
		if (ImGui::Button("Save", ImVec2(110, 0)))
		{
			bool ok = t->path.empty() ? SaveScriptAs(m_CloseConfirmTab) : t->Save();
			if (ok && !t->path.empty()) AfterSave(*t);
			if (ok) CloseTab(m_CloseConfirmTab);
			m_CloseConfirmTab = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Don't Save", ImVec2(110, 0))) { CloseTab(m_CloseConfirmTab); m_CloseConfirmTab = -1; ImGui::CloseCurrentPopup(); }
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_CloseConfirmTab = -1; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
}

void ScriptEditor::HandleShortcuts()
{
	if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;

	ImGuiIO& io = ImGui::GetIO();
	ScriptTab* tab = GetActiveTab();
	if (ImGui::IsKeyPressed(ImGuiKey_F7, false))
	{
		if (io.KeyCtrl) { for (auto& t : m_Tabs) Recompile(t); }
		else if (tab) Recompile(*tab);
	}
	if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_N, false)) NewScript();
		else if (ImGui::IsKeyPressed(ImGuiKey_O, false))
		{
			std::string path = SceneSerializer::OpenFileDialog("Lua Script (*.lua)\0*.lua\0All Files (*.*)\0*.*\0", "Open Lua Script");
			if (!path.empty()) OpenFile(NormalizePath(path));
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveActiveScript();
		else if (ImGui::IsKeyPressed(ImGuiKey_W, false) && tab)
		{
			if (tab->isDirty) m_CloseConfirmTab = m_ActiveTabIndex; else CloseActiveTab();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) m_FontSize = std::min(36.0f, m_FontSize + 1.0f);
		else if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) m_FontSize = std::max(10.0f, m_FontSize - 1.0f);
		else if (ImGui::IsKeyPressed(ImGuiKey_0, false)) m_FontSize = 16.0f;
	}
	else if (io.KeyCtrl && io.KeyShift && !io.KeyAlt)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveAll();
	}
}

// =========================================================================
// Completion
// =========================================================================

bool ScriptEditor::InterceptKeys()
{
	using A = CompletionState::Action;
	ImGuiIO& io = ImGui::GetIO();
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) { m_Completion.pending = A::Trigger; return true; }
	if (!m_Completion.open) return false;
	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { m_Completion.pending = A::Up; return true; }
	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { m_Completion.pending = A::Down; return true; }
	if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) { m_Completion.pending = A::PageUp; return true; }
	if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) { m_Completion.pending = A::PageDown; return true; }
	if (!m_Completion.items.empty() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Tab)))
	{
		m_Completion.pending = A::Accept;
		return true;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_Completion.pending = A::Close; return true; }
	return false;
}

void ScriptEditor::OpenCompletion(ScriptTab& tab, bool manual)
{
	if (!tab.editor) return;
	auto pos = tab.editor->GetMainCursorPosition();
	ScriptAnalysis::CompletionContext ctx;
	std::vector<Completion> items;
	bool ok = ScriptAnalysis::Complete(tab.model, (int)pos.line, (int)pos.index, manual, ctx, items);

	m_Completion.ctx = ctx;
	m_Completion.items = std::move(items);
	m_Completion.manual = manual;
	m_Completion.open = ok || manual;
	m_Completion.selected = 0; // the best match
	m_Completion.scrollToSelected = true;
}

void ScriptEditor::UpdateCompletion(ScriptTab& tab, bool textChanged)
{
	using A = CompletionState::Action;
	auto& c = m_Completion;
	int n = (int)c.items.size();
	switch (c.pending)
	{
	case A::Trigger: OpenCompletion(tab, true); break;
	case A::Up: if (n) { c.selected = (c.selected + n - 1) % n; c.scrollToSelected = true; } break;
	case A::Down: if (n) { c.selected = (c.selected + 1) % n; c.scrollToSelected = true; } break;
	case A::PageUp: if (n) { c.selected = std::max(0, c.selected - 8); c.scrollToSelected = true; } break;
	case A::PageDown: if (n) { c.selected = std::min(n - 1, c.selected + 8); c.scrollToSelected = true; } break;
	case A::Accept: AcceptCompletion(tab); break;
	case A::Close: c.open = false; break;
	default: break;
	}
	c.pending = A::None;

	if (textChanged && m_EditorFocused)
	{
		ImWchar last = m_TypedThisFrame.empty() ? 0 : m_TypedThisFrame.back();
		bool identChar = last == '_' || (last < 128 && std::isalnum((int)last));
		bool trigger = identChar || last == '.' || last == ':' || last == '"' || last == '\'' || last == '/';
		if (c.open) OpenCompletion(tab, c.manual && !(last == ' ' || last == '(' || last == ')' || last == ','));
		else if (trigger) OpenCompletion(tab, false);
		if (c.open && !c.manual && c.items.empty()) c.open = false;
	}

	if (c.open && tab.editor)
	{
		auto pos = tab.editor->GetMainCursorPosition();
		if ((int)pos.line != c.ctx.line || (int)pos.index < c.ctx.replaceStart) c.open = false;
	}
	if (c.open && !m_EditorFocused)
	{
		// Clicking the popup itself keeps it; focusing anything else closes it
		ImGuiWindow* popup = ImGui::FindWindowByName("##ScriptCompletion");
		ImGuiContext& g = *GImGui;
		bool popupActive = popup && (g.NavWindow == popup || g.HoveredWindow == popup || g.ActiveIdWindow == popup);
		if (!popupActive) c.open = false;
	}
}

void ScriptEditor::AcceptCompletion(ScriptTab& tab)
{
	auto& c = m_Completion;
	if (!tab.editor || c.selected < 0 || c.selected >= (int)c.items.size()) { c.open = false; return; }
	const Completion item = c.items[c.selected];
	const auto& ctx = c.ctx;
	TextEditor& e = *tab.editor;

	std::string lineText = e.GetLineText(ctx.line);
	int start = ctx.replaceStart;
	int end = std::min((int)e.GetMainCursorPosition().index, (int)lineText.size());
	if (!ctx.inString)
		while (end < (int)lineText.size() && (std::isalnum((unsigned char)lineText[end]) || lineText[end] == '_')) end++;

	std::string text = item.insertText;
	int caret = -1; // offset in 'text' where the caret goes
	if (item.kind == Kind::Snippet)
	{
		std::string indent = LeadingWhitespace(lineText);
		std::string expanded;
		for (char ch : text) { expanded.push_back(ch); if (ch == '\n') expanded += indent; }
		size_t marker = expanded.find("$0");
		if (marker != std::string::npos) { expanded.erase(marker, 2); caret = (int)marker; }
		text = expanded;
	}
	else if (!ctx.inString && item.callable && !(end < (int)lineText.size() && lineText[end] == '('))
	{
		text += "()";
		caret = item.hasParams ? (int)text.size() - 1 : (int)text.size();
	}
	if (item.fixSeparator && ctx.separatorCol >= 0)
	{
		start = ctx.separatorCol;
		text = ":" + text;
		if (caret >= 0) caret++;
	}
	if (caret < 0) caret = (int)text.size();

	e.ReplaceSectionText(TextEditor::DocPos(ctx.line, start), TextEditor::DocPos(ctx.line, end), text);
	std::string head = text.substr(0, caret);
	int newLines = (int)std::count(head.begin(), head.end(), '\n');
	size_t lastNl = head.find_last_of('\n');
	int col = lastNl == std::string::npos ? start + caret : (int)(head.size() - lastNl - 1);
	e.SetCursor(TextEditor::DocPos(ctx.line + newLines, col));
	e.SetFocus();
	c.open = false;
	SyncFromEditor(tab);
}

void ScriptEditor::RenderCompletionPopup(ScriptTab& tab)
{
	auto& c = m_Completion;
	if (!c.open || !tab.editor) return;
	TextEditor& e = *tab.editor;

	const float lineHeight = e.GetLineHeight();
	ImVec2 anchor = e.DocPosToScreenPos(TextEditor::DocPos(c.ctx.line, c.ctx.replaceStart));
	anchor.y += lineHeight + 2.0f;

	const float rowH = ImGui::GetTextLineHeightWithSpacing() + 2.0f;
	const int visibleRows = std::min(12, std::max(1, (int)c.items.size()));
	const float listH = visibleRows * rowH + 8.0f;
	const float width = 520.0f;

	// Flip above the caret when there is no room below
	ImGuiViewport* vp = ImGui::GetWindowViewport();
	float docH = 0.0f;
	if (!c.items.empty() && c.selected < (int)c.items.size() && (!c.items[c.selected].doc.empty() || !c.items[c.selected].detail.empty())) docH = 70.0f;
	if (anchor.y + listH + docH > vp->WorkPos.y + vp->WorkSize.y) anchor.y -= lineHeight + listH + docH + 6.0f;

	ImGui::SetNextWindowPos(anchor);
	ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
	ImGui::SetNextWindowViewport(vp->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.09f, 0.14f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking;
	if (ImGui::Begin("##ScriptCompletion", nullptr, flags))
	{
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
		if (c.items.empty())
		{
			ImGui::TextDisabled("  No suggestions");
		}
		else
		{
			ImGui::BeginChild("##CompletionList", ImVec2(width - 8.0f, listH - 8.0f), ImGuiChildFlags_None);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			ImGui::PushFont(s_CodeFont, std::max(13.0f, m_FontSize - 1.0f));
			for (int i = 0; i < (int)c.items.size(); i++)
			{
				const Completion& it = c.items[i];
				ImGui::PushID(i);
				ImVec2 p = ImGui::GetCursorScreenPos();
				bool selected = i == c.selected;
				if (ImGui::Selectable("##item", selected, ImGuiSelectableFlags_None, ImVec2(0, rowH - 2.0f)))
				{
					c.selected = i;
					AcceptCompletion(tab);
					ImGui::PopID();
					break;
				}
				if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x != 0.0f) c.selected = i;
				if (selected && c.scrollToSelected) { ImGui::SetScrollHereY(0.5f); c.scrollToSelected = false; }
				float sz = rowH - 6.0f;
				ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase);
				Badge(dl, ImVec2(p.x + 2.0f, p.y + 1.0f), sz, KindColor(it.kind), KindLetter(it.kind));
				ImGui::PopFont();
				// label with the matched prefix highlighted
				ImVec2 tp(p.x + sz + 10.0f, p.y + (rowH - 2.0f - ImGui::GetTextLineHeight()) * 0.5f);
				const std::string& prefix = c.ctx.prefix;
				size_t matchLen = 0;
				while (matchLen < prefix.size() && matchLen < it.label.size() && std::tolower((unsigned char)prefix[matchLen]) == std::tolower((unsigned char)it.label[matchLen])) matchLen++;
				std::string matched = it.label.substr(0, matchLen), rest = it.label.substr(matchLen);
				dl->AddText(tp, ImGui::GetColorU32(kAccent), matched.c_str());
				float mw = ImGui::CalcTextSize(matched.c_str()).x;
				dl->AddText(ImVec2(tp.x + mw, tp.y), ImGui::GetColorU32(ImGuiCol_Text), rest.c_str());
				if (!it.detail.empty())
				{
					std::string detail = it.detail.size() > 40 ? it.detail.substr(0, 37) + "..." : it.detail;
					float dw = ImGui::CalcTextSize(detail.c_str()).x;
					float labelEnd = tp.x + ImGui::CalcTextSize(it.label.c_str()).x + 16.0f;
					float x = std::max(labelEnd, p.x + width - 24.0f - dw);
					dl->AddText(ImVec2(x, tp.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), detail.c_str());
				}
				ImGui::PopID();
			}
			ImGui::PopFont();
			ImGui::EndChild();

			// Details of the highlighted entry
			if (c.selected >= 0 && c.selected < (int)c.items.size())
			{
				const Completion& it = c.items[c.selected];
				if (!it.doc.empty() || !it.detail.empty())
				{
					ImGui::Separator();
					ImGui::PushTextWrapPos(width - 12.0f);
					std::string sig = it.label + (it.callable ? it.detail : (it.detail.empty() ? "" : ": " + it.detail));
					if (it.kind == Kind::Snippet) sig = it.label + "  (snippet)";
					ImGui::PushStyleColor(ImGuiCol_Text, kCodeColor);
					ImGui::TextUnformatted(sig.c_str());
					ImGui::PopStyleColor();
					if (!it.doc.empty()) ImGui::TextDisabled("%s", it.doc.c_str());
					ImGui::PopTextWrapPos();
				}
			}
			ImGui::TextDisabled("Enter/Tab: insert   Esc: close   Ctrl+Space: all");
		}
	}
	ImGui::End();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(3);
}

void ScriptEditor::RenderHoverAndSignature(ScriptTab& tab)
{
	if (!tab.editor) return;
	TextEditor& e = *tab.editor;
	const ImGuiWindowFlags tipFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking;
	ImGuiViewport* vp = ImGui::GetWindowViewport();

	// Signature help while typing arguments
	if (m_EditorFocused && !m_Completion.open)
	{
		auto pos = e.GetMainCursorPosition();
		std::string sig, doc;
		int active = 0;
		if (ScriptAnalysis::SignatureHelp(tab.model, (int)pos.line, (int)pos.index, sig, doc, active))
		{
			ImVec2 at = e.DocPosToScreenPos(TextEditor::DocPos(pos.line, pos.index));
			ImGui::SetNextWindowPos(ImVec2(at.x - 20.0f, at.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
			ImGui::SetNextWindowViewport(vp->ID);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.09f, 0.14f, 0.97f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
			if (ImGui::Begin("##ScriptSignature", nullptr, tipFlags))
			{
				ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
				ImGui::PushFont(s_CodeFont, std::max(13.0f, m_FontSize - 1.0f));
				// highlight the active parameter
				size_t open = sig.find('('), close = sig.rfind(')');
				if (open != std::string::npos && close != std::string::npos && close > open)
				{
					ImGui::TextUnformatted(sig.substr(0, open + 1).c_str());
					std::vector<std::string> params;
					std::string cur;
					int depth = 0;
					for (size_t i = open + 1; i < close; i++)
					{
						char ch = sig[i];
						if (ch == '(' || ch == '[' || ch == '{') depth++;
						if (ch == ')' || ch == ']' || ch == '}') depth--;
						if (ch == ',' && depth == 0) { params.push_back(cur); cur.clear(); continue; }
						cur.push_back(ch);
					}
					if (!cur.empty() || !params.empty()) params.push_back(cur);
					for (int i = 0; i < (int)params.size(); i++)
					{
						ImGui::SameLine(0, 0);
						if (i == active) ImGui::TextColored(kAccent, "%s", params[i].c_str());
						else ImGui::TextUnformatted(params[i].c_str());
						if (i + 1 < (int)params.size()) { ImGui::SameLine(0, 0); ImGui::TextUnformatted(","); }
					}
					ImGui::SameLine(0, 0);
					ImGui::TextUnformatted(sig.substr(close).c_str());
				}
				else ImGui::TextUnformatted(sig.c_str());
				ImGui::PopFont();
				if (!doc.empty()) { ImGui::PushTextWrapPos(420.0f); ImGui::TextDisabled("%s", doc.c_str()); ImGui::PopTextWrapPos(); }
			}
			ImGui::End();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}
	}

	// Hover help (after the mouse rests on a word)
	if (!m_EditorHovered || m_Completion.open || ImGui::GetDragDropPayload() || ImGui::IsMouseDown(0))
	{
		m_HoverLine = -1;
		return;
	}
	ImVec2 mouse = ImGui::GetMousePos();
	if (!e.IsMousePosOverGlyph(mouse)) { m_HoverLine = -1; return; }
	auto pos = e.GetDocPosAtMousePos(mouse);
	if ((int)pos.line != m_HoverLine || std::abs((int)pos.index - m_HoverCol) > 0)
	{
		int tokenNow = tab.model.TokenAt((int)pos.line, (int)pos.index);
		int tokenBefore = m_HoverLine >= 0 ? tab.model.TokenAt(m_HoverLine, m_HoverCol) : -2;
		m_HoverLine = (int)pos.line;
		m_HoverCol = (int)pos.index;
		if (tokenNow != tokenBefore)
		{
			m_HoverStart = ImGui::GetTime();
			m_HoverTitle.clear();
			m_HoverBody.clear();
			if (!ScriptAnalysis::Describe(tab.model, m_HoverLine, m_HoverCol, m_HoverTitle, m_HoverBody)) m_HoverTitle.clear();
		}
	}
	if (m_HoverTitle.empty() || ImGui::GetTime() - m_HoverStart < 0.45) return;

	// Problems on this line come first
	std::string problems;
	for (const auto& d : tab.compile.diagnostics) if (d.line - 1 == m_HoverLine) problems += (problems.empty() ? "" : "\n") + d.message;

	ImGui::SetNextWindowPos(ImVec2(mouse.x + 14.0f, mouse.y + 18.0f));
	ImGui::SetNextWindowViewport(vp->ID);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.09f, 0.14f, 0.97f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
	if (ImGui::Begin("##ScriptHover", nullptr, tipFlags))
	{
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
		ImGui::PushTextWrapPos(460.0f);
		if (!problems.empty()) { ImGui::TextColored(kWarningColor, "%s", problems.c_str()); ImGui::Separator(); }
		ImGui::PushFont(s_CodeFont, std::max(13.0f, m_FontSize - 1.0f));
		ImGui::TextColored(kCodeColor, "%s", m_HoverTitle.c_str());
		ImGui::PopFont();
		if (!m_HoverBody.empty()) ImGui::TextUnformatted(m_HoverBody.c_str());
		ImGui::PopTextWrapPos();
	}
	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

// =========================================================================
// Drag & drop into code
// =========================================================================

bool ScriptEditor::PayloadForDrop(SceneManager& scene, const ImGuiPayload* payload, ScriptReference::Payload& out)
{
	if (!payload) return false;
	if (payload->IsDataType("SCENE_OBJECT_INDEX") && payload->DataSize >= (int)sizeof(int))
	{
		int index = *static_cast<const int*>(payload->Data);
		const auto& objects = scene.GetObjects();
		if (index < 0 || index >= (int)objects.size() || !objects[index]) return false;
		GameObject* go = objects[index];
		strncpy_s(out.expression, ScriptReference::ObjectExpression(go).c_str(), _TRUNCATE);
		strncpy_s(out.variable, ScriptReference::MakeVariableName(go->GetName()).c_str(), _TRUNCATE);
		strncpy_s(out.label, go->GetName().c_str(), _TRUNCATE);
		strncpy_s(out.objectName, go->GetName().c_str(), _TRUNCATE);
		return true;
	}
	return ScriptReference::FromImGuiPayload(payload, out);
}

std::string ScriptEditor::UniqueVariable(const ScriptTab& tab, const std::string& base) const
{
	std::string name = base.empty() ? "ref" : base;
	if (!tab.model.IsDeclared(name) && !ScriptApi::Registry::Get().FindGlobal(name)) return name;
	for (int i = 2; i < 1000; i++)
	{
		std::string candidate = name + std::to_string(i);
		if (!tab.model.IsDeclared(candidate)) return candidate;
	}
	return name;
}

void ScriptEditor::HandleDrop(SceneManager& scene, ScriptTab& tab)
{
	const ImGuiPayload* active = ImGui::GetDragDropPayload();
	ScriptReference::Payload ref;
	if (!active || m_EditorRect.z <= 0.0f || !PayloadForDrop(scene, active, ref)) return;

	ImRect rect(ImVec2(m_EditorRect.x, m_EditorRect.y), ImVec2(m_EditorRect.x + m_EditorRect.z, m_EditorRect.y + m_EditorRect.w));
	if (!ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID("##ScriptCodeDrop"))) return;

	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(active->DataType, ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
	{
		DropTarget target = ComputeDrop(tab, ImGui::GetMousePos());
		InsertReference(tab, ref, target.append ? (int)tab.editor->GetLineCount() : target.line, target.column, target.inlineExpression);
		m_FocusRequested = true;
		(void)payload;
	}
	ImGui::EndDragDropTarget();
}

std::string ScriptEditor::ResolveSelfReference(const std::string& expression, const std::string& objectName) const
{
	// The script's own object is simply 'gameObject'
	if (!objectName.empty() && std::find(m_Env.selfObjects.begin(), m_Env.selfObjects.end(), objectName) != m_Env.selfObjects.end())
	{
		std::string prefix = "Scene.Find(\"" + objectName + "\")";
		if (expression.rfind(prefix, 0) == 0) return "gameObject" + expression.substr(prefix.size());
	}
	return expression;
}

ScriptEditor::DropTarget ScriptEditor::ComputeDrop(ScriptTab& tab, ImVec2 mouse) const
{
	DropTarget t;
	TextEditor& e = *tab.editor;
	int lineCount = (int)e.GetLineCount();
	ImVec2 lastLine = e.DocPosToScreenPos(TextEditor::DocPos(lineCount - 1, 0));
	if (mouse.y > lastLine.y + e.GetLineHeight())
	{
		t.append = true;
		t.line = lineCount;
		return t;
	}
	auto pos = e.GetDocPosAtMousePos(mouse);
	t.line = (int)pos.line;
	std::string text = e.GetLineText(pos.line);
	t.blankLine = IsBlank(text);
	int codeStart = (int)LeadingWhitespace(text).size();

	// Over code (not indentation, not past the end): the expression goes there; Ctrl swaps the modes
	bool overCode = !t.blankLine && e.IsMousePosOverGlyph(mouse) && (int)pos.index >= codeStart;
	t.inlineExpression = !t.blankLine && (overCode != ImGui::GetIO().KeyCtrl);
	if (t.inlineExpression)
	{
		// Nearest gap between glyphs, never inside a word
		int col = std::min((int)pos.index, (int)text.size());
		ImVec2 glyph = e.DocPosToScreenPos(TextEditor::DocPos(pos.line, col));
		if (mouse.x > glyph.x + e.GetGlyphWidth() * 0.5f) col++;
		col = std::clamp(col, codeStart, (int)text.size());
		auto isWord = [&](int i) { return i >= 0 && i < (int)text.size() && (std::isalnum((unsigned char)text[i]) || text[i] == '_'); };
		if (isWord(col - 1) && isWord(col))
		{
			int start = col, end = col;
			while (isWord(start - 1)) start--;
			while (isWord(end)) end++;
			col = (col - start < end - col) ? start : end;
		}
		t.column = col;
	}
	return t;
}

void ScriptEditor::InsertReference(ScriptTab& tab, const ScriptReference::Payload& ref, int line, int column, bool inlineExpression)
{
	EnsureEditor(tab);
	TextEditor& e = *tab.editor;
	SyncFromEditor(tab);

	std::string expr = ResolveSelfReference(ref.expression, ref.objectName);

	int lineCount = (int)e.GetLineCount();
	if (inlineExpression)
	{
		line = std::clamp(line, 0, lineCount - 1);
		int len = (int)e.GetLineText(line).size();
		column = std::clamp(column, 0, len);
		e.ReplaceSectionText(TextEditor::DocPos(line, column), TextEditor::DocPos(line, column), expr);
		e.SetCursor(TextEditor::DocPos(line, column + (int)expr.size()));
	}
	else
	{
		std::string statement = "local " + UniqueVariable(tab, ref.variable) + " = " + expr;
		if (line >= lineCount)
		{
			// Below the last line: append
			int last = lineCount - 1;
			std::string lastText = e.GetLineText(last);
			if (IsBlank(lastText))
			{
				e.ReplaceSectionText(TextEditor::DocPos(last, 0), TextEditor::DocPos(last, (int)lastText.size()), statement);
				line = last;
			}
			else
			{
				e.ReplaceSectionText(TextEditor::DocPos(last, (int)lastText.size()), TextEditor::DocPos(last, (int)lastText.size()), "\n" + statement);
				line = last + 1;
			}
		}
		else
		{
			line = std::max(0, line);
			std::string text = e.GetLineText(line);
			if (IsBlank(text))
			{
				// An empty line: use it, indented like the code around it
				std::string indent = text;
				if (indent.empty())
				{
					for (int l = line - 1; l >= 0; l--)
					{
						std::string above = e.GetLineText(l);
						if (IsBlank(above)) continue;
						indent = LeadingWhitespace(above);
						if (OpensBlock(above)) indent += (indent.find(' ') != std::string::npos) ? "    " : "\t";
						break;
					}
				}
				e.ReplaceSectionText(TextEditor::DocPos(line, 0), TextEditor::DocPos(line, (int)text.size()), indent + statement);
			}
			else
			{
				// A line with code: the new statement takes its place, the code moves down
				std::string indent = LeadingWhitespace(text);
				e.ReplaceSectionText(TextEditor::DocPos(line, 0), TextEditor::DocPos(line, 0), indent + statement + "\n");
			}
		}
		e.SetCursor(TextEditor::DocPos(line, (int)e.GetLineText(line).size()));
	}

	m_FlashLine = line;
	m_FlashUntil = ImGui::GetTime() + 1.2;
	e.SetFocus();
	SyncFromEditor(tab);
	tab.compileDue = ImGui::GetTime(); // check the new reference right away
}
