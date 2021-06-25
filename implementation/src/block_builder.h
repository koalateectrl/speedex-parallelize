#pragma once

#include "database.h"
#include "merkle_work_unit_manager.h"
#include "transaction_buffer_manager.h"
#include "serial_transaction_processor.h"
#include "block_builder_manager.h"
#include "account_modification_log.h"
#include "edce_management_structures.h"

#include <memory>
#include <mutex>

#include "xdr/transaction.h"

namespace edce {


//processes txs THAT HAVE ALREADY HAD SIGS CHECKED into blocks
class BlockBuilder {

	EdceManagementStructures& management_structures;
	SerialTransactionProcessor<> tx_processor;
	TransactionBufferManager& tx_buffer_manager;
	BlockBuilderManager& block_builder_manager;

	std::mutex mtx;

	TransactionBufferManager::buffer_ptr current_buffer;

	std::vector<SignedTransaction> valid_txs;
	std::vector<TransactionResult> results;

public:

	BlockBuilder(
		EdceManagementStructures& management_structures,
		TransactionBufferManager& tx_buffer_manager,
		BlockBuilderManager& block_builder_manager)
		: management_structures(management_structures),
		tx_processor(management_structures),
		tx_buffer_manager(tx_buffer_manager),
		block_builder_manager(block_builder_manager),
		mtx(),
		current_buffer() {}
  
	void run(); //runs endlessly

	std::pair<std::vector<SignedTransaction>, std::vector<TransactionResult>>
	finish_intermediates() {
		std::lock_guard lock(mtx);

		tx_processor.finish();
		auto tx_out = std::move(valid_txs);
		auto results_out = std::move(results);
		valid_txs.clear();
		results.clear();
		return std::make_pair(tx_out, results_out);
	}
};

} /* edce */