#pragma once

#ifndef SOL_DEFAULT_PASS_ON_ERROR
#define SOL_DEFAULT_PASS_ON_ERROR 1
#endif

#include <sol/sol.hpp>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <utility>

/**
 * @file ScriptApi.h
 * @brief Single source of truth for the Lua scripting API.
 *
 * Every binding is declared through ScriptApi::Class / ScriptApi::Table, which registers it with
 * sol2 AND records its metadata (kind, type, parameters, documentation) in the Registry. The
 * Script Editor reads that metadata for autocomplete, hover help, signature help and its static
 * checks, so the editor always describes exactly what the runtime exposes.
 *
 * Type strings used in the metadata:
 *   - a registered type name ("GameObject", "vec3", ...) or a primitive ("number", "string",
 *     "boolean", "table", "function", "any")
 *   - "T[]" for a Lua array of T (indexing / ipairs yields T)
 *   - "" for "returns nothing"
 */
namespace ScriptApi
{
	enum class Kind
	{
		Method,     // usertype member function, called with ':'
		Function,   // table / global function, called with '.'
		Property,   // value read (and maybe written) with '.'
		Constant,   // read-only value (enum entries, key codes)
		Global,     // global variable (gameObject, Scene, ...)
		Type,       // a registered type name
		Keyword,    // Lua keyword (editor only)
		Snippet,    // code template (editor only)
		Local,      // local variable of the current document (editor only)
		ScriptMember // function / variable defined by another script (editor only)
	};

	struct Member
	{
		std::string name;
		Kind kind = Kind::Property;
		std::string type;     // value type, or return type for callables ("" = nothing)
		std::string params;   // "(x, y, z)" for callables
		std::string doc;
		bool readOnly = false;

		bool IsCallable() const { return kind == Kind::Method || kind == Kind::Function; }
		std::string Signature() const; // "SetPosition(pos: vec3)" / "position: vec3"
	};

	struct Type
	{
		std::string name;
		std::string doc;
		std::string base;     // inherited members (e.g. RigidBody -> Component)
		bool isTable = false; // a global table of functions (Scene, Input...) rather than a usertype
		std::vector<Member> members;

		const Member* Find(const std::string& memberName) const;
		Member& Add(Member m);
	};

	class Registry
	{
	public:
		static Registry& Get();

		Type& AddType(const std::string& name, const std::string& doc, const std::string& base = "", bool isTable = false);
		const Type* FindType(const std::string& name) const;
		const std::deque<Type>& Types() const { return m_Types; }

		void AddGlobal(Member m);
		const Member* FindGlobal(const std::string& name) const;
		const std::vector<Member>& Globals() const { return m_Globals; }

		// Members of a type including the ones inherited through 'base'
		void CollectMembers(const std::string& typeName, std::vector<const Member*>& out) const;
		const Member* FindMember(const std::string& typeName, const std::string& memberName) const;

		// Incremented whenever the API changes (editor caches can key on it)
		int Revision() const { return m_Revision; }

	private:
		std::deque<Type> m_Types; // deque: stable references while binding
		std::unordered_map<std::string, size_t> m_TypeIndex;
		std::vector<Member> m_Globals;
		int m_Revision = 0;
	};

	// Element type of an array type ("TextureLayer[]" -> "TextureLayer"), or "" if not an array
	std::string ElementType(const std::string& type);

	/**
	 * Binds members onto a sol2 usertype and documents them in one call.
	 *   ScriptApi::Class<Transform>(ut, meta).Property("position", get, set, "vec3", "World position");
	 */
	template <typename T>
	class Class
	{
	public:
		Class(sol::usertype<T>& usertype, Type& meta) : m_UT(usertype), m_Meta(meta) {}

		template <typename F>
		Class& Method(const char* name, F&& fn, const char* params, const char* returnType, const char* doc)
		{
			m_UT.set(name, std::forward<F>(fn));
			m_Meta.Add({ name, Kind::Method, returnType, params, doc, true });
			return *this;
		}

		// A function on the type table itself, called with '.' (Resource.Texture(path))
		template <typename F>
		Class& Static(const char* name, F&& fn, const char* params, const char* returnType, const char* doc)
		{
			m_UT.set(name, std::forward<F>(fn));
			m_Meta.Add({ name, Kind::Function, returnType, params, doc, true });
			return *this;
		}

		template <typename G>
		Class& ReadOnly(const char* name, G&& getter, const char* type, const char* doc)
		{
			m_UT.set(name, sol::property(std::forward<G>(getter)));
			m_Meta.Add({ name, Kind::Property, type, "", doc, true });
			return *this;
		}

		template <typename G, typename S>
		Class& Property(const char* name, G&& getter, S&& setter, const char* type, const char* doc)
		{
			m_UT.set(name, sol::property(std::forward<G>(getter), std::forward<S>(setter)));
			m_Meta.Add({ name, Kind::Property, type, "", doc, false });
			return *this;
		}

		template <typename M>
		Class& Field(const char* name, M T::* field, const char* type, const char* doc)
		{
			m_UT.set(name, field);
			m_Meta.Add({ name, Kind::Property, type, "", doc, false });
			return *this;
		}

		// Documents a member bound elsewhere (constructors, metamethods set at creation time)
		Class& Doc(Member m) { m_Meta.Add(std::move(m)); return *this; }

	private:
		sol::usertype<T>& m_UT;
		Type& m_Meta;
	};

	/**
	 * A global table of functions / constants (Scene, Input, Assets...), bound and documented
	 * together. Also registers the global itself.
	 */
	class Table
	{
	public:
		Table(sol::state& lua, const char* globalName, const char* doc);

		template <typename F>
		Table& Function(const char* name, F&& fn, const char* params, const char* returnType, const char* doc)
		{
			m_Table.set_function(name, std::forward<F>(fn));
			m_Meta.Add({ name, Kind::Function, returnType, params, doc, true });
			return *this;
		}

		template <typename V>
		Table& Constant(const char* name, V&& value, const char* type, const char* doc)
		{
			m_Table[name] = std::forward<V>(value);
			m_Meta.Add({ name, Kind::Constant, type, "", doc, true });
			return *this;
		}

		// A value the engine updates (Time.deltaTime): documented as a read-only property
		template <typename V>
		Table& Value(const char* name, V&& value, const char* type, const char* doc)
		{
			m_Table[name] = std::forward<V>(value);
			m_Meta.Add({ name, Kind::Property, type, "", doc, true });
			return *this;
		}

		sol::table& Raw() { return m_Table; }

	private:
		sol::table m_Table;
		Type& m_Meta;
	};

	// Registers an enum both in Lua (lua.new_enum) and in the metadata, as a table of constants
	void DocumentEnum(const char* name, const char* doc, const std::vector<std::pair<std::string, std::string>>& entries);
}
