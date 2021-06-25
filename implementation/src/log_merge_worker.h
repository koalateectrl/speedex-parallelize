#pragma once
#include "async_worker.h"
#include "edce_management_structures.h"

#include <condition_variable>
#include <mutex>

namespace edce {

class LogMergeWorker : public AsyncWorker {
	using AsyncWorker::mtx;
	using AsyncWorker::cv;

	EdceManagementStructures& management_structures;

	//std::vector<SerialAccountModificationLog> logs;
	bool logs_ready_for_merge = false;

	bool exists_work_to_do() override final {
		return logs_ready_for_merge;
	}

	void run();

public:
	LogMergeWorker(EdceManagementStructures& management_structures)
		: AsyncWorker()
		, management_structures(management_structures) {
			start_async_thread([this] {run();});
		}
	~LogMergeWorker() {
		wait_for_async_task();
		end_async_thread();
	}

	void do_merge() {//std::vector<SerialAccountModificationLog>&& new_logs) {
		wait_for_async_task();
		std::lock_guard lock(mtx);
	//	logs = std::move(new_logs);
		logs_ready_for_merge = true;
		cv.notify_all();
	}
	void wait_for_merge_finish() {
		wait_for_async_task();
	}
};

}
