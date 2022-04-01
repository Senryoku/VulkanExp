#pragma once

#include <vector>

#include "Undoable.hpp"

class EditHistory {
  public:
	~EditHistory();

	void clear();

	bool undo();
	bool redo();

	void push(Undoable* action) {
		if(_undoneActions > 0) {
			// Delete divergent actions.
			for(auto i = _actions.size() - _undoneActions; i < _actions.size(); ++i)
				delete _actions[i];
			_actions.resize(_actions.size() - _undoneActions);
			_undoneActions = 0;
		}
		_actions.push_back(action);
	}

  private:
	size_t				   _undoneActions = 0;
	std::vector<Undoable*> _actions;
};

EditHistory::~EditHistory() {
	clear();
}

void EditHistory::clear() {
	for(const auto& a : _actions)
		delete a;
	_actions.clear();
	_undoneActions = 0;
}

bool EditHistory::undo() {
	if(_undoneActions < _actions.size()) {
		_actions[_actions.size() - _undoneActions]->undo();
		++_undoneActions;
		return true;
	}
	return false;
}

bool EditHistory::redo() {
	if(!_actions.empty() && _undoneActions > 0) {
		_actions[_actions.size() - (_undoneActions + 1)]->undo();
		--_undoneActions;
		return true;
	}
	return false;
}
