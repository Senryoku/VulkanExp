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
		TimerStack.push(this);
	}

	~QuickTimer() {
		TimerStack.pop();
		stop();
		report();
	}

	void start() { _start = std::chrono::high_resolution_clock::now(); }

	void stop() { _end = std::chrono::high_resolution_clock::now(); }

	void reportStart() {
		if(TimerStack.empty())
			print("> {}...", _name);
		else {
			if(LastReportDepth < TimerStack.size())
				print("\n");
			print("{: >{}}> {}...", "", 2 * TimerStack.size(), _name);
		}
		LastReportDepth = TimerStack.size();
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
		if(LastReportDepth > TimerStack.size()) {
			success("{: >{}}< Done", "", 2 * TimerStack.size());
			print(" ({})\n", d);
		} else {
			success("\tDone");
			print(" ({})\n", d);
		}
		LastReportDepth = TimerStack.size();
	}

	inline static std::stack<QuickTimer*> TimerStack;
	inline static size_t				  LastReportDepth = 0;
};
