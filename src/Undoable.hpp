#pragma once

// Purely virtual Undoable action
class Undoable {
  public:
	virtual ~Undoable() = default;

	virtual void undo() = 0;
	virtual void redo() = 0;

  private:
};
