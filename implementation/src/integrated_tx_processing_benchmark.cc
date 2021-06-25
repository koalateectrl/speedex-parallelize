
#include "edce.h"
#include "edce_options.h"
#include "xdr/types.h"


#include "database.h"
#include "merkle_work_unit_manager.h"
#include "tatonnement_oracle.h"
#include "lp_solver.h"

#include "tbb/global_control.h"

#include "utils.h"

#include <thread>
#include <vector>

#include "tatonnement_sim_setup.h"

#include "xdr/block.h"

#include <chrono>

using namespace edce;

/*
class TxGenThread {

	TatonnementSimSetup<MemoryDatabase>& setup;
	int account_start, account_end, num_assets;
	double& elapsed_time;
	double& finish_time;

	Price* underlying_prices;
public:
	TxGenThread(TatonnementSimSetup<MemoryDatabase>& setup, int num_assets, int account_start, int account_end, Price* underlying_prices, double& elapsed_time, double& finish_time) 
		: setup(setup), num_accounts(num_accounts), num_assets(num_assets), elapsed_time(elapsed_time), finish_time(finish_time), underlying_prices(underlying_prices) {}

	void run(int num_txs, int seed) {

		auto time = init_time_measurement();
		finish_time = setup.create_txs(num_txs, num_accounts, num_assets, underlying_prices, seed, false);
		elapsed_time = measure_time(time);
	}
};*/

void run_simulation(int num_threads, int num_txs_per_thread, int num_accounts, EdceOptions& options, int seed = 0) {
	//MemoryDatabase db;
	//MerkleWorkUnitManager manager(
	//	options.smooth_mult,
	//	options.tax_rate,
	//	options.num_assets);
	EdceManagementStructures management_structures(
		options.num_assets,
		ApproximationParameters {
			options.tax_rate,
			options.smooth_mult
		});


	/*EdceManagementStructures management_structures{
		MemoryDatabase(),
		MerkleWorkUnitManager(
			options.smooth_mult,
			options.tax_rate,
			options.num_assets),
		AccountModificationLog()
	};*/

	//auto& db = management_structures.db;
	//MemoryDatabase db;

	//auto& manager = management_structures.work_unit_manager;

	TatonnementManagementStructures tatonnement(management_structures);

	//LPSolver lp_solver(manager);
	//TatonnementOracle oracle(manager, lp_solver, 0);

	std::vector<double> elapsed_times;
	std::vector<double> finish_times;

	for (int i = 0; i < num_threads; i++) {
		elapsed_times.push_back(-1);
		finish_times.push_back(-1);
	}

	TatonnementSimSetup setup(management_structures);
	
	setup.create_accounts(num_accounts);

	Price* prices = new Price[options.num_assets];

	for (unsigned int i = 0; i < options.num_assets; i++) {
		prices[i] = PriceUtils::from_double(i);
	}

	std::vector<std::thread> threads;
	for (int i = 0; i < num_threads; i++) {

		int account_start_idx = (num_accounts * i) / (num_threads);
		int account_end_idx = (num_accounts * (i+1)) / num_threads;
		threads.push_back(
			std::thread(
				[&setup, &options, &num_accounts, &num_txs_per_thread, &elapsed_times, &finish_times, i, &seed, prices, account_start_idx, account_end_idx] ()
				{

					auto time = init_time_measurement();
					finish_times[i] = setup.create_txs(num_txs_per_thread, account_end_idx, options.num_assets, prices, seed, false, account_start_idx);
					elapsed_times[i] = measure_time(time) - finish_times[i];
					//TxGenThread runner(setup, options.num_assets, num_accounts, prices, elapsed_times[i], finish_times[i]);
					//runner.run(num_txs_per_thread, seed);
				}));
	}

	for (int i = 0; i < num_threads; i++) {
		threads[i].join();
	}

	BlockCreationMeasurements stats;

	for (unsigned int i = 0; i < options.num_assets; i++) {
		prices[i] = PriceUtils::from_double(1);
	}

	tbb::global_control control(
		tbb::global_control::max_allowed_parallelism, num_threads);

	HashedBlock block_out;
	HashedBlock prev_block;

	BlockStateUpdateStatsWrapper state_update_stats;

	uint8_t fee_rate;

	edce_block_creation_logic(prices, management_structures, tatonnement, options, prev_block.hash, prev_block.block.blockNumber, stats, block_out.block.internalHashes.clearingDetails, fee_rate, state_update_stats);

	std::printf("%u\t%u\t%u\t%u\t", options.num_assets, num_accounts, num_txs_per_thread, num_threads);

	//TODO different measurements
	std::printf("%.5f\t%.5f\t%.5f\t%.5f\t", stats.initial_account_db_commit_time, stats.tatonnement_time, stats.lp_time, stats.final_commit_time);

	double avg_elapsed = 0;
	double avg_finish = 0;
	for (int i = 0; i < num_threads; i++) {
		avg_elapsed += elapsed_times[i];
		avg_finish += finish_times[i];
	}

	avg_elapsed /= num_threads;
	avg_finish /= num_threads;

	std::printf("%.5f\t%.5f\t", avg_elapsed, avg_finish);

	for (int i = 0; i < num_threads; i++) {
		std::printf("%.5f\t%.5f\t", elapsed_times[i], finish_times[i]);
	}
	std::printf("\n");
}


void print_col_headers() {
	std::printf("assets\taccts\ttx/thrd\tthrds\tcommit1\tttnmt\tlp\tcommit2\tavg_e\tavg_f\telaps1\tfinish1\t...\n");
}

int main(int argc, char const *argv[])
{
	if (!(argc == 5 || argc == 6)) {
		std::printf("USAGE: ./integrated_tx_processing_benchmark <params_yaml> <num_threads> <num_accounts> <num_txs_per_thread> <optional:print col headers>\n");
		return -1;
	}

	if (argc == 6) {
		print_col_headers();
	}

	EdceOptions options;

	options.parse_options(argv[1]);

	int num_threads = std::stoi(argv[2]);
	int num_accounts = std::stoi(argv[3]);
	int num_txs_per_thread = std::stoi(argv[4]);

	run_simulation(num_threads, num_txs_per_thread, num_accounts, options);
}
