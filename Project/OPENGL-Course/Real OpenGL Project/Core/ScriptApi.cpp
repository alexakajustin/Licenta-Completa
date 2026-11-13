#include "Core/ScriptApi.h"

namespace ScriptApi
{
	std::string Member::Signature() const
	{
		if (IsCallable())
		{
			std::string s = name + (params.empty() ? "()" : params);
			if (!type.empty()) s += " -> " + type;
			return s;
		}
		if (kind == Kind::Keyword || kind == Kind::Snippet || kind == Kind::Type) return name;
		return type.empty() ? name : name + ": " + type;
	}

	const Member* Type::Find(const std::string& memberName) const
	{
		for (const auto& m : members)
			if (m.name == memberName) return &m;
		return nullptr;
	}

	Member& Type::Add(Member m)
	{
		// Re-binding a name replaces its documentation (keeps the list free of duplicates)
		for (auto& existing : members)
		{
			if (existing.name == m.name) { existing = std::move(m); return existing; }
		}
		members.push_back(std::move(m));
		return members.back();
	}

	Registry& Registry::Get()
	{
		static Registry instance;
		return instance;
	}

	Type& Registry::AddType(const std::string& name, const std::string& doc, const std::string& base, bool isTable)
	{
		m_Revision++;
		auto it = m_TypeIndex.find(name);
		if (it != m_TypeIndex.end())
		{
			Type& t = m_Types[it->second];
			if (!doc.empty()) t.doc = doc;
			if (!base.empty()) t.base = base;
			t.isTable = t.isTable || isTable;
			return t;
		}
		m_TypeIndex[name] = m_Types.size();
		m_Types.push_back(Type{ name, doc, base, isTable, {} });
		return m_Types.back();
	}

	const Type* Registry::FindType(const std::string& name) const
	{
		auto it = m_TypeIndex.find(name);
		return it == m_TypeIndex.end() ? nullptr : &m_Types[it->second];
	}

	void Registry::AddGlobal(Member m)
	{
		m_Revision++;
		for (auto& g : m_Globals)
		{
			if (g.name == m.name) { g = std::move(m); return; }
		}
		m_Globals.push_back(std::move(m));
	}

	const Member* Registry::FindGlobal(const std::string& name) const
	{
		for (const auto& g : m_Globals)
			if (g.name == name) return &g;
		return nullptr;
	}

	void Registry::CollectMembers(const std::string& typeName, std::vector<const Member*>& out) const
	{
		std::string current = typeName;
		for (int depth = 0; depth < 8 && !current.empty(); depth++)
		{
			const Type* t = FindType(current);
			if (!t) break;
			for (const auto& m : t->members)
			{
				bool shadowed = false;
				for (const Member* o : out) if (o->name == m.name) { shadowed = true; break; }
				if (!shadowed) out.push_back(&m);
			}
			current = t->base;
		}
	}

	const Member* Registry::FindMember(const std::string& typeName, const std::string& memberName) const
	{
		std::string current = typeName;
		for (int depth = 0; depth < 8 && !current.empty(); depth++)
		{
			const Type* t = FindType(current);
			if (!t) return nullptr;
			if (const Member* m = t->Find(memberName)) return m;
			current = t->base;
		}
		return nullptr;
	}

	std::string ElementType(const std::string& type)
	{
		if (type.size() > 2 && type.compare(type.size() - 2, 2, "[]") == 0)
			return type.substr(0, type.size() - 2);
		return "";
	}

	Table::Table(sol::state& lua, const char* globalName, const char* doc)
		: m_Table(lua[globalName].valid() && lua[globalName].get_type() == sol::type::table
			? lua[globalName].get<sol::table>()
			: lua.create_named_table(globalName)),
		  m_Meta(Registry::Get().AddType(globalName, doc, "", true))
	{
		Registry::Get().AddGlobal({ globalName, Kind::Global, globalName, "", doc, true });
	}

	void DocumentEnum(const char* name, const char* doc, const std::vector<std::pair<std::string, std::string>>& entries)
	{
		Type& t = Registry::Get().AddType(name, doc, "", true);
		for (const auto& e : entries)
			t.Add({ e.first, Kind::Constant, name, "", e.second, true });
		Registry::Get().AddGlobal({ name, Kind::Global, name, "", doc, true });
	}
}
