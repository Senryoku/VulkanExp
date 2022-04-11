#pragma once

#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool {
  public:
	class TaskQueue {
	  public:
		~TaskQueue() { wait(); }

		const std::future<void>& start(std::function<void()>&& func) {
			_tasks.emplace_back(ThreadPool::GetInstance().queue(std::forward<std::function<void()>>(func)));
			return _tasks.back();
		}

		void wait() {
			for(const auto& f : _tasks)
				f.wait();
			_tasks.clear();
		}

	  private:
		std::vector<std::future<void>> _tasks;
	};

	ThreadPool(uint32_t threadCount = std::thread::hardware_concurrency() - 1);
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	~ThreadPool();

	static ThreadPool& GetInstance() {
		static ThreadPool globalThreadPool;
		return globalThreadPool;
	}

	void startThreads(uint32_t threadCount = std::thread::hardware_concurrency() - 1);

	std::future<void> queue(std::function<void()>&& func) {
		auto task = std::packaged_task<void()>(std::forward<std::function<void()>>(func));
		auto future = task.get_future();
		{
			std::unique_lock<std::mutex> lock(_tasksMutex);
			_tasks.push_back(std::move(task));
		}
		_tasksAvailable.notify_one();
		return future;
	}

  private:
	std::vector<std::thread>			   _threads;
	std::deque<std::packaged_task<void()>> _tasks;
	std::mutex							   _tasksMutex;
	std::condition_variable				   _tasksAvailable;

	void threadLoop() {
		std::packaged_task<void()> localTask;
		while(true) {
			{
				std::unique_lock<std::mutex> lock(_tasksMutex);
				if(_tasks.empty())
					_tasksAvailable.wait(lock, [&] { return !_tasks.empty(); });
				localTask = std::move(_tasks.front());
				_tasks.pop_front();
			}
			if(!localTask.valid())
				return;
			localTask();
		}
	}
};
