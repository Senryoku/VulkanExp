#pragma once

#include <Logger.hpp>
#include <chrono>
#include <stack>
#include <string>

#include <RollingBuffer.hpp>

class QuickTimer {
  public:
	QuickTimer(const std::string& name) : _name(name) { begin(); }
	QuickTimer(RollingBuffer<float>& rollingBuffer) : _rollingBuffer(&rollingBuffer) { begin(); }

	~QuickTimer() {
		if(_running)
			end();
	}

	void begin() {
		assert(!_running);
		start();
		reportStart();
		TimerStack.push(this);
	}

	void end() {
		assert(_running);
		TimerStack.pop();
		stop();
		report();
	}

	void start() {
		_start = std::chrono::high_resolution_clock::now();
		_running = true;
	}

	void stop() {
		_end = std::chrono::high_resolution_clock::now();
		_running = false;
	}

	void reportStart() {
		if(!shouldReport())
			return;
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
		if(_rollingBuffer) {
			_rollingBuffer->add(d.count() / 1000000.0f);
		}

		if(shouldReport()) {
			if(d.count() > 10000000)
				report(std::chrono::duration_cast<std::chrono::milliseconds>(d));
			else if(d.count() > 1000000)
				report(fmt::format("{:.1f}ms", d.count() / 1000000.0));
			else if(d.count() > 10000)
				report(std::chrono::duration_cast<std::chrono::microseconds>(d));
			else
				report(std::chrono::duration_cast<std::chrono::nanoseconds>(d));
		}
	}

  private:
	std::string									   _name;
	RollingBuffer<float>*						   _rollingBuffer = nullptr;
	std::chrono::high_resolution_clock::time_point _start;
	std::chrono::high_resolution_clock::time_point _end;
	bool										   _running = false;

	bool shouldReport() { return !_rollingBuffer; }

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
