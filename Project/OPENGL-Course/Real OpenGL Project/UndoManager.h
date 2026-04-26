#pragma once

#include <vector>
#include <memory>
#include <string>
#include <cstdio>

// =====================================================================
// UndoAction — Abstract base for all undoable operations
// =====================================================================
class UndoAction
{
public:
	virtual ~UndoAction() = default;
	virtual void Undo() = 0;
	virtual void Redo() = 0;
	virtual std::string GetDescription() const = 0;
};

// =====================================================================
// UndoManager — Manages undo/redo stacks using the Command Pattern
// =====================================================================
class UndoManager
{
public:
	UndoManager() = default;
	~UndoManager() = default;

	// Execute an action (it has already been performed), push to undo stack
	void PushAction(std::unique_ptr<UndoAction> action)
	{
		// Adding a new action invalidates the redo history
		redoStack.clear();

		undoStack.push_back(std::move(action));
		printf("[UndoManager] Pushed: %s (Stack: %d)\n",
			undoStack.back()->GetDescription().c_str(), (int)undoStack.size());

		// Enforce max stack size
		while ((int)undoStack.size() > maxStackSize) {
			undoStack.erase(undoStack.begin());
		}
	}

	void Undo()
	{
		if (undoStack.empty()) return;

		auto action = std::move(undoStack.back());
		undoStack.pop_back();

		printf("[UndoManager] Undo: %s\n", action->GetDescription().c_str());
		action->Undo();

		redoStack.push_back(std::move(action));
	}

	void Redo()
	{
		if (redoStack.empty()) return;

		auto action = std::move(redoStack.back());
		redoStack.pop_back();

		printf("[UndoManager] Redo: %s\n", action->GetDescription().c_str());
		action->Redo();

		undoStack.push_back(std::move(action));
	}

	bool CanUndo() const { return !undoStack.empty(); }
	bool CanRedo() const { return !redoStack.empty(); }

	void Clear()
	{
		undoStack.clear();
		redoStack.clear();
	}

	int GetUndoCount() const { return (int)undoStack.size(); }
	int GetRedoCount() const { return (int)redoStack.size(); }

	std::string GetUndoDescription() const {
		return undoStack.empty() ? "" : undoStack.back()->GetDescription();
	}
	std::string GetRedoDescription() const {
		return redoStack.empty() ? "" : redoStack.back()->GetDescription();
	}

private:
	std::vector<std::unique_ptr<UndoAction>> undoStack;
	std::vector<std::unique_ptr<UndoAction>> redoStack;
	int maxStackSize = 100;
};
