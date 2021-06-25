#include "file_prealloc_worker.h"
#include "utils.h"

namespace edce {

void 
FilePreallocWorker::run() {

	while (true) {
		std::unique_lock lock(mtx);
		if ((!done_flag) && (!exists_work_to_do())) {
			cv.wait(lock, [this] () {return done_flag || exists_work_to_do();});
		}
		//if ((!next_alloc_block) && (!end_flag)) {
		//	cv.wait(lock, [this] { return ((bool) next_alloc_block) || end_flag;});
		//}
		if (done_flag) return;
		prealloc(*next_alloc_block);
		next_alloc_block = std::nullopt;
		cv.notify_all();
	}
}

void 
FilePreallocWorker::prealloc(uint64_t block_number) {
	auto filename = tx_block_name(block_number);
	std::printf("preallocating file for block %lu filename %s\n", block_number, filename.c_str());
	block_fd = preallocate_file(filename.c_str());
}


} /* edce */