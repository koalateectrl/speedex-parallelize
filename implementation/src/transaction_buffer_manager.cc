#include "transaction_buffer_manager.h"

namespace edce {

TransactionBufferManager::buffer_ptr TransactionBufferManager::get_empty_buffer() {
	std::lock_guard lock(modify_mtx);
	if (empty_buffers.empty()) {
		return std::make_unique<buffer_type>(); 
	}
	auto output = std::move(empty_buffers.back());
	empty_buffers.pop_back();
	return output;
}
void TransactionBufferManager::return_empty_buffer(buffer_ptr ptr) {
	std::lock_guard lock(modify_mtx);
	if (empty_buffers.size() < MAX_NUM_BUFFERS) {
		empty_buffers.emplace_back(std::move(ptr));
	} 
	//TODO instead of deleting ptr, we could keep track of the total global number of buffers issued.
	//if we don't track that, we have to let some get deleted bc otherwise inflow could back up.
}
TransactionBufferManager::buffer_ptr TransactionBufferManager::get_full_buffer() {
	std::unique_lock lock(modify_mtx);
	if (full_buffers.size() > 0) {
		auto output = std::move(full_buffers.back());
		full_buffers.pop_back();
		cv_wait_full_get.notify_all();
		return output;
	}
	lock.unlock();

	while(true) {
		std::unique_lock lock_wait(full_buffer_get_wait_mtx);
		cv_wait_full_return.wait(lock_wait);
		if (full_buffers.size()) {
			auto output = std::move(full_buffers.back());
			full_buffers.pop_back();
			cv_wait_full_get.notify_all();
			return output;
		}
	}

}
void TransactionBufferManager::return_full_buffer(buffer_ptr ptr) {
	std::unique_lock lock(modify_mtx);
	if (full_buffers.size() < MAX_NUM_BUFFERS) {
		full_buffers.emplace_back(std::move(ptr));
		cv_wait_full_return.notify_all();
		return;
	}
	lock.unlock();

	while(true) {
		std::unique_lock lock_wait(full_buffer_return_wait_mtx);
		cv_wait_full_get.wait(lock_wait);
		if (full_buffers.size() < MAX_NUM_BUFFERS) {
			full_buffers.emplace_back(std::move(ptr));
			cv_wait_full_return.notify_all();
			return;
		}
	}
}


} /* edce */