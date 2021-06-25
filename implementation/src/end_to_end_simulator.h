#pragma once

#include <vector>
#include <array>

#include "xdr/types.h"
#include "xdr/transaction.h"

#include "database.h"
#include "merkle_work_unit_manager.h"

#include <atomic>
#include <cstdint>

#include "tx_gen_mock.h"
#include "tatonnement_oracle.h"
#include "lp_solver.h"

#include "edce_management_structures.h"

namespace edce {

class EndToEndSimulator {
	
	EdceManagementStructures& management_structures;

	int tx_per_thread;
	int block_gen_threads;

	int num_accounts;

	std::vector<std::unique_ptr<std::atomic<uint64_t>>> used_sequence_numbers; // keyed by AccountID

	std::vector<TxGenMock> tx_generators;

	TatonnementOracle oracle;

	LPSolver lp_solver;

public:
	EndToEndSimulator(EdceManagementStructures& management_structures, int tx_per_thread, int block_gen_threads, int num_accounts) 
		: management_structures(management_structures),
		tx_per_thread(tx_per_thread), 
		block_gen_threads(block_gen_threads),
		num_accounts(num_accounts),
		used_sequence_numbers(),
		tx_generators(),
		oracle(management_structures.work_unit_manager, 0), // 0 for num tatonnement oracle worker threads, which i haven't yet impl'd
		lp_solver(management_structures.work_unit_manager) {}

	void run_block(Price* price_workspace);

	void init(Price* underlying_prices);
};



}