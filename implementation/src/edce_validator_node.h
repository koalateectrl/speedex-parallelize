#pragma once

#include "edce_management_structures.h"
#include "edce.h"
#include "xdr/block.h"
#include "xdr/experiments.h"
#include "edce_options.h"

#include <mutex>
#include <cstdint>

namespace edce {

class EdceValidatorNode {
	constexpr static size_t PERSIST_BATCH = 5;
	constexpr static size_t MEASUREMENT_PERSIST_FREQUENCY = 100;

	EdceManagementStructures& management_structures;

	std::mutex confirmation_mtx;
	std::mutex measurement_mtx;

	HashedBlock prev_block;

	uint64_t highest_confirmed_block;

	EdceAsyncPersister async_persister;

	ExperimentValidationResults measurement_results;
	std::string measurement_output_prefix;

	const EdceOptions& options;
	


public:

	EdceValidatorNode(
		EdceManagementStructures& management_structures,
		const ExperimentParameters params,
		const EdceOptions& options,
		std::string measurement_output_prefix)
	: management_structures(management_structures)
	, confirmation_mtx()
	, measurement_mtx()
	, prev_block()
	, highest_confirmed_block(0)
	, async_persister(management_structures)
	, measurement_results()
	, measurement_output_prefix(measurement_output_prefix)
	, options(options) {
		measurement_results.block_results.resize(MEASUREMENT_PERSIST_FREQUENCY);
		measurement_results.params = params;
	}

	~EdceValidatorNode() {
		write_measurements();
	}

	std::string measurement_filename(uint64_t block_number) {
		return measurement_output_prefix + std::to_string(block_number) +  "." + std::to_string(measurement_results.params.num_threads) + "_validation_results";
	}

	bool validate_block(HashedBlock& header, AccountModificationBlock& block);

	void log_block_confirmation(uint64_t block_number);
	void write_measurements();

};



} /* edce */