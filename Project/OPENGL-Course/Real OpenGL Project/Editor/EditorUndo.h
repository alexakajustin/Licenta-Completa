#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include "Scene/UndoManager.h"
#include "GraphicsSettings.h"

// =====================================================================
// Undo for the node graphs and the graphics settings
//
// Both work by snapshots taken when an interaction ends (nothing pressed, no widget active): the
// state is compared with the last snapshot and a difference becomes one undo step on the scene's
// stack, next to the object edits. Every kind of change is covered without each widget or
// operation recording itself: parameters, moved nodes, links, added and deleted nodes, templates.
// =====================================================================

// A graph before and after one edit
class GraphEditAction : public UndoAction
{
public:
	using Restore = std::function<void(uint64_t graphUid, const std::string& snapshot)>;
	GraphEditAction(Restore restore, uint64_t graphUid, std::string before, std::string after, std::string description)
		: restore(std::move(restore)), graphUid(graphUid), before(std::move(before)), after(std::move(after)), description(std::move(description)) {}
	void Undo() override { restore(graphUid, before); }
	void Redo() override { restore(graphUid, after); }
	std::string GetDescription() const override { return description; }

private:
	Restore restore;
	uint64_t graphUid;
	std::string before, after, description;
};

// The graphics settings before and after one edit
class SettingsEditAction : public UndoAction
{
public:
	using Restore = std::function<void(const GraphicsSettings&)>;
	SettingsEditAction(Restore restore, const GraphicsSettings& before, const GraphicsSettings& after, std::string description)
		: restore(std::move(restore)), before(before), after(after), description(std::move(description)) {}
	void Undo() override { restore(before); }
	void Redo() override { restore(after); }
	std::string GetDescription() const override { return description; }

private:
	Restore restore;
	GraphicsSettings before, after;
	std::string description;
};
