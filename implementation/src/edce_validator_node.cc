#include "edce_validator_node.h"

namespace edce {

bool EdceValidatorNode::validate_block(HashedBlock& header, AccountModificationBlock& block) {
	std::lock_guard lock(measurement_mtx);
	uint64_t prev_block_number = prev_block.block.blockNumber;

	auto& current_measurements = measurement_results.block_results.at(prev_block_number % MEASUREMENT_PERSIST_FREQUENCY);

	auto timestamp = init_time_measurement();

	auto logic_timestamp = init_time_measurement();
	auto res =  edce_block_validation_logic( 
		management_structures,
		options,
		current_measurements.block_validation_measurements,
		prev_block,
		header,
		block);
		//experiment.blocks[block]);

	if (!res) {
		return false;
	}

	current_measurements.validation_logic_time = measure_time(logic_timestamp);
	
	auto persistence_start = init_time_measurement();
	edce_persist_critical_round_data(management_structures, header, current_measurements.data_persistence_measurements, 1000000);
	current_measurements.total_persistence_time = measure_time(persistence_start);

	current_measurements.total_time = measure_time(timestamp);

	return true;
}

void EdceValidatorNode::log_block_confirmation(uint64_t block_number) {
	std::lock_guard lock(confirmation_mtx);

	if (block_number <= highest_confirmed_block) {
		BLOCK_INFO("confirming block that was already confirmed");
		return;
	}

	highest_confirmed_block = block_number;

	if (highest_confirmed_block - async_persister.get_highest_persisted_block() >= PERSIST_BATCH) {

		std::lock_guard lock2 (measurement_mtx);
		auto& current_measurements = measurement_results.block_results.at(highest_confirmed_block % MEASUREMENT_PERSIST_FREQUENCY);
		async_persister.do_async_persist(
			highest_confirmed_block, 
			current_measurements
				.data_persistence_measurements);

	}
}

void EdceValidatorNode::write_measurements() {
	std::lock_guard lock(measurement_mtx);
	async_persister.wait_for_async_persist();

	auto filename = measurement_filename(prev_block.block.blockNumber);
	save_xdr_to_file(measurement_results, filename.c_str());

	measurement_results.block_results.clear();
	measurement_results.block_results.resize(MEASUREMENT_PERSIST_FREQUENCY);
}



} /* edce */