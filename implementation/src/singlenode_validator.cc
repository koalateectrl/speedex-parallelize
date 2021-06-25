#include "xdr/experiments.h"
#include "edce.h"
#include "price_utils.h"
#include "utils.h"

#include <xdrpp/marshal.h>
#include <atomic>
#include <thread>

#include "serial_transaction_processor.h"

#include "memory_database_view.h"

#include "edce_management_structures.h"
#include "header_persistence_utils.h"

#include "tbb/global_control.h"

#include "xdr/experiments.h"

#include "singlenode_init.h"
#include "block_update_stats.h"




#include <signal.h>
#include <unistd.h>
#include <stdlib.h>



using namespace edce;



bool run_experiment(const ExperimentParameters& params, std::string experiment_root, const EdceOptions& options, const int thread_count, ExperimentValidationResults& results) {

	EdceManagementStructures management_structures(
		options.num_assets,
		ApproximationParameters {
			.tax_rate = (uint8_t)options.tax_rate,
			.smooth_mult = (uint8_t)options.smooth_mult

		});

	/*management_structures.open_lmdb_env();
	management_structures.open_lmdb();

	auto starting_block = edce_load_persisted_data(management_structures);
	if (starting_block != 0) {
		throw std::runtime_error("invalid start");
	}*/


	uint64_t starting_block = init_management_structures_from_lmdb(management_structures);
//	init_management_structures_no_lmdb(management_structures, params.num_accounts, params.num_assets, 10000000000);

	tbb::global_control control(
		tbb::global_control::max_allowed_parallelism, thread_count);

	HashedBlock prev_block;

	/*std::vector<TransactionData> formatted_data;
	for (unsigned int block = starting_block; block < experiment.blocks.size(); block++) {
		formatted_data.emplace_back();

		for (unsigned int i = 0; i < experiment.blocks[block].txs.size(); i++) {
			formatted_data.back().transactions.emplace_back();
			formatted_data.back().transactions.back().transaction = experiment.blocks[block].txs[i];
		}
	}*/

	if (starting_block > 1) {
		prev_block = load_header(starting_block - 1);
	}

	EdceAsyncPersister persister(management_structures);

	results.block_results.resize(params.num_blocks);


	const size_t BUF_SIZE = 100'000'000;

	unsigned char* buf = new unsigned char[BUF_SIZE];
	for (unsigned int block = starting_block; ; block++) {

		std::printf("Processing block %d\n", block);

		BlockStateUpdateStatsWrapper state_update_stats;

		auto timestamp = init_time_measurement();

		auto load_timestamp = init_time_measurement();
		
		if (!check_if_header_exists(block)) {
			std::printf("couldn't find header, exiting");
			return true;
		}
		const HashedBlock header_block = load_header(block);

		std::printf("loaded header\n");
		if (header_block.block.blockNumber != block) {
			std::printf("invalid block loading: wanted %u got %lu\n", block, header_block.block.blockNumber);

			throw std::runtime_error("invalid block loading");

		}

		//TransactionData data;
		AccountModificationBlock data;

		std::string data_name = tx_block_name(block);//experiment_root + std::to_string(block) + ".txs";
		std::printf("data_name = %s\n", data_name.c_str());
		
		if (load_xdr_from_file(data, data_name.c_str())) {
		//if (load_xdr_from_file_fast(data, data_name.c_str(), buf, BUF_SIZE)) {
			throw std::runtime_error("failed to load data file " + data_name);
		}
		std::printf("starting load\n");

		results.block_results.at(block-1).block_load_time = measure_time(load_timestamp);
		std::printf("load time: %lf\n", results.block_results.at(block-1).block_load_time);


		auto logic_timestamp = init_time_measurement();
		auto res =  edce_block_validation_logic( 
			management_structures,
			options,
			results.block_results.at(block-1).block_validation_measurements,
			state_update_stats,
			prev_block,
			header_block,
			data);
			//experiment.blocks[block]);

		if (!res) {
			delete[] buf;
			return false;
		}

		results.block_results.at(block-1).validation_logic_time = measure_time(logic_timestamp);
		
		auto persistence_start = init_time_measurement();

		edce_persist_critical_round_data(management_structures, header_block, results.block_results.at(block-1).data_persistence_measurements, 1000000);

		prev_block = header_block;

		if ((block) % options.persistence_frequency == 0) {
			std::printf("async persist on max block num %lu\n", prev_block.block.blockNumber);
			persister.do_async_persist(prev_block.block.blockNumber, results.block_results.at(block-1).data_persistence_measurements);
		}

		results.block_results.at(block-1).total_persistence_time = measure_time(persistence_start);
		results.block_results.at(block-1).total_time = measure_time(timestamp);

	}
	std::printf("done validation, waiting for last async persistence\n");
	delete[] buf;
	return true;
}


int main(int argc, char const *argv[])
{
	if (argc != 4) {
		std::printf("usage: ./whatever <data_directory> <results_filename> <num_threads>\n");
		return -1;
	}
	EdceOptions options;
	//options.parse_options(argv[1]);

	ExperimentParameters params;

	std::string experiment_root = std::string(argv[1]) + "/";

	std::string params_filename = experiment_root + "params";

	if (load_xdr_from_file(params, params_filename.c_str())) {
		throw std::runtime_error("failed to load " + params_filename);
	}

	int num_threads = std::stoi(argv[3]);

	ExperimentValidationResults results;

	results.params.tax_rate = params.tax_rate;
	results.params.smooth_mult = params.smooth_mult;
	results.params.num_threads = num_threads;
	results.params.num_assets = params.num_assets;
	results.params.num_accounts = params.num_accounts;
	results.params.persistence_frequency = params.persistence_frequency;

	options.num_assets = params.num_assets;
	options.tax_rate = params.tax_rate;
	options.smooth_mult = params.smooth_mult;
	options.persistence_frequency = params.persistence_frequency;

	std::printf("starting validation run with %d threads\n", num_threads);

	if (run_experiment(params, experiment_root, options, num_threads, results)) {
		std::printf("validation success\n");
		if (save_xdr_to_file(results, argv[2])) {
			std::printf("failed to save results file to %s\n", argv[2]);
			throw std::runtime_error("failed to save results file");
		}
	} else {
		std::printf("validation failure\n");
	}
	return 0;
}
