#include "EditHistory.hpp"

EditHistory::~EditHistory() {
	clear();
}

void EditHistory::clear() {
	_actions.clear();
	_undoneActions = 0;
}

bool EditHistory::undo() {
	if(_undoneActions < _actions.size()) {
		_actions[_actions.size() - (_undoneActions + 1)]->undo();
		++_undoneActions;
		return true;
	}
	return false;
}

bool EditHistory::redo() {
	if(!_actions.empty() && _undoneActions > 0) {
		_actions[_actions.size() - _undoneActions]->redo();
		--_undoneActions;
		return true;
	}
	return false;
}

void EditHistory::push(Undoable* action) {
	if(_undoneActions > 0) {
		// Delete divergent actions.
		_actions.resize(_actions.size() - _undoneActions);
		_undoneActions = 0;
	}
	_actions.emplace_back(action);
}
