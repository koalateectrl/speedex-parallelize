#pragma once

#include "database.h"
#include "merkle_work_unit_manager.h"
#include "serial_transaction_processor.h"

#include <atomic>
#include <cstdint>
#include <vector>
#include <unordered_map>


#include "edce_management_structures.h"
#include "xdr/transaction.h"
#include "xdr/types.h"
#include "price_utils.h"
#include <random>
#include <cstdint>
#include <cstdio>

#include "tx_type_utils.h"

namespace edce {

class TxGenMock {
	int num_assets;
	int num_agents;
	Price* underlying_prices;
	EdceManagementStructures& management_structures;
	SerialTransactionProcessor<> tx_processor;
	std::vector<std::unique_ptr<std::atomic<uint64_t>>>& used_sequence_numbers;
	std::mt19937 gen;

public:

	TxGenMock(
		int num_assets, 
		int num_agents, 
		Price* underlying_prices, 
		EdceManagementStructures& management_structures,
		std::vector<std::unique_ptr<std::atomic<uint64_t>>>& used_sequence_numbers,
		int random_seed)
		: num_assets(num_assets), 
		num_agents(num_agents), 
		underlying_prices(underlying_prices),
		management_structures(management_structures),
		tx_processor(management_structures), 
		used_sequence_numbers(used_sequence_numbers),
		gen(random_seed) {}

	std::vector<Transaction> stream_transactions(int num_transactions);

};

}