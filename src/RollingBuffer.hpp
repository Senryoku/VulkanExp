#pragma once

template<class T>
class RollingBuffer {
  public:
	RollingBuffer(size_t max = 1024) { _buffer.resize(max); }

	void add(const T& val) {
		_buffer[_end] = val;
		++_end;
		if(_end >= _buffer.size()) {
			_end = 0;
			_rolled = true;
		}
	}

	struct Views {
		const T* first;
		size_t	 firstCount;
		const T* second;
		size_t	 secondCount;
	};

	Views get() const {
		if(!_rolled)
			return {
				.first = _buffer.data(),
				.firstCount = _end,
				.second = nullptr,
				.secondCount = 0,
			};
		else
			return {
				.first = _buffer.data() + _end,
				.firstCount = _buffer.size() - _end,
				.second = _buffer.data(),
				.secondCount = _end,
			};
	}

  private:
	size_t		   _end = 0;
	bool		   _rolled = false; // We rolled over at least once
	std::vector<T> _buffer;
};
