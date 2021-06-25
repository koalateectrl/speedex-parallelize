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

#include "tbb/global_control.h"

#include "work_unit_state_commitment.h" 

#include "singlenode_init.h"
#include "tbb/task_arena.h"

#include <signal.h>
#include <unistd.h>
#include <execinfo.h>
#include <stdlib.h>

using namespace edce;

/*
template<typename BufferedViewT>
bool tx_processor_thread(EdceManagementStructures& management_structures, std::atomic<uint64_t>& idx, const ExperimentBlock& block, TxProcessingMeasurements& measurements) {
	const static int PROCESSING_BUF_SIZE = 10000;
	SerialTransactionProcessor<BufferedViewT> tx_processor(management_structures);

	auto timestamp = init_time_measurement();
	while(true) {
		auto cur_idx = idx.fetch_add(PROCESSING_BUF_SIZE, std::memory_order_relaxed);
		//std::printf("%lu\n", cur_idx);
		for (unsigned int i = 0; i < PROCESSING_BUF_SIZE; i++) {
			if (cur_idx + i >= block.size()) {
				//std::printf("starting finish\n");
				measurements.process_time = measure_time(timestamp);
				tx_processor.finish();
				measurements.finish_time = measure_time(timestamp);
				//std::printf("done finish\n");
				return true;
			}

			//SignedTransaction signed_tx_wrapper;
			//signed_tx_wrapper.transaction = block[cur_idx + i];
			auto status = tx_processor.process_transaction(block.at(cur_idx + i));
			if (status != TransactionProcessingStatus::SUCCESS) {
				return false;
			}
		}
	}
}*/

template<typename BufferedViewT>
bool experiment_inner_loop(
	const ExperimentBlock& block, 
	const EdceOptions& options, 
	const int thread_count,
	Price* prices,
	TatonnementManagementStructures& tatonnement_structs,
	EdceManagementStructures& management_structures, 
	BlockStateUpdateStatsWrapper& state_update_stats,
	ExperimentResults& results, 
	HashedBlock& prev_block) {
	
	//std::vector<std::thread> threads;

	std::atomic<uint64_t> counter = 0;

//	management_structures.account_modification_log.sanity_check();
	if (management_structures.account_modification_log.size() != 0) {
		throw std::runtime_error("forgot to clear mod log!");
	}

	auto timestamp = init_time_measurement();

	std::atomic_flag failure;
	failure.clear();
	std::mutex state_update_stats_mtx;

	std::printf("foooo\n");

	tbb::parallel_for(tbb::blocked_range<std::size_t>(0, block.size(), 10000),
		[&management_structures, &counter, &failure, &block, &state_update_stats_mtx, &state_update_stats] (auto r) {

			BlockStateUpdateStatsWrapper stats_local;
			SerialTransactionProcessor<BufferedViewT> tx_processor(management_structures);
		//	std::printf("%d\n", tbb::this_task_arena::current_thread_index());
			SerialAccountModificationLog serial_log(management_structures.account_modification_log);
			
			for (auto idx = r.begin(); idx < r.end(); idx++) {
				auto status = tx_processor.process_transaction(block.at(idx), stats_local, serial_log);
				if (status != TransactionProcessingStatus::SUCCESS) {
					failure.test_and_set();
					return;
				}
			}
			tx_processor.finish();

			std::lock_guard lock(state_update_stats_mtx);
			state_update_stats += stats_local;
		});

	management_structures.account_modification_log.merge_in_log_batch();

	/*for (int i = 0; i < thread_count; i++) {
		results.block_results.back().processing_measurements.emplace_back();
	}

	for (int i = 1; i < thread_count; i++) {
		threads.emplace_back([&management_structures, &counter, &failure, &block, &results, i] () {
			auto status = tx_processor_thread<BufferedViewT>(management_structures, counter, block, results.block_results.back().processing_measurements.at(i));
			if (!status) {
				failure.test_and_set();
			};
		});
	}
	auto status = tx_processor_thread<BufferedViewT>(management_structures, counter, block, results.block_results.back().processing_measurements[0]);
	std::printf("primary thread done processing\n");
	if (!status) {
		std::printf("got bad status from primary tx processing thread\n");
		return false;
	}
	for (int i = 1; i < thread_count; i++) {
		threads.at(i-1).join();
	}*/
	if (failure.test_and_set()) {
		std::printf("got bad status from a tx processing thread\n");
		return false;
	}

	HashedBlock new_block;

	std::printf("starting block creation logic\n");

	uint64_t prev_block_number = prev_block.block.blockNumber;

	results.block_results.at(prev_block_number).block_creation_measurements.block_building_time = measure_time(timestamp);


	uint8_t fee_rate;

	edce_block_creation_logic(
		prices, 
		management_structures, 
		tatonnement_structs, 
		options, 
		prev_block.hash, 
		prev_block.block.blockNumber, 
		results.block_results.at(prev_block_number).block_creation_measurements,
		new_block.block.internalHashes.clearingDetails,
		fee_rate,
		state_update_stats);

	std::printf("finished block creation logic\n");

	measure_time(timestamp);

	auto checker = WorkUnitStateCommitmentChecker(new_block.block.internalHashes.clearingDetails, std::vector<Price>(), 0);

	edce_make_state_commitment(new_block.block.internalHashes, management_structures, results.block_results.at(prev_block_number).production_hashing_measurements, options);


	results.block_results.at(prev_block_number).state_commitment_time = measure_time(timestamp);

	edce_format_hashed_block(new_block, prev_block, options, prices, fee_rate);

	prev_block = new_block;

	results.block_results.at(prev_block_number).format_time = measure_time(timestamp);

	//prev_block.block.block_number is current block bc of previous line prev_block = new_block
	edce_persist_critical_round_data(management_structures, prev_block, results.block_results.at(prev_block_number).data_persistence_measurements);

	return true;
}

/*

bool run_experiment(const Experiment& experiment, const EdceOptions& options, const int thread_count, ExperimentResults& results) {

	EdceManagementStructures management_structures{
		MemoryDatabase(),
		MerkleWorkUnitManager(
			options.smooth_mult,
			options.tax_rate,
			experiment.num_assets)
	};

	auto& db = management_structures.db;
	auto& manager = management_structures.work_unit_manager;

	for (uint64_t i = 0; i < experiment.num_accounts; i++) {
		db.add_account_to_db(i);
	}
	db.commit();

	std::printf("Finished Account Creation\n");

	TatonnementOracle oracle(manager, 0);
	LPSolver solver(manager);

	Price prices[options.num_assets];

	for (unsigned int i = 0; i < options.num_assets; i++) {
		prices[i] = PriceUtils::from_double(1);
	}

	results.params.tax_rate = options.tax_rate;
	results.params.smooth_mult = options.smooth_mult;
	results.params.num_threads = thread_count;

	//FrozenDataCache frozen_cache;
	HashedBlock prev_block;

//	std::printf("starting lmdb open\n");

	management_structures.open_lmdb_env();
	management_structures.create_lmdb();
//	std::printf("done lmdb open\n");
	//AccountLMDB account_lmdb_instance;
	//account_lmdb_instance.open("account_databases");
	//account_lmdb_instance.create_db("account_lmdb");

	//manager.create_lmdb_instances();
	//manager.open_lmdb();

//	std::printf("threadcount = %d\n", thread_count);

	tbb::global_control control(
		tbb::global_control::max_allowed_parallelism, thread_count);

	for (unsigned int block = 0; block < experiment.blocks.size(); block++) {

		std::printf("Processing block %d\n", block);

		auto res = experiment_inner_loop<UnlimitedMoneyBufferedMemoryDatabaseView>(experiment.blocks[block], options, thread_count, prices, oracle, solver, management_structures, results, prev_block);
		if (!res) {
			return false;
		}
	}
	return true;
} */

bool run_experiment_using_block0(const ExperimentParameters& params, std::string experiment_root, const EdceOptions& options, const int thread_count, ExperimentResults& results) {

	EdceManagementStructures management_structures(
		options.num_assets,
		ApproximationParameters {
			.tax_rate = (uint8_t) options.tax_rate,
			.smooth_mult = (uint8_t) options.smooth_mult

		});
		//MemoryDatabase(),
		//MerkleWorkUnitManager(
		//	options.smooth_mult,
		//	options.tax_rate,
		//	options.num_assets)
	//};

	tbb::global_control control(
		tbb::global_control::max_allowed_parallelism, thread_count);

	init_management_structures_from_lmdb(management_structures);

	//init_management_structures_no_lmdb(management_structures, params.num_accounts, params.num_assets, 10000000000);

	/*management_structures.open_lmdb_env();
	management_structures.open_lmdb();
	auto start_blk = edce_load_persisted_data(management_structures);

	if (start_blk != 0) {
		throw std::runtime_error("invalid start to experiment");
	}*/


	TatonnementManagementStructures tatonnement_structs(management_structures);

	//LPSolver solver(manager);
	//TatonnementOracle oracle(manager, solver, 0);

	Price prices[options.num_assets];

	for (unsigned int i = 0; i < options.num_assets; i++) {
		prices[i] = PriceUtils::from_double(1);
	}


	if (management_structures.db.get_persisted_round_number() != 0 || 
		management_structures.work_unit_manager.get_max_persisted_round_number() != 0 || 
		management_structures.block_header_hash_map.get_persisted_round_number() != 0) {
		throw std::runtime_error("we expected to start at block 0 for this experiment");
	}

	HashedBlock prev_block;

	EdceAsyncPersister persister(management_structures);

	results.block_results.resize(params.num_blocks);

	const size_t BUF_SIZE = 100'000'000;

	unsigned char* buffer = new unsigned char[BUF_SIZE];

	for (unsigned int block = 1; block <= params.num_blocks; block++) {

		std::printf("Processing block %d\n", block);

		auto timestamp = init_time_measurement();
		ExperimentBlock tx_block;

		BlockStateUpdateStatsWrapper state_update_stats;

		std::string filename = experiment_root + std::to_string(block) + std::string(".txs");
		if (load_xdr_from_file_fast(tx_block, filename.c_str(), buffer, BUF_SIZE)) {
			std::printf("failed to load file %s\n", filename.c_str());
			throw std::runtime_error("failed to load file");
		}
		std::printf("done loading file\n");

		auto res = experiment_inner_loop<BufferedMemoryDatabaseView>(
			tx_block, options, thread_count, prices, tatonnement_structs, management_structures, state_update_stats, results, prev_block);
		if (!res) {
			std::printf("production failure\n");
			return false;
		}

		std::printf("options.persistence_frequency = %lu\n", options.persistence_frequency);
		if ((block) % options.persistence_frequency == 0) {
			persister.do_async_persist(prev_block.block.blockNumber, results.block_results.at(block-1).data_persistence_measurements);
		//	edce_persist_async(management_structures, prev_block, results.block_results.back().data_persistence_measurements);
		}
		results.block_results.at(block-1).total_time = measure_time(timestamp);
		results.block_results.at(block-1).state_update_stats = state_update_stats.get_xdr();
	}
	delete[] buffer;
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
		std::printf("failed to load file %s\n", params_filename.c_str());
		throw std::runtime_error("failed to load file");
	}

	int num_threads = std::stoi(argv[3]);

	ExperimentResults results;

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

	std::printf("starting experiment run\n");

	if (run_experiment_using_block0(params, experiment_root, options, num_threads, results)) {
		if (save_xdr_to_file(results, argv[2])) {
			std::printf("failed to save file %s\n", argv[2]);
			throw std::runtime_error("failed to save file");
		}
	}
	return 0;
}
