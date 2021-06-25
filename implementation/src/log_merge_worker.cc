#include "log_merge_worker.h"

namespace edce {

void
LogMergeWorker::run() {
	std::unique_lock lock(mtx);
	while(true) {
		if ((!done_flag) && (!exists_work_to_do())) {
			cv.wait(lock, [this] () { return done_flag || exists_work_to_do();});
		}
		if (done_flag) return;
		if (logs_ready_for_merge) {

			management_structures.account_modification_log.merge_in_log_batch();
			
			/*if (logs.size() > 0) {
				while(management_structures.account_modification_log.size() == 0 && logs.size() > 0) {
					logs.back().finish();
					logs.pop_back();
				}

				if (logs.size() > 0) {
					management_structures.account_modification_log.merge_in_log_batch(logs);
				}
			}*/
			logs_ready_for_merge = false;
		}
		cv.notify_all();
	}
}
	
}