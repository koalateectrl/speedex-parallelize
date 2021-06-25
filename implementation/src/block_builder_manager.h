#pragma once

#include "transaction_buffer_manager.h"
#include "xdr/block.h"
#include <cstdint>
namespace edce {

class BlockBuilderManager {

	uint32_t reserved_tx_count;

	std::mutex reservation_wait_mtx;
	std::condition_variable reservation_wait_cv;

	bool enabled_flag;

	constexpr static int BUF_SIZE = TransactionBufferManager::BUF_SIZE;

public:

	BlockBuilderManager() 
		: reserved_tx_count(0),
		reservation_wait_mtx(),
		reservation_wait_cv(),
		enabled_flag(true) {}


	void log_tx_buffer_reservation();
	void commit_tx_addition(int num_txs);
	
	void disable_block_building() {
		std::lock_guard lock(reservation_wait_mtx);
		enabled_flag = false;
	}
	void enable_block_building() {
		std::lock_guard lock(reservation_wait_mtx);
		enabled_flag = true;
		reservation_wait_cv.notify_all();
	}
};

}