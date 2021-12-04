#pragma once

#include <Logger.hpp>
#include <chrono>
#include <stack>
#include <string>

class QuickTimer {
  public:
	QuickTimer(const std::string& name) : _name(name) {
		start();
		reportStart();
		_timerStack.push(this);
	}

	~QuickTimer() {
		_timerStack.pop();
		stop();
		report();
	}

	void start() { _start = std::chrono::high_resolution_clock::now(); }

	void stop() { _end = std::chrono::high_resolution_clock::now(); }

	void reportStart() {
		if(_timerStack.empty())
			print("{}... ", _name);
		else
			print("[{}... ", _name);
	}

	void report() {
		auto d = _end - _start;
		if(d.count() > 10000000)
			report(std::chrono::duration_cast<std::chrono::milliseconds>(d));
		else if(d.count() > 10000)
			report(std::chrono::duration_cast<std::chrono::microseconds>(d));
		else
			report(std::chrono::duration_cast<std::chrono::nanoseconds>(d));
	}

  private:
	std::string									   _name;
	std::chrono::high_resolution_clock::time_point _start;
	std::chrono::high_resolution_clock::time_point _end;

	template<typename T>
	void report(const T& d) {
		success("Done");
		if(_timerStack.empty()) {
			print(" ({}).\n", d);
		} else {
			print(" ({})] ", d);
		}
	}

	inline static std::stack<QuickTimer*> _timerStack;
};
