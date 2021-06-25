#include "edce_node.h"
#include "utils.h"
#include "simple_debug.h"

namespace edce {




//std::pair<HashedBlock, AccountModificationBlock> 

//returns true if successfully makes block from mempool.
bool
EdceNode::produce_block() {
	auto start_time = init_time_measurement();

	std::lock_guard lock(operation_mtx);

	assert_state(BLOCK_PRODUCER);

	uint64_t prev_block_number = prev_block.block.blockNumber;

	if (prev_block_number % 1 == 0) {
		std::fprintf(stderr, "block %lu\n", prev_block_number);
	}

	HashedBlock new_block;

	std::lock_guard lock2(measurement_mtx);
	
	BLOCK_INFO("Starting production on block %lu", prev_block_number + 1);

	//management_structures.db.log();
	//management_structures.db.values_log();

	set_current_measurements_type();
	auto& current_measurements
		= measurement_results
			.block_results
			.at(prev_block_number % MEASUREMENT_PERSIST_FREQUENCY)
			.productionResults();
	
	auto mempool_push_ts = init_time_measurement();
	mempool.push_mempool_buffer_to_mempool();
	current_measurements.mempool_push_time = measure_time(mempool_push_ts);

	BlockStateUpdateStatsWrapper state_update_stats;
	size_t block_size = 0;

	current_measurements.total_init_time = measure_time_from_basept(start_time);

	BLOCK_INFO("mempool size: %lu", mempool.size());
	{
		auto timestamp = init_time_measurement();
		current_measurements.last_block_added_to_mempool = mempool.latest_block_added_to_mempool.load(std::memory_order_relaxed);

		block_size = block_producer.build_block(mempool, TARGET_BLOCK_SIZE, current_measurements.block_creation_measurements, state_update_stats);

		current_measurements
			.block_creation_measurements
			.block_building_time = measure_time(timestamp);

		current_measurements
			.block_creation_measurements
			.number_of_transactions = block_size;

		BLOCK_INFO("block build time: %lf", current_measurements.block_creation_measurements.block_building_time);
	}

	/*std::thread mempool_cleaning_thread([this, &current_measurements] {
		auto timestamp = init_time_measurement();
		mempool.remove_confirmed_txs();
		mempool.join_small_chunks();
		current_measurements.block_creation_measurements.mempool_clearing_time = measure_time(timestamp);
	});*/
	mempool_worker.do_mempool_cleaning(&current_measurements.block_creation_measurements.mempool_clearing_time);


	current_measurements.total_block_build_time = measure_time_from_basept(start_time);

	uint8_t tax_rate_out = 0;

	edce_block_creation_logic(
		prices.data(),
		management_structures,
		tatonnement_structs,
		options, 
		prev_block.hash,
		prev_block_number,
		current_measurements.block_creation_measurements,
		new_block.block.internalHashes.clearingDetails,
		tax_rate_out, 
		state_update_stats);

	current_measurements.total_block_creation_time = measure_time_from_basept(start_time);

	auto timestamp = init_time_measurement();

	edce_make_state_commitment(
		new_block.block.internalHashes, 
		management_structures,
		current_measurements.production_hashing_measurements,
		options);

	current_measurements.state_commitment_time = measure_time(timestamp);

	edce_format_hashed_block(new_block, prev_block, options, prices.data(), tax_rate_out);

	prev_block = new_block;
	
	current_measurements.format_time = measure_time(timestamp);

	current_measurements.total_block_commitment_time = measure_time_from_basept(start_time);

	auto output_tx_block = edce_persist_critical_round_data(management_structures, prev_block, current_measurements.data_persistence_measurements, true);
	current_measurements.data_persistence_measurements.total_critical_persist_time = measure_time(timestamp);

	current_measurements.total_critical_persist_time = measure_time_from_basept(start_time);

	BLOCK_INFO("finished block production, starting to send to other nodes");
	connection_manager.send_block(prev_block, std::move(output_tx_block));
	BLOCK_INFO("send time: %lf", measure_time(timestamp));
	connection_manager.log_confirmation(prev_block.block.blockNumber);
	BLOCK_INFO("log confirm time: %lf", measure_time(timestamp));

	current_measurements.total_block_send_time = measure_time_from_basept(start_time);

	BLOCK_INFO("done sending to other nodes");
	if (connection_manager.self_confirmable()) {
		self_confirmation(prev_block.block.blockNumber);
	}

	current_measurements.total_self_confirm_time = measure_time_from_basept(start_time);

	auto async_ts = init_time_measurement();
	if (prev_block.block.blockNumber % PERSIST_BATCH == 0) {
		async_persister.do_async_persist(
			prev_block.block.blockNumber, 
			get_persistence_measurements(prev_block.block.blockNumber));
	}
	current_measurements.data_persistence_measurements.async_persist_wait_time = measure_time(async_ts);

	current_measurements.total_block_persist_time = measure_time_from_basept(start_time);

	get_state_update_stats(prev_block.block.blockNumber) = state_update_stats.get_xdr();

	auto mempool_wait_ts = init_time_measurement();

	mempool_worker.wait_for_mempool_cleaning_done();
	current_measurements.mempool_wait_time = measure_time(mempool_wait_ts);
	
	current_measurements.total_time_from_basept = measure_time_from_basept(start_time);

	current_measurements.total_time = measure_time(start_time);

	return block_size > 1000;
	//management_structures.db.values_log();

	//return std::make_pair(prev_block, output_tx_block);
}

template bool EdceNode::validate_block(const HashedBlock& header, std::unique_ptr<SerializedBlock> block);
template bool EdceNode::validate_block(const HashedBlock& header, std::unique_ptr<SignedTransactionList> block);
template bool EdceNode::validate_block(const HashedBlock& header, std::unique_ptr<AccountModificationBlock> block);

template<typename TxListType>
bool EdceNode::validate_block(const HashedBlock& header, std::unique_ptr<TxListType> block) {
	uint64_t prev_block_number = prev_block.block.blockNumber;

	std::lock_guard lock(operation_mtx);

	assert_state(BLOCK_VALIDATOR);

	std::lock_guard lock2(measurement_mtx);

	//management_structures.db.log();
	//management_structures.db.values_log();

	set_current_measurements_type();

	BlockStateUpdateStatsWrapper state_update_stats;
	//prev number is current - 1, so this indexing is ok (first block is 1, measurement location 0)
	auto& current_measurements = measurement_results.block_results.at(prev_block_number % MEASUREMENT_PERSIST_FREQUENCY).validationResults();

	auto timestamp = init_time_measurement();

	auto logic_timestamp = init_time_measurement();
	auto res =  edce_block_validation_logic( 
		management_structures,
		options,
		current_measurements.block_validation_measurements,
		state_update_stats,
		prev_block,
		header,
		*block);

	if (!res) {
		return false;
	}

	current_measurements.validation_logic_time = measure_time(logic_timestamp);
	
	auto persistence_start = init_time_measurement();
	edce_persist_critical_round_data(management_structures, header, current_measurements.data_persistence_measurements, 1000000);
	current_measurements.total_persistence_time = measure_time(persistence_start);

	current_measurements.total_time = measure_time(timestamp);

	prev_block = header;

	connection_manager.send_block(prev_block, std::move(block));
	connection_manager.log_confirmation(prev_block.block.blockNumber);

	if (connection_manager.self_confirmable()) {
		self_confirmation(prev_block.block.blockNumber);
	}

	if (prev_block.block.blockNumber % PERSIST_BATCH == 0) {
		async_persister.do_async_persist(
			prev_block.block.blockNumber, 
			get_persistence_measurements(prev_block.block.blockNumber));
	}

	get_state_update_stats(prev_block.block.blockNumber) = state_update_stats.get_xdr();

	return true;
}


void EdceNode::set_current_measurements_type() {
	measurement_results.block_results.at(prev_block.block.blockNumber % MEASUREMENT_PERSIST_FREQUENCY).type(state);
}

BlockDataPersistenceMeasurements& EdceNode::get_persistence_measurements(uint64_t block_number) {
	switch(state) {
		case NodeType::BLOCK_PRODUCER:
			return measurement_results.block_results.at((block_number - 1) % MEASUREMENT_PERSIST_FREQUENCY)
				.productionResults().data_persistence_measurements;
		case NodeType::BLOCK_VALIDATOR:
			return measurement_results.block_results.at((block_number - 1) % MEASUREMENT_PERSIST_FREQUENCY)
				.validationResults().data_persistence_measurements;
	}
	throw std::runtime_error("invalid state");
}

BlockStateUpdateStats& EdceNode::get_state_update_stats(uint64_t block_number) {
	switch(state) {
		case NodeType::BLOCK_PRODUCER:
			return measurement_results.block_results.at((block_number - 1) % MEASUREMENT_PERSIST_FREQUENCY)
				.productionResults().state_update_stats;
		case NodeType::BLOCK_VALIDATOR:
			return measurement_results.block_results.at((block_number - 1) % MEASUREMENT_PERSIST_FREQUENCY)
				.validationResults().state_update_stats;
	}
	throw std::runtime_error("invalid state");
}

void 
EdceNode::assert_state(NodeType required_state) {
	if (state != required_state) {
		std::string errstr = std::string("Expected ") + state_to_string(required_state) + ", but was in state " + state_to_string(state);
		throw std::runtime_error(errstr);
	}
}

std::string EdceNode::state_to_string(NodeType query_state) {
	switch(query_state) {
		case BLOCK_PRODUCER:
			return "BLOCK_PRODUCER";
		case BLOCK_VALIDATOR:
			return "BLOCK_VALIDATOR";
	}
	throw std::runtime_error("Invalid State!");
}

//REQUIRES HOLDING measurement_mtx
void EdceNode::self_confirmation(uint64_t block_number) {
	std::lock_guard lock(confirmation_mtx);

	highest_confirmed_block = block_number;

	BLOCK_INFO("logging confirmation of block %lu", highest_confirmed_block);

	
	/*if (highest_confirmed_block - async_persister.get_highest_persisted_block() >= PERSIST_BATCH) {

		//std::lock_guard lock2(measurement_mtx);

		async_persister.do_async_persist(
			highest_confirmed_block, 
			get_persistence_measurements(highest_confirmed_block));
	}*/
}

void EdceNode::log_block_confirmation(uint64_t block_number) {
	std::lock_guard lock(confirmation_mtx);

	if (block_number <= highest_confirmed_block) {
		BLOCK_INFO("confirming block that was already confirmed");
		return;
	}

	if (block_number > prev_block.block.blockNumber) {
		BLOCK_INFO("attempting to confirm block we don't have yet!");
		return;
	}

	highest_confirmed_block = block_number;

	BLOCK_INFO("logging confirmation of block %lu", highest_confirmed_block);

/*	if (highest_confirmed_block - async_persister.get_highest_persisted_block() >= PERSIST_BATCH) {

		std::lock_guard lock2(measurement_mtx);
		BLOCK_INFO("doing async persists with block %lu (batch: %lu)", highest_confirmed_block, PERSIST_BATCH);

		async_persister.do_async_persist(
			highest_confirmed_block, 
			get_persistence_measurements(highest_confirmed_block));
	} */
}

void EdceNode::write_measurements() {
	std::lock_guard lock(confirmation_mtx);
	std::lock_guard lock2(measurement_mtx);
	BLOCK_INFO("write measurements called");
	async_persister.wait_for_async_persist();

	auto filename = overall_measurement_filename();

	ExperimentResultsUnion out;
	out.block_results.insert(out.block_results.end(), measurement_results.block_results.begin(), measurement_results.block_results.begin() + highest_confirmed_block + 1);
	out.params = measurement_results.params;
	//auto filename = measurement_filename(prev_block.block.blockNumber);
	if (save_xdr_to_file(out, filename.c_str())) {
		BLOCK_INFO("failed to save measurements file %s", filename.c_str());
	}

	//measurement_results.block_results.clear();
	//measurement_results.block_results.resize(MEASUREMENT_PERSIST_FREQUENCY);
}

void EdceNode::add_txs_to_mempool(std::vector<SignedTransaction>&& txs, uint64_t latest_block_number) {

	assert_state(BLOCK_PRODUCER);	

	for(size_t i = 0; i <= txs.size() / mempool.TARGET_CHUNK_SIZE; i ++) {
		std::vector<SignedTransaction> chunk;
		size_t min_idx = i * mempool.TARGET_CHUNK_SIZE;
		size_t max_idx = std::min(txs.size(), (i + 1) * mempool.TARGET_CHUNK_SIZE);
		chunk.insert(
			chunk.end(),
			std::make_move_iterator(txs.begin() + min_idx),
			std::make_move_iterator(txs.begin() + max_idx));
		mempool.add_to_mempool_buffer(std::move(chunk));
	}
	mempool.latest_block_added_to_mempool.store(latest_block_number, std::memory_order_relaxed);
}


} /* edce */
