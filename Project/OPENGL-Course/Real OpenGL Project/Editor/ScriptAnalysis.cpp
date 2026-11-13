#include "Editor/ScriptAnalysis.h"
#include "Core/ScriptEngine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>

using ScriptApi::Kind;
using ScriptApi::Member;

namespace ScriptAnalysis
{
	namespace
	{
		bool IsIdentStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
		bool IsIdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

		std::string Lower(std::string s)
		{
			for (char& c : s) c = (char)std::tolower((unsigned char)c);
			return s;
		}

		bool IsOp(const Token& t, const char* op) { return t.type == Token::Type::Op && t.text == op; }
		bool IsKw(const Token& t, const char* kw) { return t.type == Token::Type::Keyword && t.text == kw; }

		std::string FileName(const std::string& path)
		{
			size_t slash = path.find_last_of("/\\");
			return slash == std::string::npos ? path : path.substr(slash + 1);
		}

		std::string Stem(const std::string& path)
		{
			std::string name = FileName(path);
			size_t dot = name.find_last_of('.');
			return dot == std::string::npos ? name : name.substr(0, dot);
		}

		int EditDistance(const std::string& a, const std::string& b)
		{
			std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
			for (size_t j = 0; j <= b.size(); j++) prev[j] = (int)j;
			for (size_t i = 1; i <= a.size(); i++)
			{
				cur[0] = (int)i;
				for (size_t j = 1; j <= b.size(); j++)
				{
					int cost = std::tolower((unsigned char)a[i - 1]) == std::tolower((unsigned char)b[j - 1]) ? 0 : 1;
					cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
				}
				std::swap(prev, cur);
			}
			return prev[b.size()];
		}

		std::string Closest(const std::string& word, const std::vector<std::string>& candidates)
		{
			// A name the word is the start of ("SetPos" -> "SetPosition"): shortest wins
			std::string best;
			if (word.size() < 3) return best; // too short to guess
			std::string lw = Lower(word);
			for (const auto& c : candidates)
			{
				if (word.size() >= 3 && c.size() > word.size() && Lower(c).compare(0, lw.size(), lw) == 0 && (best.empty() || c.size() < best.size()))
					best = c;
			}
			if (!best.empty()) return best;
			int bestDist = std::max(2, (int)word.size() / 3) + 1;
			for (const auto& c : candidates)
			{
				int d = EditDistance(word, c);
				if (d < bestDist) { bestDist = d; best = c; }
			}
			return best;
		}

		// Matching bracket searching forward from an opening one, or -1
		int MatchForward(const std::vector<Token>& toks, int open, int limit)
		{
			int depth = 0;
			for (int k = open; k < limit; k++)
			{
				const Token& t = toks[k];
				if (t.type != Token::Type::Op) continue;
				if (t.text == "(" || t.text == "[" || t.text == "{") depth++;
				else if (t.text == ")" || t.text == "]" || t.text == "}") { if (--depth == 0) return k; }
			}
			return -1;
		}

		int MatchBackward(const std::vector<Token>& toks, int close)
		{
			int depth = 0;
			for (int k = close; k >= 0; k--)
			{
				const Token& t = toks[k];
				if (t.type != Token::Type::Op) continue;
				if (t.text == ")" || t.text == "]" || t.text == "}") depth++;
				else if (t.text == "(" || t.text == "[" || t.text == "{") { if (--depth == 0) return k; }
			}
			return -1;
		}

		// Start of the member chain ending at token 'last' ("a.b(c):d" -> index of 'a')
		int ChainStartOf(const std::vector<Token>& toks, int last)
		{
			int k = last;
			while (k >= 0)
			{
				const Token& t = toks[k];
				if (IsOp(t, ")") || IsOp(t, "]"))
				{
					int open = MatchBackward(toks, k);
					if (open < 0) return k;
					if (open == 0) return 0;
					const Token& p = toks[open - 1];
					if (p.type == Token::Type::Name || IsOp(p, ")") || IsOp(p, "]") || p.type == Token::Type::String) { k = open - 1; continue; }
					return open; // "(expr)" primary
				}
				if (t.type == Token::Type::Name || t.type == Token::Type::String)
				{
					if (k >= 2 && (IsOp(toks[k - 1], ".") || IsOp(toks[k - 1], ":"))) { k -= 2; continue; }
					if (t.type == Token::Type::String && k >= 1 && toks[k - 1].type == Token::Type::Name) { k--; continue; }
					return k;
				}
				return k + 1;
			}
			return 0;
		}

		std::string TokensText(const std::vector<Token>& toks, int first, int last)
		{
			std::string s;
			for (int k = first; k < last; k++)
			{
				if (toks[k].type == Token::Type::String) s += "\"" + toks[k].text + "\"";
				else s += toks[k].text;
				if (toks[k].type != Token::Type::Op && k + 1 < last && toks[k + 1].type != Token::Type::Op) s += " ";
			}
			return s;
		}

		const std::unordered_set<std::string>& BuiltinNames()
		{
			static const std::unordered_set<std::string> names = { "self", "_G", "_ENV", "_VERSION", "arg", "utf8", "coroutine", "debug", "io", "package", "collectgarbage", "dofile", "load", "loadfile", "rawequal", "rawlen", "xpcall", "warn" };
			return names;
		}

		// Component types: registered types deriving from Component (name matching is lenient)
		std::string CanonicalComponentType(const std::string& query)
		{
			std::string q = Lower(query);
			auto strip = [](std::string s) {
				const std::string suffix = "component";
				if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) s.resize(s.size() - suffix.size());
				return s;
			};
			if (q == "script" || q == "luascript" || q == "luascriptcomponent") return "LuaScript";
			for (const auto& t : ScriptApi::Registry::Get().Types())
			{
				if (t.base != "Component") continue;
				if (Lower(t.name) == q || strip(Lower(t.name)) == strip(q)) return t.name;
			}
			return "";
		}

		std::vector<std::string> ComponentTypeNames()
		{
			std::vector<std::string> names;
			for (const auto& t : ScriptApi::Registry::Get().Types())
				if (t.base == "Component") names.push_back(t.name);
			return names;
		}

		bool IsScriptType(const std::string& type) { return type.rfind("script:", 0) == 0; }

		int KindPriority(Kind k)
		{
			switch (k)
			{
			case Kind::Local: return 0;
			case Kind::Property: return 1;
			case Kind::Method: return 2;
			case Kind::ScriptMember: return 2;
			case Kind::Function: return 3;
			case Kind::Global: return 4;
			case Kind::Constant: return 5;
			case Kind::Snippet: return 6;
			case Kind::Keyword: return 7;
			default: return 8;
			}
		}

		int MatchScore(const std::string& candidate, const std::string& prefix)
		{
			if (prefix.empty()) return 1;
			if (candidate.compare(0, prefix.size(), prefix) == 0) return 1000 - (int)(candidate.size() - prefix.size());
			std::string lc = Lower(candidate), lp = Lower(prefix);
			if (lc.compare(0, lp.size(), lp) == 0) return 900 - (int)(candidate.size() - prefix.size());
			size_t pos = lc.find(lp);
			if (pos != std::string::npos) return 600 - (int)pos;
			// subsequence ("gtp" -> "GetPosition")
			size_t j = 0;
			int gaps = 0;
			for (size_t i = 0; i < lc.size() && j < lp.size(); i++)
			{
				if (lc[i] == lp[j]) j++;
				else if (j > 0) gaps++;
			}
			if (j == lp.size() && std::tolower((unsigned char)candidate[0]) == lp[0]) return 300 - gaps;
			return 0;
		}

		// Statement keywords that end an expression when met at bracket depth 0
		bool EndsExpression(const Token& t)
		{
			if (t.type != Token::Type::Keyword) return false;
			static const std::unordered_set<std::string> stop = { "local", "return", "if", "for", "while", "repeat", "until", "end", "else", "elseif", "do", "then", "break", "goto" };
			return stop.count(t.text) > 0;
		}

		bool IsBinaryOp(const Token& t)
		{
			if (t.type == Token::Type::Keyword) return t.text == "and" || t.text == "or";
			if (t.type != Token::Type::Op) return false;
			static const std::unordered_set<std::string> ops = { "+", "-", "*", "/", "//", "%", "^", "..", "==", "~=", "<", ">", "<=", ">=", "&", "|", "~", "<<", ">>", "=", ",", "(", "[", "{" };
			return ops.count(t.text) > 0;
		}

		// End (exclusive) of one expression starting at 'start': stops at a top-level ',' or at
		// the end of the statement (new line after a complete operand, or a statement keyword)
		int ExpressionEnd(const std::vector<Token>& toks, int start)
		{
			int depth = 0;
			int k = start;
			if (k < (int)toks.size() && IsKw(toks[k], "function")) return k + 1; // anonymous function
			for (; k < (int)toks.size(); k++)
			{
				const Token& t = toks[k];
				if (t.type == Token::Type::Op)
				{
					if (t.text == "(" || t.text == "[" || t.text == "{") { depth++; continue; }
					if (t.text == ")" || t.text == "]" || t.text == "}") { if (depth == 0) return k; depth--; continue; }
					if (depth == 0 && (t.text == "," || t.text == "=" || t.text == ";")) return k;
				}
				if (depth > 0) continue;
				if (k > start)
				{
					if (EndsExpression(t)) return k;
					if (IsKw(t, "function")) return k;
					const Token& prev = toks[k - 1];
					if (t.line != prev.line && !IsBinaryOp(prev) && !IsBinaryOp(t) && !IsOp(t, ".") && !IsOp(t, ":")) return k;
					// two operands in a row on the same line ("x y") also end it
					bool prevOperand = prev.type != Token::Type::Op || IsOp(prev, ")") || IsOp(prev, "]") || IsOp(prev, "}");
					bool curOperand = (t.type == Token::Type::Name || t.type == Token::Type::Number);
					if (prevOperand && curOperand && prev.type != Token::Type::Keyword) return k;
				}
			}
			return k;
		}

		void Push(std::vector<Completion>& out, const std::string& label, const std::string& detail, const std::string& doc, Kind kind, int score, bool callable = false, bool hasParams = false)
		{
			Completion c;
			c.label = label;
			c.insertText = label;
			c.detail = detail;
			c.doc = doc;
			c.kind = kind;
			c.score = score;
			c.callable = callable;
			c.hasParams = hasParams;
			out.push_back(std::move(c));
		}
	}

	// =========================================================================================
	// Lexer
	// =========================================================================================

	bool IsKeyword(const std::string& word)
	{
		static const std::unordered_set<std::string> keywords = {
			"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
			"local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while" };
		return keywords.count(word) > 0;
	}

	void Tokenize(const std::string& code, std::vector<Token>& tokens, std::vector<Span>* spans)
	{
		tokens.clear();
		if (spans) spans->clear();
		const size_t n = code.size();
		size_t i = 0;
		int line = 0, col = 0;

		auto advance = [&](size_t count) {
			for (size_t k = 0; k < count && i < n; k++)
			{
				if (code[i] == '\n') { line++; col = 0; }
				else col++;
				i++;
			}
		};
		auto longLevel = [&](size_t at) -> int {
			if (at >= n || code[at] != '[') return -1;
			size_t j = at + 1;
			int level = 0;
			while (j < n && code[j] == '=') { level++; j++; }
			return (j < n && code[j] == '[') ? level : -1;
		};
		auto skipLong = [&](int level) -> std::string {
			advance(level + 2);
			std::string close = "]" + std::string(level, '=') + "]";
			size_t end = code.find(close, i);
			size_t contentEnd = end == std::string::npos ? n : end;
			std::string content = code.substr(i, contentEnd - i);
			advance((end == std::string::npos ? n : end + close.size()) - i);
			return content;
		};
		auto push = [&](Token::Type type, std::string text, int l, int c) {
			Token t;
			t.type = type;
			t.text = std::move(text);
			t.line = l; t.col = c;
			t.endLine = line; t.endCol = col;
			tokens.push_back(std::move(t));
		};

		while (i < n)
		{
			char c = code[i];
			if (c == '\n' || c == ' ' || c == '\t' || c == '\r') { advance(1); continue; }
			int sl = line, sc = col;

			if (c == '-' && i + 1 < n && code[i + 1] == '-')
			{
				advance(2);
				int level = longLevel(i);
				if (level >= 0) skipLong(level);
				else while (i < n && code[i] != '\n') advance(1);
				if (spans) spans->push_back({ sl, sc, line, col, true });
				continue;
			}
			if (IsIdentStart(c))
			{
				size_t s = i;
				while (i < n && IsIdentChar(code[i])) advance(1);
				std::string word = code.substr(s, i - s);
				push(IsKeyword(word) ? Token::Type::Keyword : Token::Type::Name, word, sl, sc);
				continue;
			}
			if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)code[i + 1])))
			{
				size_t s = i;
				bool hex = c == '0' && i + 1 < n && (code[i + 1] == 'x' || code[i + 1] == 'X');
				if (hex) advance(2);
				while (i < n)
				{
					char d = code[i];
					char exp = hex ? 'p' : 'e';
					if (std::isalnum((unsigned char)d) || d == '.') { advance(1); continue; }
					if ((d == '+' || d == '-') && i > s && std::tolower((unsigned char)code[i - 1]) == exp) { advance(1); continue; }
					break;
				}
				push(Token::Type::Number, code.substr(s, i - s), sl, sc);
				continue;
			}
			if (c == '"' || c == '\'')
			{
				char quote = c;
				advance(1);
				std::string content;
				while (i < n && code[i] != quote && code[i] != '\n')
				{
					if (code[i] == '\\' && i + 1 < n) { content.push_back(code[i + 1]); advance(2); }
					else { content.push_back(code[i]); advance(1); }
				}
				if (i < n && code[i] == quote) advance(1);
				push(Token::Type::String, content, sl, sc);
				if (spans) spans->push_back({ sl, sc, line, col, false });
				continue;
			}
			if (c == '[')
			{
				int level = longLevel(i);
				if (level >= 0)
				{
					std::string content = skipLong(level);
					push(Token::Type::String, content, sl, sc);
					if (spans) spans->push_back({ sl, sc, line, col, false });
					continue;
				}
			}

			static const char* ops3[] = { "..." };
			static const char* ops2[] = { "..", "==", "~=", "<=", ">=", "//", "::", "<<", ">>" };
			std::string op(1, c);
			for (const char* o : ops3) if (code.compare(i, 3, o) == 0) { op = o; break; }
			if (op.size() == 1)
				for (const char* o : ops2) if (code.compare(i, 2, o) == 0) { op = o; break; }
			advance(op.size());
			push(Token::Type::Op, op, sl, sc);
		}
	}

	// =========================================================================================
	// Environment
	// =========================================================================================

	bool Environment::HasObject(const std::string& name) const
	{
		return std::find(objectNames.begin(), objectNames.end(), name) != objectNames.end();
	}

	bool Environment::HasLight(const std::string& name) const
	{
		return std::find(lightNames.begin(), lightNames.end(), name) != lightNames.end();
	}

	std::string Environment::FindScript(const std::string& nameOrPath) const
	{
		if (nameOrPath.empty()) return "";
		std::string q = Lower(nameOrPath);
		for (char& c : q) if (c == '\\') c = '/';
		for (const auto& f : scriptFiles)
		{
			std::string lf = Lower(f);
			if (lf == q || Lower(Stem(f)) == q || Lower(FileName(f)) == q) return f;
			if (lf.size() > q.size() && lf.compare(lf.size() - q.size(), q.size(), q) == 0 && lf[lf.size() - q.size() - 1] == '/') return f;
		}
		return "";
	}

	// =========================================================================================
	// Script members (other scripts seen through GetScript / Script.Require)
	// =========================================================================================

	const std::vector<Member>* ScriptMembers(const std::string& scriptPath)
	{
		struct Entry
		{
			std::filesystem::file_time_type time;
			std::vector<Member> members;
		};
		static std::map<std::string, Entry> cache;

		std::error_code ec;
		auto time = std::filesystem::last_write_time(scriptPath, ec);
		if (ec) return nullptr;
		auto it = cache.find(scriptPath);
		if (it != cache.end() && it->second.time == time) return &it->second.members;

		std::ifstream file(scriptPath);
		if (!file.is_open()) return nullptr;
		std::stringstream ss;
		ss << file.rdbuf();

		DocumentModel model;
		model.Build(ss.str(), nullptr);
		Entry& e = cache[scriptPath];
		e.time = time;
		e.members = model.ExportedMembers();
		for (auto& m : e.members) m.doc = m.doc.empty() ? "Defined in " + FileName(scriptPath) : m.doc + " (" + FileName(scriptPath) + ")";
		return &e.members;
	}

	std::string ScriptTypeFor(const std::string& nameOrPath, const Environment* env)
	{
		std::string path = env ? env->FindScript(nameOrPath) : "";
		if (path.empty()) path = ScriptEngine::ResolveScriptPath(nameOrPath);
		return path.empty() ? "" : "script:" + path;
	}

	std::string DisplayType(const std::string& type)
	{
		if (IsScriptType(type)) return "script " + FileName(type.substr(7));
		return type;
	}

	void CollectMembers(const std::string& type, const Environment* env, std::vector<Member>& out)
	{
		(void)env;
		if (IsScriptType(type))
		{
			if (const auto* members = ScriptMembers(type.substr(7))) out.insert(out.end(), members->begin(), members->end());
			return;
		}
		std::vector<const Member*> members;
		ScriptApi::Registry::Get().CollectMembers(type, members);
		for (const Member* m : members) out.push_back(*m);
	}

	// =========================================================================================
	// Document model
	// =========================================================================================

	void DocumentModel::Build(const std::string& text, const Environment* env)
	{
		m_Env = env;
		m_Lines.clear();
		{
			std::string current;
			for (char c : text)
			{
				if (c == '\n') { m_Lines.push_back(current); current.clear(); }
				else if (c != '\r') current.push_back(c);
			}
			m_Lines.push_back(current);
		}
		Tokenize(text, m_Tokens, &m_Spans);
		m_Assignments.clear();
		m_Symbols.clear();
		m_Declared.clear();

		const auto& toks = m_Tokens;
		const int n = (int)toks.size();
		std::vector<char> brackets; // innermost bracket kind

		auto declare = [&](const std::string& name, Kind kind, int line, bool isLocal, const std::string& params = "") {
			m_Declared.insert(name);
			for (const auto& s : m_Symbols) if (s.name == name && s.isLocal == isLocal && s.kind == kind) return;
			m_Symbols.push_back({ name, kind, params, line, isLocal });
		};
		auto paramsText = [&](int open, int close) {
			std::string s = "(";
			for (int k = open + 1; k < close; k++) { s += toks[k].text; if (IsOp(toks[k], ",")) s += " "; }
			return s + ")";
		};
		auto declareParams = [&](int open, const std::string& functionName) -> int {
			int close = MatchForward(toks, open, n);
			if (close < 0) return open;
			int index = 0;
			for (int k = open + 1; k < close; k++)
			{
				if (toks[k].type != Token::Type::Name) continue;
				declare(toks[k].text, Kind::Local, toks[k].line, true);
				Assignment a;
				a.name = toks[k].text;
				a.line = toks[k].line;
				a.isLocal = true;
				if (functionName == "Update" && index == 0) a.fixedType = "number"; // Update(dt)
				m_Assignments.push_back(a);
				index++;
			}
			return close;
		};

		for (int i = 0; i < n; i++)
		{
			const Token& t = toks[i];
			if (t.type == Token::Type::Op)
			{
				if (t.text == "(" || t.text == "[" || t.text == "{") brackets.push_back(t.text[0]);
				else if ((t.text == ")" || t.text == "]" || t.text == "}") && !brackets.empty()) brackets.pop_back();
				continue;
			}

			if (IsKw(t, "local"))
			{
				if (i + 2 < n && IsKw(toks[i + 1], "function") && toks[i + 2].type == Token::Type::Name)
				{
					std::string name = toks[i + 2].text;
					std::string params;
					if (i + 3 < n && IsOp(toks[i + 3], "("))
					{
						int close = MatchForward(toks, i + 3, n);
						if (close > 0) params = paramsText(i + 3, close);
					}
					declare(name, Kind::Function, t.line, true, params);
					Assignment a; a.name = name; a.line = t.line; a.isLocal = true; a.fixedType = "function";
					m_Assignments.push_back(a);
					if (i + 3 < n && IsOp(toks[i + 3], "(")) declareParams(i + 3, name);
					i += 2;
					continue;
				}
				std::vector<int> names;
				int j = i + 1;
				while (j < n && toks[j].type == Token::Type::Name)
				{
					names.push_back(j);
					j++;
					if (j < n && IsOp(toks[j], "<")) { while (j < n && !IsOp(toks[j], ">")) j++; j++; } // <const>
					if (j < n && IsOp(toks[j], ",")) { j++; continue; }
					break;
				}
				std::vector<std::pair<int, int>> exprs;
				if (j < n && IsOp(toks[j], "="))
				{
					int k = j + 1;
					while (k < n)
					{
						int end = ExpressionEnd(toks, k);
						exprs.push_back({ k, end });
						if (end < n && IsOp(toks[end], ",")) { k = end + 1; continue; }
						break;
					}
				}
				for (size_t m = 0; m < names.size(); m++)
				{
					const Token& nameTok = toks[names[m]];
					declare(nameTok.text, Kind::Local, nameTok.line, true);
					Assignment a;
					a.name = nameTok.text;
					a.line = nameTok.line;
					a.isLocal = true;
					if (m < exprs.size()) { a.exprFirst = exprs[m].first; a.exprLast = exprs[m].second; }
					m_Assignments.push_back(a);
				}
				if (!names.empty()) i = names.back();
				continue;
			}

			if (IsKw(t, "function"))
			{
				int j = i + 1;
				std::string first;
				bool simple = true, method = false;
				if (j < n && toks[j].type == Token::Type::Name)
				{
					first = toks[j].text;
					j++;
					while (j + 1 < n && (IsOp(toks[j], ".") || IsOp(toks[j], ":")) && toks[j + 1].type == Token::Type::Name)
					{
						if (IsOp(toks[j], ":")) method = true;
						simple = false;
						j += 2;
					}
				}
				std::string params;
				if (j < n && IsOp(toks[j], "("))
				{
					int close = MatchForward(toks, j, n);
					if (close > 0) params = paramsText(j, close);
				}
				if (!first.empty() && simple)
				{
					declare(first, Kind::Function, t.line, false, params);
					Assignment a; a.name = first; a.line = t.line; a.isLocal = false; a.fixedType = "function";
					m_Assignments.push_back(a);
				}
				else if (!first.empty())
				{
					m_Declared.insert(first);
				}
				if (method) m_Declared.insert("self");
				if (j < n && IsOp(toks[j], "(")) declareParams(j, simple ? first : "");
				continue;
			}

			if (IsKw(t, "for"))
			{
				std::vector<int> names;
				int j = i + 1;
				while (j < n && toks[j].type == Token::Type::Name)
				{
					names.push_back(j);
					j++;
					if (j < n && IsOp(toks[j], ",")) { j++; continue; }
					break;
				}
				if (j < n && IsOp(toks[j], "="))
				{
					for (int idx : names)
					{
						declare(toks[idx].text, Kind::Local, toks[idx].line, true);
						Assignment a; a.name = toks[idx].text; a.line = toks[idx].line; a.isLocal = true; a.fixedType = "number";
						m_Assignments.push_back(a);
					}
				}
				else if (j < n && IsKw(toks[j], "in"))
				{
					bool isIpairs = j + 2 < n && toks[j + 1].type == Token::Type::Name && toks[j + 1].text == "ipairs" && IsOp(toks[j + 2], "(");
					int argOpen = isIpairs ? j + 2 : -1;
					int argClose = isIpairs ? MatchForward(toks, argOpen, n) : -1;
					for (size_t m = 0; m < names.size(); m++)
					{
						const Token& nt = toks[names[m]];
						declare(nt.text, Kind::Local, nt.line, true);
						Assignment a; a.name = nt.text; a.line = nt.line; a.isLocal = true;
						if (isIpairs && m == 0) a.fixedType = "number";
						if (isIpairs && m == 1 && argClose > argOpen) { a.iterOf = 1; a.exprFirst = argOpen + 1; a.exprLast = argClose; }
						m_Assignments.push_back(a);
					}
				}
				if (!names.empty()) i = names.back();
				continue;
			}

			// Global assignment: name = value (statement level, not a table key / field)
			if (t.type == Token::Type::Name && i + 1 < n && IsOp(toks[i + 1], "=") &&
				!(i > 0 && (IsOp(toks[i - 1], ".") || IsOp(toks[i - 1], ":"))) &&
				(brackets.empty() || brackets.back() != '{'))
			{
				Assignment a;
				a.name = t.text;
				a.line = t.line;
				a.isLocal = false;
				a.exprFirst = i + 2;
				a.exprLast = ExpressionEnd(toks, i + 2);
				m_Assignments.push_back(a);
				bool knownLocal = false;
				for (const auto& s : m_Symbols) if (s.name == t.text && s.isLocal) { knownLocal = true; break; }
				if (!knownLocal) declare(t.text, Kind::Global, t.line, false);
				else m_Declared.insert(t.text);
			}
		}
	}

	bool DocumentModel::IsInsideTextOrComment(int line, int col) const
	{
		for (const auto& s : m_Spans)
		{
			bool afterStart = line > s.line || (line == s.line && col > s.col);
			bool beforeEnd = s.comment ? (line < s.endLine || (line == s.endLine && col <= s.endCol))
			                           : (line < s.endLine || (line == s.endLine && col < s.endCol));
			if (afterStart && beforeEnd) return true;
		}
		return false;
	}

	int DocumentModel::TokenAt(int line, int col) const
	{
		for (int i = 0; i < (int)m_Tokens.size(); i++)
		{
			const Token& t = m_Tokens[i];
			if (t.line == line && col >= t.col && col <= t.endCol && t.endLine == line) return i;
			if (t.line > line) break;
		}
		return -1;
	}

	int DocumentModel::ChainStart(int last) const { return ChainStartOf(m_Tokens, last); }

	std::string DocumentModel::ResolveName(const std::string& name, int line, int depth) const
	{
		if (depth > 12) return "";
		const Assignment* best = nullptr;
		for (const auto& a : m_Assignments)
		{
			if (a.name != name || a.line > line) continue;
			if (!best || a.line >= best->line) best = &a;
		}
		if (!best)
		{
			for (const auto& a : m_Assignments)
				if (a.name == name && (!best || a.line < best->line)) best = &a;
		}
		if (!best) return "";
		if (!best->fixedType.empty()) return best->fixedType;
		if (best->exprFirst < 0 || best->exprLast <= best->exprFirst) return "";
		// The value is evaluated before the name exists: look one line up for its own name
		std::string type = Resolve(m_Tokens, best->exprFirst, best->exprLast, best->line - 1, depth + 1);
		if (best->iterOf == 1) return ScriptApi::ElementType(type);
		return type;
	}

	std::string DocumentModel::Resolve(const std::vector<Token>& toks, int first, int last, int line, int depth) const
	{
		if (first >= last || depth > 12) return "";
		auto& reg = ScriptApi::Registry::Get();

		// Unary operators
		const Token& t0 = toks[first];
		if (IsKw(t0, "not")) return "boolean";
		if (IsOp(t0, "#")) return "number";
		if (IsOp(t0, "-")) return Resolve(toks, first + 1, last, line, depth + 1);
		if (IsKw(t0, "function")) return "function";

		// Binary operators at bracket depth 0, lowest precedence first
		auto findTop = [&](auto pred) -> int {
			int d = 0;
			for (int k = first; k < last; k++)
			{
				const Token& t = toks[k];
				if (t.type == Token::Type::Op)
				{
					if (t.text == "(" || t.text == "[" || t.text == "{") { d++; continue; }
					if (t.text == ")" || t.text == "]" || t.text == "}") { d--; continue; }
				}
				if (d == 0 && k > first && pred(t)) return k;
			}
			return -1;
		};
		int k = findTop([](const Token& t) { return IsKw(t, "or"); });
		if (k > 0)
		{
			std::string right = Resolve(toks, k + 1, last, line, depth + 1);
			return right.empty() ? Resolve(toks, first, k, line, depth + 1) : right;
		}
		k = findTop([](const Token& t) { return IsKw(t, "and"); });
		if (k > 0) return Resolve(toks, k + 1, last, line, depth + 1);
		if (findTop([](const Token& t) { return t.type == Token::Type::Op && (t.text == "==" || t.text == "~=" || t.text == "<" || t.text == ">" || t.text == "<=" || t.text == ">="); }) > 0)
			return "boolean";
		if (findTop([](const Token& t) { return IsOp(t, ".."); }) > 0) return "string";
		k = findTop([](const Token& t) { return t.type == Token::Type::Op && (t.text == "+" || t.text == "-" || t.text == "*" || t.text == "/" || t.text == "%" || t.text == "^" || t.text == "//"); });
		if (k > 0)
		{
			std::string left = Resolve(toks, first, k, line, depth + 1);
			if (left.rfind("vec", 0) == 0) return left;
			std::string right = Resolve(toks, k + 1, last, line, depth + 1);
			if (right.rfind("vec", 0) == 0) return right;
			return "number";
		}

		// Primary expression
		std::string type;
		const Member* callable = nullptr;
		std::string callableName;
		bool unknownCallable = false; // a function whose return type we cannot know
		int i = first;
		const Token& p = toks[i];
		if (p.type == Token::Type::Name)
		{
			std::string local = ResolveName(p.text, line, depth + 1);
			if (local == "function") unknownCallable = true, type = "function";
			else if (!local.empty()) type = local;
			else if (const Member* g = reg.FindGlobal(p.text))
			{
				// constructors (vec3) are callable and also a type table (vec3.new)
				if (g->IsCallable()) { callable = g; callableName = g->name; type = reg.FindType(g->name) ? g->name : "function"; }
				else type = g->type;
			}
			else return "";
			i++;
		}
		else if (p.type == Token::Type::String) { type = "string"; i++; }
		else if (p.type == Token::Type::Number) { type = "number"; i++; }
		else if (IsKw(p, "true") || IsKw(p, "false")) { type = "boolean"; i++; }
		else if (IsKw(p, "nil")) { return "nil"; }
		else if (IsOp(p, "{")) { int close = MatchForward(toks, i, last); if (close < 0) return "table"; type = "table"; i = close + 1; }
		else if (IsOp(p, "("))
		{
			int close = MatchForward(toks, i, last);
			if (close < 0) return "";
			type = Resolve(toks, i + 1, close, line, depth + 1);
			i = close + 1;
		}
		else return "";

		// Suffixes: .name  :name  (args)  "str"  {table}  [index]
		while (i < last)
		{
			const Token& s = toks[i];
			if ((IsOp(s, ".") || IsOp(s, ":")) && i + 1 < last && toks[i + 1].type == Token::Type::Name)
			{
				const std::string& name = toks[i + 1].text;
				callable = nullptr;
				unknownCallable = false;
				callableName = name;
				if (IsScriptType(type))
				{
					const auto* members = ScriptMembers(type.substr(7));
					const Member* found = nullptr;
					if (members) for (const auto& m : *members) if (m.name == name) { found = &m; break; }
					if (!found) return "";
					if (found->IsCallable()) { unknownCallable = true; type = "function"; }
					else type = found->type;
				}
				else
				{
					const Member* m = reg.FindMember(type, name);
					if (!m) return "";
					if (m->IsCallable()) { callable = m; type = "function"; }
					else type = m->type;
				}
				i += 2;
				continue;
			}
			if (IsOp(s, "(") || s.type == Token::Type::String || IsOp(s, "{"))
			{
				std::string firstString;
				int next = i + 1;
				if (IsOp(s, "("))
				{
					int close = MatchForward(toks, i, last);
					if (close < 0) close = last; // unfinished call (still typing)
					if (i + 1 < close && toks[i + 1].type == Token::Type::String) firstString = toks[i + 1].text;
					next = close + 1;
				}
				else if (s.type == Token::Type::String) firstString = s.text;
				else { int close = MatchForward(toks, i, last); next = close < 0 ? last : close + 1; }

				if (callable)
				{
					std::string rt = callable->type;
					if (callableName == "GetComponent")
					{
						std::string c = CanonicalComponentType(firstString);
						rt = c.empty() ? "Component" : c;
					}
					else if (rt == "script")
						rt = firstString.empty() ? "" : ScriptTypeFor(firstString, m_Env);
					type = rt;
					callable = nullptr;
				}
				else if (unknownCallable) { type = ""; unknownCallable = false; }
				else return "";
				i = next;
				if (type.empty() && i < last) return "";
				continue;
			}
			if (IsOp(s, "["))
			{
				int close = MatchForward(toks, i, last);
				type = ScriptApi::ElementType(type);
				if (type.empty() || close < 0) return "";
				i = close + 1;
				continue;
			}
			return "";
		}
		return (callable || unknownCallable) ? "function" : type;
	}

	std::string DocumentModel::InferTokens(int first, int last, int line) const
	{
		return Resolve(m_Tokens, first, last, line, 0);
	}

	std::string DocumentModel::InferExpression(const std::string& expr, int line) const
	{
		std::vector<Token> toks;
		Tokenize(expr, toks);
		return Resolve(toks, 0, (int)toks.size(), line, 0);
	}

	std::string DocumentModel::InferVariable(const std::string& name, int line) const
	{
		return ResolveName(name, line, 0);
	}

	std::vector<Member> DocumentModel::ExportedMembers() const
	{
		std::vector<Member> out;
		int lastLine = (int)m_Lines.size();
		for (const auto& s : m_Symbols)
		{
			if (s.isLocal) continue;
			if (std::any_of(out.begin(), out.end(), [&](const Member& m) { return m.name == s.name; })) continue;
			Member m;
			m.name = s.name;
			if (s.kind == Kind::Function)
			{
				m.kind = Kind::Function;
				m.params = s.params.empty() ? "()" : s.params;
				m.doc = "Function, line " + std::to_string(s.line + 1);
			}
			else
			{
				m.kind = Kind::Property;
				m.type = InferVariable(s.name, lastLine);
				m.doc = "Variable, line " + std::to_string(s.line + 1);
			}
			out.push_back(std::move(m));
		}
		return out;
	}

	// =========================================================================================
	// Completion
	// =========================================================================================

	namespace
	{
		// Values offered inside a string argument: Scene.Find("|") -> object names...
		void StringArgumentValues(const std::string& owner, const std::string& fn, const Environment* env, std::vector<std::string>& values, Kind& kind)
		{
			kind = Kind::Constant;
			if (!env) return;
			if ((owner == "Scene" && fn == "Find") || (owner == "Resource" && (fn == "GameObject" || fn == "FindObject" || fn == "FromScene")) || fn == "FindChild")
				values = env->objectNames;
			else if (owner == "Scene" && fn == "FindLight")
				values = env->lightNames;
			else if (fn == "GetComponent" || fn == "HasComponent")
				values = ComponentTypeNames();
			else if (fn == "GetScript" || (owner == "Script" && (fn == "Require" || fn == "Reload")))
			{
				for (const auto& f : env->scriptFiles) values.push_back(Stem(f));
				kind = Kind::ScriptMember;
			}
			else if ((owner == "Assets" && fn == "Texture") || (owner == "Resource" && fn == "Texture") ||
			         fn == "SetTexture" || fn == "SetNormalMap" || fn == "SetDisplacementMap" || fn == "AddLayer")
				values = env->textureFiles;
			else if (owner == "Assets" && fn == "Material") values = env->materialFiles;
			else if ((owner == "Assets" || owner == "Resource") && fn == "Model") values = env->modelFiles;
			else if ((owner == "Assets" || owner == "Resource") && fn == "Audio") values = env->audioFiles;
			else if ((owner == "Assets" && fn == "Exists") || (owner == "Resource" && (fn == "Get" || fn == "FromPath")))
			{
				for (const auto* list : { &env->textureFiles, &env->materialFiles, &env->modelFiles, &env->audioFiles, &env->scriptFiles })
					values.insert(values.end(), list->begin(), list->end());
			}
		}

		void FinishList(std::vector<Completion>& out)
		{
			out.erase(std::remove_if(out.begin(), out.end(), [](const Completion& c) { return c.score <= 0; }), out.end());
			std::stable_sort(out.begin(), out.end(), [](const Completion& a, const Completion& b) {
				if (a.score != b.score) return a.score > b.score;
				int pa = KindPriority(a.kind), pb = KindPriority(b.kind);
				if (pa != pb) return pa < pb;
				return Lower(a.label) < Lower(b.label);
			});
			// drop duplicates (same label)
			std::unordered_set<std::string> seen;
			out.erase(std::remove_if(out.begin(), out.end(), [&](const Completion& c) { return !seen.insert(c.label).second; }), out.end());
			if (out.size() > 200) out.resize(200);
		}
	}

	bool Complete(const DocumentModel& doc, int line, int col, bool manual, CompletionContext& ctx, std::vector<Completion>& out)
	{
		out.clear();
		ctx = CompletionContext();
		ctx.line = line;
		if (line < 0 || line >= (int)doc.Lines().size()) return false;
		const std::string& text = doc.Lines()[line];
		col = std::min(col, (int)text.size());
		const std::string before = text.substr(0, col);
		auto& reg = ScriptApi::Registry::Get();

		// Multi-line comments / long strings
		if (doc.IsInsideTextOrComment(line, col))
		{
			// a one-line quoted string is handled below (argument completion); anything else: no
			bool quoted = false;
			for (const auto& s : doc.Spans())
				if (!s.comment && s.line == line && s.col < col && (s.endLine > line || s.endCol >= col)) quoted = true;
			if (!quoted) return false;
		}

		// Scan the line up to the cursor: quotes and '--'
		int quoteCol = -1;
		char quote = 0;
		for (int i = 0; i < col; i++)
		{
			char c = before[i];
			if (quote)
			{
				if (c == '\\') { i++; continue; }
				if (c == quote) { quote = 0; quoteCol = -1; }
				continue;
			}
			if (c == '-' && i + 1 < col && before[i + 1] == '-') return false; // comment
			if (c == '"' || c == '\'') { quote = c; quoteCol = i; }
		}

		const Environment* env = doc.Env();

		if (quote)
		{
			// Completing a string argument: f("...|  or  obj:f("...|
			std::string head = before.substr(0, quoteCol);
			while (!head.empty() && (head.back() == ' ' || head.back() == '\t')) head.pop_back();
			if (head.empty() || head.back() != '(') return false;
			head.pop_back();
			std::vector<Token> toks;
			Tokenize(head, toks);
			if (toks.empty() || toks.back().type != Token::Type::Name) return false;
			std::string fn = toks.back().text;
			std::string owner;
			int last = (int)toks.size() - 1;
			if (last >= 2 && (IsOp(toks[last - 1], ".") || IsOp(toks[last - 1], ":")))
			{
				int start = ChainStartOf(toks, last - 2);
				owner = doc.InferExpression(TokensText(toks, start, last - 1), line);
			}
			std::vector<std::string> values;
			Kind kind;
			StringArgumentValues(owner, fn, env, values, kind);
			if (values.empty()) return false;

			ctx.inString = true;
			ctx.replaceStart = quoteCol + 1;
			ctx.replaceEnd = col;
			ctx.prefix = before.substr(quoteCol + 1);
			for (const auto& v : values)
			{
				int score = std::max(MatchScore(v, ctx.prefix), MatchScore(FileName(v), ctx.prefix));
				std::string detail = kind == Kind::ScriptMember ? "script" : (fn == "GetComponent" || fn == "HasComponent") ? "component" : fn == "FindLight" ? "light" :
					(owner == "Scene" || fn == "FindChild" || fn == "GameObject" || fn == "FindObject" || fn == "FromScene") ? "scene object" : "asset";
				Push(out, v, detail, "", kind, score);
			}
			FinishList(out);
			return !out.empty();
		}

		// Identifier being typed
		int s = col;
		while (s > 0 && IsIdentChar(before[s - 1])) s--;
		ctx.prefix = before.substr(s);
		ctx.replaceStart = s;
		ctx.replaceEnd = col;
		if (!ctx.prefix.empty() && std::isdigit((unsigned char)ctx.prefix[0])) return false;

		bool member = s > 0 && (before[s - 1] == '.' || before[s - 1] == ':') && !(s > 1 && before[s - 2] == '.');
		if (member)
		{
			ctx.member = true;
			ctx.separator = before[s - 1];
			ctx.separatorCol = s - 1;
			std::vector<Token> toks;
			Tokenize(before.substr(0, s - 1), toks);
			if (toks.empty()) return false;
			int start = ChainStartOf(toks, (int)toks.size() - 1);
			ctx.ownerType = doc.InferExpression(TokensText(toks, start, (int)toks.size()), line);
			if (ctx.ownerType.empty()) return false;

			std::vector<Member> members;
			CollectMembers(ctx.ownerType, env, members);
			for (const auto& m : members)
			{
				if (ctx.separator == ':' && m.kind != Kind::Method && !(IsScriptType(ctx.ownerType) && m.kind == Kind::Function)) continue;
				Completion c;
				c.label = m.name;
				c.insertText = m.name;
				c.detail = m.IsCallable() ? (m.params.empty() ? "()" : m.params) + (m.type.empty() ? "" : " -> " + DisplayType(m.type)) : DisplayType(m.type);
				c.doc = m.doc;
				c.kind = IsScriptType(ctx.ownerType) ? Kind::ScriptMember : m.kind;
				c.callable = m.IsCallable();
				c.hasParams = m.IsCallable() && m.params != "()" && !m.params.empty();
				c.fixSeparator = ctx.separator == '.' && m.kind == Kind::Method;
				c.score = MatchScore(m.name, ctx.prefix);
				out.push_back(std::move(c));
			}
			FinishList(out);
			return !out.empty();
		}

		if (ctx.prefix.empty() && !manual) return false;

		// Declaring a new name: "local |", "function |", "for |"
		{
			std::vector<Token> toks;
			Tokenize(before.substr(0, s), toks);
			if (!toks.empty() && !manual)
			{
				const Token& prev = toks.back();
				bool forVariableList = IsOp(prev, ",") && toks.size() >= 2 && IsKw(toks.front(), "for");
				if (IsKw(prev, "local") || IsKw(prev, "function") || IsKw(prev, "for") || forVariableList)
					return false;
			}
		}

		// Document symbols
		for (const auto& sym : doc.Symbols())
		{
			if (sym.name == ctx.prefix && sym.line == line) continue; // the word being typed itself
			Completion c;
			c.label = sym.name;
			c.insertText = sym.name;
			c.kind = sym.kind == Kind::Function ? Kind::Function : Kind::Local;
			if (sym.kind == Kind::Function)
			{
				c.detail = (sym.params.empty() ? "()" : sym.params);
				c.callable = true;
				c.hasParams = !sym.params.empty() && sym.params != "()";
				c.doc = std::string(sym.isLocal ? "local " : "") + "function, line " + std::to_string(sym.line + 1);
			}
			else
			{
				c.detail = DisplayType(doc.InferVariable(sym.name, line));
				c.doc = std::string(sym.isLocal ? "local" : "global") + " variable, line " + std::to_string(sym.line + 1);
			}
			c.score = MatchScore(sym.name, ctx.prefix);
			out.push_back(std::move(c));
		}

		// Engine globals
		for (const auto& g : reg.Globals())
		{
			Completion c;
			c.label = g.name;
			c.insertText = g.name;
			c.kind = g.IsCallable() ? Kind::Function : Kind::Global;
			c.detail = g.IsCallable() ? (g.params.empty() ? "()" : g.params) + (g.type.empty() ? "" : " -> " + g.type) : g.type;
			c.doc = g.doc;
			c.callable = g.IsCallable();
			c.hasParams = g.IsCallable() && !g.params.empty() && g.params != "()";
			c.score = MatchScore(g.name, ctx.prefix);
			out.push_back(std::move(c));
		}

		// Keywords
		static const char* keywords[] = { "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
			"local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while" };
		for (const char* k : keywords)
		{
			if (ctx.prefix.size() < 2 && !manual) break;
			Push(out, k, "keyword", "", Kind::Keyword, MatchScore(k, ctx.prefix) - 50);
		}

		// Snippets ($0 = caret)
		struct Snip { const char* label; const char* text; const char* doc; };
		static const Snip snippets[] = {
			{ "Start", "function Start()\n\t$0\nend", "Called once when Play mode starts." },
			{ "Update", "function Update(dt)\n\t$0\nend", "Called every frame; dt = seconds since the previous frame." },
			{ "fori", "for i = 1, 10 do\n\t$0\nend", "Numeric for loop." },
			{ "foripairs", "for i, item in ipairs($0) do\n\t\nend", "Loop over an array." },
			{ "forpairs", "for key, value in pairs($0) do\n\t\nend", "Loop over a table." },
			{ "if", "if $0 then\n\t\nend", "if block." },
			{ "ifelse", "if $0 then\n\t\nelse\n\t\nend", "if / else block." },
			{ "while", "while $0 do\n\t\nend", "while loop." },
			{ "function", "function $0()\n\t\nend", "Function definition." },
			{ "localfunction", "local function $0()\n\t\nend", "Local function definition." },
		};
		if (!ctx.prefix.empty())
		{
			for (const auto& sn : snippets)
			{
				Completion c;
				c.label = sn.label;
				c.insertText = sn.text;
				c.detail = "snippet";
				c.doc = std::string(sn.doc) + "\n\n" + sn.text;
				c.kind = Kind::Snippet;
				c.score = MatchScore(sn.label, ctx.prefix) - 20;
				out.push_back(std::move(c));
			}
		}

		FinishList(out);
		return !out.empty();
	}

	// =========================================================================================
	// Hover / signature help
	// =========================================================================================

	bool Describe(const DocumentModel& doc, int line, int col, std::string& title, std::string& body)
	{
		int idx = doc.TokenAt(line, col);
		if (idx < 0) return false;
		const auto& toks = doc.Tokens();
		const Token& t = toks[idx];
		if (t.type != Token::Type::Name) return false;

		if (idx >= 2 && (IsOp(toks[idx - 1], ".") || IsOp(toks[idx - 1], ":")))
		{
			int start = doc.ChainStart(idx - 2);
			std::string owner = doc.InferTokens(start, idx - 1, line);
			if (owner.empty()) return false;
			std::vector<Member> members;
			CollectMembers(owner, doc.Env(), members);
			for (const auto& m : members)
			{
				if (m.name != t.text) continue;
				title = DisplayType(owner) + (m.kind == Kind::Method ? ":" : ".") + m.Signature();
				body = m.doc;
				if (!m.IsCallable() && !m.type.empty())
				{
					if (const ScriptApi::Type* tt = ScriptApi::Registry::Get().FindType(m.type)) if (!tt->doc.empty()) body += "\n\n" + m.type + ": " + tt->doc;
				}
				return true;
			}
			title = DisplayType(owner) + " has no member '" + t.text + "'";
			body.clear();
			return true;
		}

		for (const auto& sym : doc.Symbols())
		{
			if (sym.name != t.text) continue;
			if (sym.kind == Kind::Function)
			{
				title = std::string(sym.isLocal ? "local " : "") + "function " + sym.name + (sym.params.empty() ? "()" : sym.params);
				body = "Defined at line " + std::to_string(sym.line + 1);
				return true;
			}
			std::string type = doc.InferVariable(sym.name, line);
			title = std::string(sym.isLocal ? "local " : "") + sym.name + (type.empty() ? "" : ": " + DisplayType(type));
			body.clear();
			if (IsScriptType(type))
			{
				body = "Functions and variables of " + FileName(type.substr(7)) + ":";
				if (const auto* members = ScriptMembers(type.substr(7)))
					for (const auto& m : *members) body += "\n  " + m.Signature();
			}
			else if (const ScriptApi::Type* tt = ScriptApi::Registry::Get().FindType(type))
				body = tt->doc;
			body += (body.empty() ? "" : "\n\n") + std::string("Declared at line ") + std::to_string(sym.line + 1);
			return true;
		}

		if (const Member* g = ScriptApi::Registry::Get().FindGlobal(t.text))
		{
			title = g->Signature();
			body = g->doc;
			if (!g->IsCallable())
				if (const ScriptApi::Type* tt = ScriptApi::Registry::Get().FindType(g->type)) if (tt->doc != g->doc && !tt->doc.empty()) body += "\n\n" + tt->doc;
			return true;
		}
		return false;
	}

	bool SignatureHelp(const DocumentModel& doc, int line, int col, std::string& signature, std::string& docText, int& activeParam)
	{
		if (line < 0 || line >= (int)doc.Lines().size()) return false;
		const std::string& text = doc.Lines()[line];
		std::string before = text.substr(0, std::min(col, (int)text.size()));
		std::vector<Token> toks;
		Tokenize(before, toks);
		int depth = 0;
		activeParam = 0;
		int open = -1;
		for (int k = (int)toks.size() - 1; k >= 0; k--)
		{
			const Token& t = toks[k];
			if (t.type != Token::Type::Op) continue;
			if (t.text == ")" || t.text == "]" || t.text == "}") depth++;
			else if (t.text == "(" || t.text == "[" || t.text == "{")
			{
				if (depth == 0) { if (t.text == "(") open = k; break; }
				depth--;
			}
			else if (t.text == "," && depth == 0) activeParam++;
		}
		if (open <= 0 || toks[open - 1].type != Token::Type::Name) return false;

		const std::string& fn = toks[open - 1].text;
		int nameIdx = open - 1;
		if (nameIdx >= 2 && (IsOp(toks[nameIdx - 1], ".") || IsOp(toks[nameIdx - 1], ":")))
		{
			int start = ChainStartOf(toks, nameIdx - 2);
			std::string owner = doc.InferExpression(TokensText(toks, start, nameIdx - 1), line);
			if (owner.empty()) return false;
			std::vector<Member> members;
			CollectMembers(owner, doc.Env(), members);
			for (const auto& m : members)
			{
				if (m.name != fn || !m.IsCallable()) continue;
				signature = DisplayType(owner) + (m.kind == Kind::Method ? ":" : ".") + m.Signature();
				docText = m.doc;
				return true;
			}
			return false;
		}
		for (const auto& sym : doc.Symbols())
		{
			if (sym.name == fn && sym.kind == Kind::Function)
			{
				signature = fn + (sym.params.empty() ? "()" : sym.params);
				docText = "line " + std::to_string(sym.line + 1);
				return true;
			}
		}
		if (const Member* g = ScriptApi::Registry::Get().FindGlobal(fn))
		{
			if (!g->IsCallable()) return false;
			signature = g->Signature();
			docText = g->doc;
			return true;
		}
		return false;
	}

	// =========================================================================================
	// Compiler
	// =========================================================================================

	CompileResult Compile(const std::string& code, const std::string& chunkName, const DocumentModel& model)
	{
		(void)chunkName;
		auto t0 = std::chrono::high_resolution_clock::now();
		CompileResult result;
		auto add = [&](Diagnostic::Severity sev, int line, int col, const std::string& msg) {
			for (const auto& d : result.diagnostics)
				if (d.line == line && d.column == col && d.message == msg) return;
			result.diagnostics.push_back({ sev, line, col, msg });
		};

		// 1) Syntax: compile with the real Lua parser (nothing runs)
		{
			lua_State* L = luaL_newstate();
			int status = luaL_loadbufferx(L, code.data(), code.size(), "=script", "t");
			if (status != LUA_OK)
			{
				result.syntaxOk = false;
				std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "syntax error";
				int line = 1;
				// "script:12: 'end' expected (to close 'function' at line 3) near <eof>"
				size_t c1 = msg.find(':');
				size_t c2 = c1 == std::string::npos ? std::string::npos : msg.find(':', c1 + 1);
				if (c1 != std::string::npos && c2 != std::string::npos)
				{
					line = std::max(1, std::atoi(msg.substr(c1 + 1, c2 - c1 - 1).c_str()));
					msg = msg.substr(c2 + 1);
					while (!msg.empty() && msg.front() == ' ') msg.erase(msg.begin());
				}
				add(Diagnostic::Severity::Error, line, 0, msg);
				size_t at = msg.find("at line ");
				if (at != std::string::npos)
				{
					int opened = std::atoi(msg.c_str() + at + 8);
					if (opened > 0 && opened != line) add(Diagnostic::Severity::Info, opened, 0, "Block opened here is never closed");
				}
			}
			lua_close(L);
		}

		// 2) Semantic checks (also on broken code: they help find the cause)
		auto& reg = ScriptApi::Registry::Get();
		const Environment* env = model.Env();
		const auto& toks = model.Tokens();
		const int n = (int)toks.size();
		std::vector<char> brackets;

		std::vector<std::string> knownNames;
		for (const auto& s : model.Symbols()) knownNames.push_back(s.name);
		for (const auto& g : reg.Globals()) knownNames.push_back(g.name);

		auto isAssetCall = [](const std::string& owner, const std::string& fn) {
			if (owner == "Assets") return fn == "Texture" || fn == "Material" || fn == "Model" || fn == "Audio";
			if (owner == "Resource") return fn == "Texture" || fn == "Model" || fn == "Audio" || fn == "Get" || fn == "FromPath";
			if (owner == "TextureLayer") return fn == "SetTexture" || fn == "SetNormalMap" || fn == "SetDisplacementMap";
			if (owner == "GameObject") return fn == "SetTexture" || fn == "AddLayer";
			return false;
		};

		for (int i = 0; i < n; i++)
		{
			const Token& t = toks[i];
			if (t.type == Token::Type::Op)
			{
				if (t.text == "(" || t.text == "[" || t.text == "{") brackets.push_back(t.text[0]);
				else if ((t.text == ")" || t.text == "]" || t.text == "}") && !brackets.empty()) brackets.pop_back();
			}

			// --- member access: owner.name / owner:name
			if (t.type == Token::Type::Name && i >= 2 && (IsOp(toks[i - 1], ".") || IsOp(toks[i - 1], ":")))
			{
				char sep = toks[i - 1].text[0];
				int start = model.ChainStart(i - 2);
				std::string owner = model.InferTokens(start, i - 1, t.line);
				if (!owner.empty() && ScriptApi::ElementType(owner).empty())
				{
					if (IsScriptType(owner))
					{
						const auto* members = ScriptMembers(owner.substr(7));
						if (members && std::none_of(members->begin(), members->end(), [&](const Member& m) { return m.name == t.text; }))
						{
							// assignments create new fields: only reads are suspicious
							bool isWrite = i + 1 < n && IsOp(toks[i + 1], "=");
							if (!isWrite)
								add(Diagnostic::Severity::Warning, t.line + 1, t.col + 1, "'" + t.text + "' is not defined in " + DisplayType(owner));
						}
					}
					else if (reg.FindType(owner))
					{
						const Member* m = reg.FindMember(owner, t.text);
						bool call = i + 1 < n && (IsOp(toks[i + 1], "(") || toks[i + 1].type == Token::Type::String);
						if (!m)
						{
							std::vector<std::string> names;
							std::vector<const Member*> members;
							reg.CollectMembers(owner, members);
							for (const Member* mm : members) names.push_back(mm->name);
							std::string hint = Closest(t.text, names);
							add(Diagnostic::Severity::Warning, t.line + 1, t.col + 1,
								"'" + t.text + "' is not a member of " + owner + (hint.empty() ? "" : " (did you mean '" + hint + "'?)"));
						}
						else if (sep == '.' && m->kind == Kind::Method && call)
							add(Diagnostic::Severity::Warning, t.line + 1, t.col + 1, "'" + t.text + "' is a method: call it with ':' (" + owner + ":" + t.text + "(...))");
						else if (sep == ':' && m->kind == Kind::Function)
							add(Diagnostic::Severity::Warning, t.line + 1, t.col + 1, "'" + t.text + "' is a function of " + owner + ": call it with '.'");
						else if (sep == ':' && (m->kind == Kind::Property || m->kind == Kind::Constant))
							add(Diagnostic::Severity::Warning, t.line + 1, t.col + 1, "'" + t.text + "' is a value, not a method: use '.'");
						else if (m->readOnly && !m->IsCallable() && m->kind == Kind::Property && i + 1 < n && IsOp(toks[i + 1], "="))
							add(Diagnostic::Severity::Warning, t.line + 1, t.col + 1, "'" + t.text + "' is read-only");
					}
				}
			}

			// --- string arguments: f("x") / obj:f("x")
			if (t.type == Token::Type::String && i >= 2 && IsOp(toks[i - 1], "(") && toks[i - 2].type == Token::Type::Name)
			{
				const std::string& fn = toks[i - 2].text;
				std::string owner;
				if (i >= 4 && (IsOp(toks[i - 3], ".") || IsOp(toks[i - 3], ":")))
					owner = model.InferTokens(model.ChainStart(i - 4), i - 3, t.line);
				const std::string& arg = t.text;
				int line = t.line + 1, col = t.col + 1;

				if (env && ((owner == "Scene" && fn == "Find") || (owner == "Resource" && (fn == "GameObject" || fn == "FindObject" || fn == "FromScene"))))
				{
					if (!env->HasObject(arg))
					{
						std::string hint = Closest(arg, env->objectNames);
						add(Diagnostic::Severity::Warning, line, col, "No object named '" + arg + "' in the current scene" + (hint.empty() ? "" : " (did you mean '" + hint + "'?)"));
					}
				}
				else if (env && owner == "Scene" && fn == "FindLight")
				{
					if (!env->HasLight(arg))
					{
						std::string hint = Closest(arg, env->lightNames);
						add(Diagnostic::Severity::Warning, line, col, "No light named '" + arg + "' in the current scene" + (hint.empty() ? "" : " (did you mean '" + hint + "'?)"));
					}
				}
				else if ((fn == "GetComponent" || fn == "HasComponent") && owner == "GameObject")
				{
					if (CanonicalComponentType(arg).empty())
					{
						std::string list;
						for (const auto& c : ComponentTypeNames()) list += (list.empty() ? "" : ", ") + c;
						add(Diagnostic::Severity::Warning, line, col, "Unknown component type '" + arg + "' (known: " + list + ")");
					}
				}
				else if ((fn == "GetScript" && owner == "GameObject") || (owner == "Script" && (fn == "Require" || fn == "Reload")))
				{
					std::string found = env ? env->FindScript(arg) : ScriptEngine::ResolveScriptPath(arg);
					if (found.empty()) add(Diagnostic::Severity::Warning, line, col, "No script named '" + arg + "' in Assets");
				}
				else if (isAssetCall(owner, fn))
				{
					std::error_code ec;
					if (!std::filesystem::exists(arg, ec)) add(Diagnostic::Severity::Warning, line, col, "File not found: " + arg);
				}
			}

			// --- variables that are never declared
			if (t.type == Token::Type::Name && !(i > 0 && (IsOp(toks[i - 1], ".") || IsOp(toks[i - 1], ":") || IsOp(toks[i - 1], "::") || IsKw(toks[i - 1], "goto"))))
			{
				bool tableKey = !brackets.empty() && brackets.back() == '{' && i + 1 < n && IsOp(toks[i + 1], "=") &&
					i > 0 && (IsOp(toks[i - 1], "{") || IsOp(toks[i - 1], ",") || IsOp(toks[i - 1], ";"));
				bool label = i + 1 < n && IsOp(toks[i + 1], "::");
				if (!tableKey && !label && !model.IsDeclared(t.text) && !reg.FindGlobal(t.text) && !BuiltinNames().count(t.text))
				{
					std::string hint = Closest(t.text, knownNames);
					add(Diagnostic::Severity::Warning, t.line + 1, t.col + 1, "Unknown variable '" + t.text + "'" + (hint.empty() ? "" : " (did you mean '" + hint + "'?)"));
				}
			}
		}

		// Engine callbacks with the wrong case never get called
		for (const auto& s : model.Symbols())
		{
			if (s.kind != Kind::Function || s.isLocal) continue;
			for (const char* cb : { "Start", "Update" })
				if (s.name != cb && Lower(s.name) == Lower(cb))
					add(Diagnostic::Severity::Info, s.line + 1, 0, "The engine calls '" + std::string(cb) + "' (case-sensitive); '" + s.name + "' will never run");
		}

		std::stable_sort(result.diagnostics.begin(), result.diagnostics.end(), [](const Diagnostic& a, const Diagnostic& b) {
			if (a.line != b.line) return a.line < b.line;
			return (int)a.severity < (int)b.severity;
		});
		for (const auto& d : result.diagnostics)
		{
			if (d.severity == Diagnostic::Severity::Error) result.errors++;
			else if (d.severity == Diagnostic::Severity::Warning) result.warnings++;
			else result.infos++;
		}
		result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
		return result;
	}
}
