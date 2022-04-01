#pragma once

#include <memory>
#include <vector>

#include "Undoable.hpp"

class EditHistory {
  public:
	~EditHistory();

	void clear();

	bool undo();
	bool redo();
	void push(Undoable* action);

	inline bool canUndo() const { return !_actions.empty() && _undoneActions < _actions.size(); }
	inline bool canRedo() const { return !_actions.empty() && _undoneActions > 0; }

  private:
	size_t								   _undoneActions = 0;
	std::vector<std::unique_ptr<Undoable>> _actions;
};
