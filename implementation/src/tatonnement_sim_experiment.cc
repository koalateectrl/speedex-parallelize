#include "tatonnement_sim_experiment.h"

#include "price_utils.h"

namespace edce {

void 
TatonnementSimExperiment::timeout_thread(size_t num_seconds, std::atomic<bool>& cancel_timeout_thread, bool&timeout_flag) {

	for (size_t i = 0; i < num_seconds; i++) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		if (cancel_timeout_thread) {
			return;
		}
	}
	timeout_flag = tatonnement.oracle.signal_grid_search_timeout();
}

void 
TatonnementSimExperiment::save_file(uint8_t smooth_mult, uint8_t tax_rate, PriceComputationExperiment& experiment) {
	auto name = get_experiment_filename(smooth_mult, tax_rate);
	if (save_xdr_to_file(experiment, name.c_str())) {
		throw std::runtime_error("failed to save results file");
	}
}


void init_price_vec(Price* prices, size_t num_assets) {
	for (size_t i = 0; i < num_assets; i++) {
		prices[i] = PriceUtils::from_double(1);
	}
}

std::optional<TatonnementMeasurements>
TatonnementSimExperiment::run_current_trial(std::vector<Price> prices) {

	//Price prices[num_assets];
	if (prices.size() == 0) {
		prices.resize(num_assets, PriceUtils::from_double(1));
//		init_price_vec(prices, num_assets);
	}

	std::atomic<bool> cancel_timeout_thread = false;
	bool timeout_flag = false;

	std::thread th([this, &cancel_timeout_thread, &timeout_flag] () {
		timeout_thread(5, cancel_timeout_thread, timeout_flag);
	});

	auto res = tatonnement.oracle.compute_prices_grid_search(prices.data(), current_approx_params);

	cancel_timeout_thread = true;

	th.join();

	auto lp_results = tatonnement.lp_solver.solve(prices.data(), current_approx_params, !timeout_flag);

	if (!timeout_flag) {
		std::printf("time per thread (micros): %lf\n", res.runtime * 1'000'000.0 / (res.num_rounds * 1.0));
	}

	auto feasible_first = management_structures.work_unit_manager.get_max_feasible_smooth_mult(lp_results, prices.data());
	std::printf("feasible smooth mult:%u\n", feasible_first);
	res.achieved_smooth_mult = feasible_first;
	res.achieved_fee_rate = lp_results.tax_rate;

/*	uint16_t volumes[num_assets];

	std::printf("finished lp solving\n");

	WorkUnitManagerUtils::get_relative_volumes(lp_results, prices.data(), num_assets, volumes);

//	volumes[0] = 1;

	for (size_t i = 0; i < num_assets; i++) {
		std::printf("%lu %u\n", i, volumes[i]);
	}
	
	tatonnement.oracle.wait_for_all_tatonnement_threads();

	//rerun with normalizers
	cancel_timeout_thread = false;
	timeout_flag = false;
	th = std::thread(
		[this, &cancel_timeout_thread, &timeout_flag] () {
			timeout_thread(5, cancel_timeout_thread, timeout_flag);
		});

	init_price_vec(prices.data(), num_assets);

	auto res2 = tatonnement.oracle.compute_prices_grid_search(prices.data(), current_approx_params, volumes);

	cancel_timeout_thread = true;
	th.join();

	std::printf("second timeout flag: %u\n", timeout_flag);
	auto lp_res_2 = tatonnement.lp_solver.solve(prices.data(), current_approx_params, !timeout_flag);

	auto feasible_second = management_structures.work_unit_manager.get_max_feasible_smooth_mult(lp_res_2, prices.data());

	std::printf("feasible smooth mult 2: %u\n", feasible_second);

	*/
	tatonnement.oracle.wait_for_all_tatonnement_threads();

	if (!timeout_flag) {
		return res;
	} else {
		std::printf("Trial finished via timeout, not success\n");
	}
	return std::nullopt;
}

} /* edce */
