#include "edce_producer_node.h"
#include "utils.h"
#include "block_producer.h"
#include "simple_debug.h"

namespace edce {

std::pair<HashedBlock, AccountModificationBlock> 
EdceProducerNode::produce_block() {
	auto start_time = init_time_measurement();

	uint64_t prev_block_number = prev_block.block.blockNumber;

	HashedBlock new_block;

	std::lock_guard lock(measurement_mtx);
	
	BLOCK_INFO("Starting production on block %lu", prev_block_number + 1);
	auto& current_measurements
		= measurement_results
			.block_results
			.at(prev_block_number % MEASUREMENT_PERSIST_FREQUENCY);
	
	mempool.push_mempool_buffer_to_mempool();
	BLOCK_INFO("mempool size: %lu", mempool.size());
	{
		auto timestamp = init_time_measurement();

		build_block(management_structures, mempool, TARGET_BLOCK_SIZE);

		current_measurements
			.block_creation_measurements
			.block_building_time = measure_time(timestamp);
	}

	edce_block_creation_logic(
		prices,
		management_structures,
		oracle,
		solver,
		options, 
		prev_block.hash,
		prev_block_number,
		current_measurements.block_creation_measurements,
		new_block.block.internalHashes.clearingDetails);

	auto timestamp = init_time_measurement();

	edce_make_state_commitment(
		new_block.block.internalHashes, 
		management_structures,
		current_measurements.production_hashing_measurements,
		options);

	current_measurements.state_commitment_time = measure_time(timestamp);

	edce_format_hashed_block(new_block, prev_block, options, prices);

	prev_block = new_block;
	
	current_measurements.format_time = measure_time(timestamp);

	auto output_tx_block = *edce_persist_critical_round_data(management_structures, prev_block, current_measurements.data_persistence_measurements, true);
	measure_time(timestamp);

	BLOCK_INFO("starting remove_confirmed_txs");
	mempool.remove_confirmed_txs();
	BLOCK_INFO("starting join small chunks");
	mempool.join_small_chunks();
	BLOCK_INFO("done mempool management");

	current_measurements.block_creation_measurements.mempool_clearing_time = measure_time(timestamp);

	current_measurements.total_time = measure_time(start_time);

	return std::make_pair(prev_block, output_tx_block);
}




void EdceProducerNode::log_block_confirmation(uint64_t block_number) {
	std::lock_guard lock(confirmation_mtx);

	if (block_number <= highest_confirmed_block) {
		BLOCK_INFO("confirming block that was already confirmed");
		return;
	}

	highest_confirmed_block = block_number;

	if (highest_confirmed_block - async_persister.get_highest_persisted_block() >= PERSIST_BATCH) {

		std::lock_guard lock2(measurement_mtx);

		async_persister.do_async_persist(
			highest_confirmed_block, 
			measurement_results
				.block_results[highest_confirmed_block % MEASUREMENT_PERSIST_FREQUENCY]
				.data_persistence_measurements);
	}
}

void EdceProducerNode::write_measurements() {
	std::lock_guard lock(measurement_mtx);
	async_persister.wait_for_async_persist();

	auto filename = measurement_filename(prev_block.block.blockNumber);
	save_xdr_to_file(measurement_results, filename.c_str());

	measurement_results.block_results.clear();
	measurement_results.block_results.resize(MEASUREMENT_PERSIST_FREQUENCY);
}

void EdceProducerNode::add_txs_to_mempool(std::vector<SignedTransaction>&& txs) {

	for(size_t i = 0; i <= txs.size() / Mempool::TARGET_CHUNK_SIZE; i ++) {
		std::vector<SignedTransaction> chunk;
		size_t min_idx = i * Mempool::TARGET_CHUNK_SIZE;
		size_t max_idx = std::min(txs.size(), (i + 1) * Mempool::TARGET_CHUNK_SIZE);
		chunk.insert(
			chunk.end(),
			std::make_move_iterator(txs.begin() + min_idx),
			std::make_move_iterator(txs.begin() + max_idx));
		mempool.add_to_mempool_buffer(std::move(chunk));
	}
}


} /* edce */
