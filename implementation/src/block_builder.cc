#include "block_builder.h"

#include "xdr/transaction.h"

#include <chrono>

namespace edce {

void BlockBuilder::run() {
	while(true) {

		current_buffer = tx_buffer_manager.get_full_buffer();
		block_builder_manager.log_tx_buffer_reservation();

		std::lock_guard lock(mtx);

		auto start_time = std::chrono::high_resolution_clock::now();

		int valid_count = 0;

		bool buffer_empty = false;
		while (!buffer_empty) {
			auto& tx = current_buffer->remove(buffer_empty);
			
/*			bool sig_result = sig_check.check(tx);
			if (!sig_result) {
				continue;
			}*/

			TransactionResult result;
			auto status = tx_processor.process_transaction(tx, result);

			if (status != TransactionProcessingStatus::SUCCESS) {
				continue;
			}
			valid_count++;
			valid_txs.push_back(tx);
			results.push_back(result);
		}

		auto end_time = std::chrono::high_resolution_clock::now();
		double elapsed_time = ((double) std::chrono::duration_cast<std::chrono::microseconds>(end_time-start_time).count()) / 1000000;

		std::printf("block builder: cleared buffer in %f\n", elapsed_time);

		block_builder_manager.commit_tx_addition(valid_count);

		tx_buffer_manager.return_empty_buffer(std::move(current_buffer));

	}
}

} /* edce */