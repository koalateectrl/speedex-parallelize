
#include "edce.h"
#include "tatonnement_sim_setup.h"
#include "xdr/experiments.h"
#include "price_utils.h"
#include "utils.h"

#include <cstdint>

#include <thread>
#include <atomic>

using namespace edce;

void timeout_thread(TatonnementOracle& oracle, size_t num_seconds, std::atomic<bool>& cancel_timeout_thread, bool&timeout_flag) {

	for (size_t i = 0; i < num_seconds; i++) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		if (cancel_timeout_thread) {
			return;
		}
	}
	timeout_flag = oracle.signal_grid_search_timeout();
}

void run_experiment(int num_txs, const ExperimentParameters& params, const ExperimentBlock& data, PriceComputationSingleExperiment& experiment_results) {
	std::printf("starting experiment with %lu txs", num_txs);

	ApproximationParameters approx_params;
	approx_params.smooth_mult = params.smooth_mult;
	approx_params.tax_rate = params.tax_rate;
	EdceManagementStructures management_structures(
		params.num_assets,
		approx_params);
	/*	MemoryDatabase(),
		MerkleWorkUnitManager(
			params.smooth_mult,
			params.tax_rate,
			params.num_assets),
		AccountModificationLog()
	};*/

	TatonnementSimSetup setup(management_structures);

	std::printf("creating accounts!\n");
	setup.create_accounts(params.num_accounts);
	std::printf("setting balances\n");
	setup.set_all_account_balances(params.num_accounts, params.num_assets, 1'000'000'000'000);
	
	std::printf("loading txs\n");
	setup.load_synthetic_txs(data, num_txs);
	Price prices[params.num_assets];

	std::printf("made setup\n");
	for (size_t i = 0; i < params.num_assets; i++) {
		prices[i] = PriceUtils::from_double(1);
	}

	auto& manager = management_structures.work_unit_manager;

	LPSolver solver(manager);

	TatonnementOracle oracle(manager, solver, 0);

	bool timeout_flag = false;
	std::atomic<bool> cancel_timeout_thread = false;


	//auto timestamp = init_time_measurement();


	std::thread th([&oracle, &timeout_flag, &cancel_timeout_thread] () {
		timeout_thread(oracle, 10, cancel_timeout_thread, timeout_flag);
	});

	auto res = oracle.compute_prices_grid_search(prices, approx_params);

	std::printf("done experiment, cancelling timeout thread\n");

	cancel_timeout_thread = true;

	//float res = measure_time(timestamp);
	
	std::printf("duration: %lf\n", res.runtime);

	th.join();

	if (res.num_rounds > 0) {
		std::printf("time per round: %lf\n", res.runtime /res.num_rounds);
	}

	if (timeout_flag) {
		std::printf("timeout happened!");
	} else {
		experiment_results.results.push_back(res);
	}

}

int main(int argc, char const *argv[])
{
	if (argc < 3 || argc > 5) {
		std::printf("usage: <blah> data_directory outfile <tax_rate> <smooth_mult>\n");
		return -1;
	}

	ExperimentParameters params;

	std::string experiment_root = std::string(argv[1]) + "/";
	std::string params_filename = experiment_root + "params";
	load_xdr_from_file(params, params_filename.c_str());
	

	if (argc >= 4) {
		params.tax_rate = std::stoi(argv[3]);
	}
	if (argc >= 5) {
		params.smooth_mult = std::stoi(argv[4]);
	}

	std::vector<size_t> num_tx_list = {500, 1'000, 2'000, 5'000, 10'000, 50'000, 100'000, 500'000, 1'000'000, 5'000'000, 10'000'000, 20'000'000};


	PriceComputationExperiment results;
	results.experiments.resize(num_tx_list.size());

	size_t num_trials = 5;

	for (size_t i = 0; i < num_tx_list.size(); i++) {
		results.experiments[i].num_assets = params.num_assets;
		results.experiments[i].tax_rate = params.tax_rate;
		results.experiments[i].smooth_mult = params.smooth_mult;
		results.experiments[i].num_txs = num_tx_list[i];
		results.experiments[i].num_trials = num_trials;
	}

	size_t read_idx = 1;

	std::printf("starting trials\n");
	for (size_t trial = 1; trial <= num_trials; trial++) {
		ExperimentBlock txs;
		std::printf("starting trial %lu\n", trial);
		size_t target_txs = num_tx_list.back();
		while (txs.size() < target_txs) {
			std::string filename = experiment_root + std::to_string(read_idx) + std::string(".txs");
			ExperimentBlock load_txs;
			load_xdr_from_file(load_txs, filename.c_str());
			std::printf("%lu %s\n", load_txs.size(), filename.c_str());
			txs.insert(txs.end(), load_txs.begin(), load_txs.end());
			read_idx ++;
			std::printf("loading up to %lu of %lu\n", target_txs, txs.size());
		}


		for (size_t i = 0; i < num_tx_list.size(); i++) {
			auto num_txs = num_tx_list[i];
			std::printf("running experiment: trial %lu size %lu tax_rate %lu smooth_mult %lu\n", trial, num_txs, params.tax_rate, params.smooth_mult);
			run_experiment(num_txs, params, txs, results.experiments[i]);
		}

	}

	save_xdr_to_file(results, argv[2]);
	return 0;
}
