#include "block_builder_manager.h"

namespace edce {

void BlockBuilderManager::log_tx_buffer_reservation() {
	std::unique_lock lock(reservation_wait_mtx);

	std::printf("reserving a block of transactions\n");

	while ((reserved_tx_count + TransactionBufferManager::BUF_SIZE > MAX_TRANSACTIONS_PER_BLOCK) || !enabled_flag) {
		reservation_wait_cv.wait(lock);
	}

	reserved_tx_count += TransactionBufferManager::BUF_SIZE;
}
void BlockBuilderManager::commit_tx_addition(int num_txs) {
	std::lock_guard lock(reservation_wait_mtx);

	auto freed_amount = TransactionBufferManager::BUF_SIZE - num_txs;
	reserved_tx_count -= freed_amount;

	if ((reserved_tx_count + TransactionBufferManager::BUF_SIZE <= MAX_TRANSACTIONS_PER_BLOCK) && enabled_flag) {
		reservation_wait_cv.notify_all();
	}
}

} /* edce */