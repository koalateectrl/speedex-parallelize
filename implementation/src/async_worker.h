#pragma once 

#include <atomic>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace edce {

struct AsyncWorker {
	mutable std::mutex mtx;
	std::condition_variable cv;
	bool done_flag = false;
	//auto done_wait_lambda;

/*	AsyncWorker(auto done_wait_lambda)
		: mtx()
		, cv()
		, done_wait_lambda(done_wait_lambda) {}
*/

	virtual bool exists_work_to_do() = 0;

	void wait_for_async_task() {
		std::unique_lock lock(mtx);
		if (!exists_work_to_do()) return;
		cv.wait(lock, [this] {return !exists_work_to_do();});
	}

	void start_async_thread(auto run_lambda) {
		std::thread(run_lambda).detach();
	}

	void end_async_thread() {
		std::lock_guard lock(mtx);
		done_flag = true;
		cv.notify_all();
	}

	//~AsyncWorker() {
	//	wait_for_async_task();
	//	end_async_thread();
	//}
};

}
