#pragma once

#include "async_worker.h"
#include "../config.h"
#include "cleanup.h"

namespace edce {


static inline 
std::string tx_block_name(uint64_t block_number) {
	return std::string(ROOT_DB_DIRECTORY) + std::string(TX_BLOCK_DB) + std::to_string(block_number) + std::string(".block");
}


class FilePreallocWorker : public AsyncWorker {
	using AsyncWorker::mtx;
	using AsyncWorker::cv;

	unique_fd block_fd;
	
	std::optional<uint64_t> next_alloc_block;

	bool exists_work_to_do() override final {
		return (bool)next_alloc_block;
	}

	void prealloc(uint64_t block_number);

public:

	FilePreallocWorker()
		: AsyncWorker()
		, block_fd()
		, next_alloc_block(std::nullopt) {
			start_async_thread([this] {run();});
		}


	~FilePreallocWorker() {
		wait_for_async_task();
		end_async_thread();
	}

	void run();

	void call_prealloc(uint64_t next_block) {
		wait_for_async_task();
		std::lock_guard lock(mtx);
		next_alloc_block = next_block;
		cv.notify_all();
	}

	unique_fd& wait_for_prealloc() {
		wait_for_async_task();
		return block_fd;
		/*std::unique_lock lock(mtx);
		if (block_fd) {
			return;
		}
		cv.wait(lock, [this] { return (bool)block_fd;} );*/
	}

	void cancel_prealloc() {
		wait_for_async_task();
		std::lock_guard lock(mtx);
		block_fd.clear();
	}

	/*void terminate() {
		std::unique_lock lock(mtx);
		end_flag = true;
		cv.notify_one();
	}*/
};

} /* edce */