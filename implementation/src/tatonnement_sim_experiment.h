#pragma once

#include <cstdint>
#include <cstddef>

#include <thread>
#include <atomic>

#include "edce_management_structures.h"
#include "tatonnement_oracle.h"
#include "lp_solver.h"
#include "utils.h"
#include "tatonnement_sim_setup.h"
#include "xdr/experiments.h"
#include "approximation_parameters.h"

namespace edce {

class TatonnementSimExperiment {

	EdceManagementStructures& management_structures;

	size_t num_assets;

	TatonnementManagementStructures tatonnement;

	TatonnementSimSetup setup;

	std::string data_root; // includes trailing /, or is empty

	void timeout_thread(size_t num_seconds, std::atomic<bool>& cancel_timeout_thread, bool&timeout_flag);

	std::optional<TatonnementMeasurements>
	run_current_trial(std::vector<Price> prices);

	ApproximationParameters current_approx_params;


public:

	std::string get_experiment_filename(uint8_t smooth_mult, uint8_t tax_rate) {
		return data_root + std::to_string(tax_rate) + "_" + std::to_string(smooth_mult) + "_results";
	}

	bool check_preexists(uint8_t smooth_mult, uint8_t tax_rate) {
		auto name = get_experiment_filename(smooth_mult, tax_rate);

		if (FILE *file = fopen(name.c_str(), "r")) {
	        fclose(file);
	        return true;
	    } else {
	        return false;
	    }   
	}

	void save_file(uint8_t smooth_mult, uint8_t tax_rate, PriceComputationExperiment& experiment);

	TatonnementSimExperiment(EdceManagementStructures& management_structures, std::string data_root, uint64_t num_assets, uint64_t num_accounts)
		: management_structures(management_structures)
		, num_assets(num_assets)
		, tatonnement(management_structures)
		, setup(management_structures)
		, data_root(data_root) {
			setup.create_accounts(num_accounts);
			setup.set_all_account_balances(num_accounts, num_assets, 1'000'000'000'000'000);
			mkdir_safe(data_root.c_str());
		}

	void run_experiment(
		uint8_t smooth_mult, 
		uint8_t tax_rate, 
		const std::vector<size_t>& num_txs_to_try, 
		const std::vector<ExperimentBlock>& trials,
		std::vector<Price> prices = {}) {
		// we don't care about inadvertently modifying db when running tatonnement_sim_setup, so don't bother clearing db
		

		PriceComputationExperiment results;
		results.experiments.resize(num_txs_to_try.size());

		size_t num_trials = trials.size();

		for (size_t i = 0; i < num_txs_to_try.size(); i++) {
			results.experiments[i].num_assets = num_assets;
			results.experiments[i].tax_rate = tax_rate;
			results.experiments[i].smooth_mult = smooth_mult;
			results.experiments[i].num_txs = num_txs_to_try[i];
			results.experiments[i].num_trials = num_trials;
		}

		for (const auto& data : trials) {
			for (size_t i = 0; i < num_txs_to_try.size(); i++) {
				auto num_txs = num_txs_to_try[i];
				std::printf("running trial with %lu txs\n", num_txs);
				if (data.size() < num_txs) {
					throw std::runtime_error("not enough txs!");
				}
				management_structures.work_unit_manager.clear_();
				current_approx_params.tax_rate = tax_rate;
				current_approx_params.smooth_mult = smooth_mult;
				//management_structures.work_unit_manager.change_approximation_parameters_(smooth_mult, tax_rate);
				setup.load_synthetic_txs(data, num_txs);
				auto current_results = run_current_trial(prices);

				if (current_results) {
					results.experiments[i].results.push_back(*current_results);
				}
			}
		}

		auto filename = get_experiment_filename(smooth_mult, tax_rate);
		if (save_xdr_to_file(results, filename.c_str())) {
			throw std::runtime_error("couldn't save results file to disk!");
		}
	}
};

} /* edce */
