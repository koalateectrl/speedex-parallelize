#pragma once
#include "edce_management_structures.h"
#include "edce.h"
#include "tatonnement_oracle.h"
#include "lp_solver.h"
#include "mempool.h"

#include <cstdint>
#include <mutex>

#include "xdr/experiments.h"
#include "xdr/block.h"

namespace edce {

class EdceProducerNode {
	constexpr static size_t PERSIST_BATCH = 5;
	constexpr static size_t MEASUREMENT_PERSIST_FREQUENCY = 100;

	constexpr static size_t TARGET_BLOCK_SIZE = 500000;

	EdceManagementStructures& management_structures;
	TatonnementOracle oracle;
	LPSolver solver;
	Price* prices;

	std::mutex confirmation_mtx;
	std::mutex measurement_mtx;

	HashedBlock prev_block;

	uint64_t highest_confirmed_block;

	EdceAsyncPersister async_persister;

	Mempool mempool;

	ExperimentResults measurement_results;
	std::string measurement_output_prefix;

	const EdceOptions& options;

public:

	EdceProducerNode(
		EdceManagementStructures& management_structures,
		const ExperimentParameters params,
		const EdceOptions& options,
		std::string measurement_output_prefix)
	: management_structures(management_structures)
	, oracle(management_structures.work_unit_manager, 0)
	, solver(management_structures.work_unit_manager)
	, confirmation_mtx()
	, measurement_mtx()
	, prev_block()
	, highest_confirmed_block(0)
	, async_persister(management_structures)
	, mempool()
	, measurement_results()
	, measurement_output_prefix(measurement_output_prefix)
	, options(options) {
		measurement_results.block_results.resize(MEASUREMENT_PERSIST_FREQUENCY);
		measurement_results.params = params;
		auto num_assets = management_structures.work_unit_manager.get_num_assets();
		prices = new Price[num_assets];
		for (size_t i = 0; i < num_assets; i++) {
			prices[i] = PriceUtils::from_double(1.0);
		}
	}

	~EdceProducerNode() {
		write_measurements();
		delete[] prices;
	}


	std::string measurement_filename(uint64_t block_number) {
		return measurement_output_prefix + std::to_string(block_number) +  "." + std::to_string(measurement_results.params.num_threads) + "_production_results";
	}

	std::pair<HashedBlock, AccountModificationBlock> produce_block();

	void add_txs_to_mempool(std::vector<SignedTransaction>&& txs);
	size_t mempool_size() {
		return mempool.size();
	}

	void push_mempool_buffer_to_mempool() {
		mempool.push_mempool_buffer_to_mempool();
	}

	void log_block_confirmation(uint64_t block_number);
	void write_measurements();
};


} /* edce */