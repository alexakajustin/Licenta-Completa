#pragma once

#include "Core/ScriptApi.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

/**
 * @file ScriptAnalysis.h
 * @brief Static understanding of Lua scripts for the Script Editor.
 *
 * - Tokenize: a Lua 5.4 lexer (comments / strings skipped, positions kept).
 * - DocumentModel: the symbols of one script (locals, globals, functions, parameters) and a type
 *   inferencer that follows member chains through the ScriptApi registry:
 *       Scene.Find("Cube"):GetLayer(0).texture   ->  Texture
 *   Calls with a string argument are understood too: GetComponent("RigidBody") -> RigidBody,
 *   GetScript("enemy") / Script.Require("enemy") -> the members of enemy.lua.
 * - Completions, hover text and signature help built on that model.
 * - Compile: a real Lua syntax check (luaL_loadbuffer, nothing is executed) followed by semantic
 *   checks: unknown members, '.' vs ':' calls, missing scene objects / assets / scripts and
 *   unknown variables.
 */
namespace ScriptAnalysis
{
	// ---------------------------------------------------------------------------------------
	// Lexer
	// ---------------------------------------------------------------------------------------
	struct Token
	{
		enum class Type { Name, Keyword, String, Number, Op };
		Type type = Type::Op;
		std::string text;   // strings: their content without quotes
		int line = 0;       // 0-based
		int col = 0;        // 0-based, first character
		int endLine = 0;
		int endCol = 0;     // one past the last character
	};

	struct Span // a comment or string, for "is this position inside text?"
	{
		int line, col, endLine, endCol;
		bool comment;
	};

	void Tokenize(const std::string& code, std::vector<Token>& tokens, std::vector<Span>* spans = nullptr);
	bool IsKeyword(const std::string& word);

	// ---------------------------------------------------------------------------------------
	// Project knowledge the analysis can use (filled by the editor)
	// ---------------------------------------------------------------------------------------
	struct Environment
	{
		std::vector<std::string> objectNames;   // scene objects (children included)
		std::vector<std::string> lightNames;    // scene lights
		std::vector<std::string> scriptFiles;   // Assets/**.lua (project-relative)
		std::vector<std::string> textureFiles;
		std::vector<std::string> materialFiles;
		std::vector<std::string> modelFiles;
		std::vector<std::string> audioFiles;
		std::string selfScriptPath;             // the script being edited
		std::vector<std::string> selfObjects;   // objects that run this script ('gameObject')

		bool HasObject(const std::string& name) const;
		bool HasLight(const std::string& name) const;
		std::string FindScript(const std::string& nameOrPath) const; // "" if none
	};

	// ---------------------------------------------------------------------------------------
	// Document model + type inference
	// ---------------------------------------------------------------------------------------
	struct SymbolInfo
	{
		std::string name;
		ScriptApi::Kind kind = ScriptApi::Kind::Local;
		std::string params;     // functions
		int line = 0;           // 0-based declaration line
		bool isLocal = true;
	};

	class DocumentModel
	{
	public:
		void Build(const std::string& text, const Environment* env);

		// Type of an expression written in Lua (e.g. "Scene.Find(\"Cube\").transform") as seen
		// at 'line'. Returns "" when unknown.
		std::string InferExpression(const std::string& expr, int line) const;
		// Type of a variable as seen at 'line'
		std::string InferVariable(const std::string& name, int line) const;
		// Type of the token range [first, last) of this document
		std::string InferTokens(int first, int last, int line) const;

		const std::vector<Token>& Tokens() const { return m_Tokens; }
		const std::vector<Span>& Spans() const { return m_Spans; }
		const std::vector<std::string>& Lines() const { return m_Lines; }
		const std::vector<SymbolInfo>& Symbols() const { return m_Symbols; }
		bool IsDeclared(const std::string& name) const { return m_Declared.count(name) > 0; }
		bool IsInsideTextOrComment(int line, int col) const;
		int TokenAt(int line, int col) const; // index of the token covering (line, col) or -1

		// Members visible from outside (globals and functions): what another script gets through
		// GetScript / Script.Require
		std::vector<ScriptApi::Member> ExportedMembers() const;

		// Start index of the member-access chain ending at token 'last' (inclusive)
		int ChainStart(int last) const;

		const Environment* Env() const { return m_Env; }

	private:
		struct Assignment
		{
			std::string name;
			int line = 0;
			int exprFirst = -1, exprLast = -1; // token range [first, last) of the value
			std::string fixedType;             // known without looking at tokens ("number"...)
			bool isLocal = false;
			int iterOf = 0;                    // 1: element of ipairs(expr), 2: key of pairs
		};

		std::string Resolve(const std::vector<Token>& toks, int first, int last, int line, int depth) const;
		std::string ResolveName(const std::string& name, int line, int depth) const;

		std::vector<std::string> m_Lines;
		std::vector<Token> m_Tokens;
		std::vector<Span> m_Spans;
		std::vector<Assignment> m_Assignments;
		std::vector<SymbolInfo> m_Symbols;
		std::unordered_set<std::string> m_Declared;
		const Environment* m_Env = nullptr;
	};

	// Members of a type, including script types ("script:Assets/scripts/enemy.lua")
	void CollectMembers(const std::string& type, const Environment* env, std::vector<ScriptApi::Member>& out);
	const std::vector<ScriptApi::Member>* ScriptMembers(const std::string& scriptPath); // cached by mtime
	std::string ScriptTypeFor(const std::string& nameOrPath, const Environment* env);    // "script:<path>" or ""
	std::string DisplayType(const std::string& type); // "script:Assets/scripts/enemy.lua" -> "script enemy.lua"

	// ---------------------------------------------------------------------------------------
	// Completion / hover / signature help
	// ---------------------------------------------------------------------------------------
	struct Completion
	{
		std::string label;       // shown
		std::string insertText;  // inserted (defaults to label)
		std::string detail;      // type / signature, right aligned
		std::string doc;
		ScriptApi::Kind kind = ScriptApi::Kind::Local;
		int score = 0;
		bool callable = false;   // add "()" and put the caret inside
		bool hasParams = false;
		bool fixSeparator = false; // method chosen after '.': turn the '.' into ':'
	};

	struct CompletionContext
	{
		int line = 0;
		int replaceStart = 0;    // column where the replaced text starts
		int replaceEnd = 0;      // column where it ends (cursor)
		std::string prefix;
		bool member = false;     // after '.' or ':'
		char separator = 0;
		int separatorCol = -1;
		bool inString = false;   // completing a string argument (object names, paths...)
		std::string ownerType;
	};

	// Returns false when nothing should be offered (inside a comment, plain string...)
	bool Complete(const DocumentModel& doc, int line, int col, bool manual, CompletionContext& ctx, std::vector<Completion>& out);

	// Hover help for the word at (line, col): title (signature) + body (documentation)
	bool Describe(const DocumentModel& doc, int line, int col, std::string& title, std::string& body);

	// Signature of the call whose argument list contains (line, col)
	bool SignatureHelp(const DocumentModel& doc, int line, int col, std::string& signature, std::string& doc_, int& activeParam);

	// ---------------------------------------------------------------------------------------
	// Compiler
	// ---------------------------------------------------------------------------------------
	struct Diagnostic
	{
		enum class Severity { Error, Warning, Info };
		Severity severity = Severity::Error;
		int line = 1;     // 1-based
		int column = 0;   // 1-based, 0 = whole line
		std::string message;
	};

	struct CompileResult
	{
		bool syntaxOk = true;
		int errors = 0, warnings = 0, infos = 0;
		double milliseconds = 0.0;
		std::vector<Diagnostic> diagnostics; // sorted by line
	};

	CompileResult Compile(const std::string& code, const std::string& chunkName, const DocumentModel& model);
}
