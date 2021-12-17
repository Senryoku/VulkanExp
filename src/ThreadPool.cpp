#include <ThreadPool.hpp>

#include <cassert>

ThreadPool::ThreadPool(uint32_t threadCount) {
	startThreads(threadCount);
}

ThreadPool::~ThreadPool() {
	{
		std::unique_lock<std::mutex> lock(_tasksMutex);
		// Push empty packaged_task to signal threads they have to terminate
		for(size_t i = 0; i < _threads.size(); ++i) {
			_tasks.push_back({});
		}
	}
	_tasksAvailable.notify_all();
	for(auto& thread : _threads)
		thread.join();
	_threads.clear();
}

void ThreadPool::startThreads(uint32_t threadCount) {
	assert(_threads.empty());
	_threads.reserve(threadCount);
	for(size_t i = 0; i < threadCount; ++i)
		_threads.emplace_back(&ThreadPool::threadLoop, this);
}
