#pragma once
#include "edce_management_structures.h"
#include "edce.h"
#include "tatonnement_oracle.h"
#include "lp_solver.h"
#include "mempool.h"
#include "consensus_connection_manager.h"
#include "block_producer.h"

#include <cstdint>
#include <mutex>

#include "xdr/experiments.h"
#include "xdr/block.h"

namespace edce {

class EdceNode {
	constexpr static size_t PERSIST_BATCH = 5;
	constexpr static size_t MEASUREMENT_PERSIST_FREQUENCY = 10000;
	
	EdceManagementStructures& management_structures;

	NodeType state;

	std::mutex confirmation_mtx;
	std::mutex operation_mtx;
	std::mutex measurement_mtx;

	HashedBlock prev_block;

	uint64_t highest_confirmed_block;

	EdceAsyncPersister async_persister;
	
	ExperimentResultsUnion measurement_results;
	std::string measurement_output_prefix;

	const EdceOptions& options;

	ConnectionManager connection_manager;

	constexpr static bool small = false;

	//block production related objects
	constexpr static size_t TARGET_BLOCK_SIZE = small ? 60'000 : 600'000;

	constexpr static size_t MEMPOOL_CHUNK_SIZE = small ? 1'000: 10'000;

	TatonnementManagementStructures tatonnement_structs;
	std::vector<Price> prices;
	Mempool mempool;
	MempoolWorker mempool_worker;
	BlockProducer block_producer;

	//block validation related objects
	//none

	//utility methods
	void set_current_measurements_type();
	BlockDataPersistenceMeasurements& get_persistence_measurements(uint64_t block_number);
	BlockStateUpdateStats& get_state_update_stats(uint64_t block_number);

	void assert_state(NodeType required_state);
	std::string state_to_string(NodeType query_state);

	void self_confirmation(uint64_t block_number);

public:

	EdceNode(
		EdceManagementStructures& management_structures,
		const ExperimentParameters params,
		const EdceOptions& options,
		std::string measurement_output_prefix,
		NodeType state)
	: management_structures(management_structures)
	, state(state)
	, confirmation_mtx()
	, operation_mtx()
	, measurement_mtx()
	, prev_block()
	, highest_confirmed_block(0)
	, async_persister(management_structures)
	, measurement_results()
	, measurement_output_prefix(measurement_output_prefix)
	, options(options) 
	, connection_manager()
	, tatonnement_structs(management_structures)
	//, solver(management_structures.work_unit_manager)
	//, oracle(management_structures.work_unit_manager, solver, 0)
	, mempool(MEMPOOL_CHUNK_SIZE)
	, mempool_worker(mempool)
	, block_producer(management_structures)
	{
		measurement_results.block_results.resize(MEASUREMENT_PERSIST_FREQUENCY);
		measurement_results.params = params;
		auto num_assets = management_structures.work_unit_manager.get_num_assets();
		//prices = new Price[num_assets];
		prices.resize(num_assets);
		for (size_t i = 0; i < num_assets; i++) {
			prices[i] = PriceUtils::from_double(1.0);
		}
	}

	~EdceNode() {
		write_measurements();
		//delete[] prices;
	}

	ExperimentResultsUnion get_measurements() {
		std::lock_guard lock(confirmation_mtx); // for highest_confirmed_block
		std::lock_guard lock2(measurement_mtx);
		ExperimentResultsUnion out;
		out.block_results.insert(out.block_results.end(), measurement_results.block_results.begin(), measurement_results.block_results.begin() + highest_confirmed_block + 1);
		out.params = measurement_results.params;
		return out;
	}

	ConnectionManager& get_connection_manager() {
		return connection_manager;
	}

	std::string overall_measurement_filename() {
		return measurement_output_prefix + "results";
	}

	std::string measurement_filename(uint64_t block_number) {
		return measurement_output_prefix + std::to_string(block_number) +  "." + std::to_string(measurement_results.params.num_threads) + "_results";
	}

	bool produce_block();

	template<typename TxListType>
	bool validate_block(const HashedBlock& header, const std::unique_ptr<TxListType> block);

	void add_txs_to_mempool(std::vector<SignedTransaction>&& txs, uint64_t latest_block_number);
	size_t mempool_size() {
		assert_state(BLOCK_PRODUCER);
		return mempool.size();
	}

	void push_mempool_buffer_to_mempool() {
		assert_state(BLOCK_PRODUCER);
		mempool.push_mempool_buffer_to_mempool();
	}

	void log_block_confirmation(uint64_t block_number);
	void write_measurements();
};


} /* edce */
