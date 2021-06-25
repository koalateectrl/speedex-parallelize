#include "convex_function_oracle.h"

#include "merkle_work_unit.h"

#include <cmath>

namespace edce {

double
ConvexFunctionOracle::one_offer_barrier_f(
	double y, double beta, double inverse_utility, double sell_price, double buy_price, double endow) {

	double barrier_out = 0.0;

	// beta <= sell_price
	if (beta < sell_price) {
		barrier_out += (-1.0/barrier) * std::log2l(sell_price - beta);
	} else {
		std::printf("beta > sell_price\n");
		return small_infinity;
	}
	
	// beta * utility <= buy_price
	if (beta / inverse_utility < buy_price) {
		barrier_out += (-1.0/barrier) * std::log2l(buy_price - beta/inverse_utility);
	} else {
		std::printf("beta * utility > buy_price\n");
		return small_infinity;
	}

	// y >= 0
	if (y > 0) {
		barrier_out += (-1.0/barrier) * std::log2l(y);
	} else {
		std::printf("y < 0\n");
		return small_infinity;
	}

	// y <= sell_price * endow
	if (y < sell_price * endow) {
		barrier_out += (-1.0/barrier) * std::log2l(sell_price * endow - y);
	} else {
		std::printf("y > sell_price * endow\n");
		return small_infinity;
	}

	// beta > 0
	if (beta > 0) {
		barrier_out += (-1.0/barrier) * std::log2l(beta);
	} else {
		std::printf("beta < 0\n");
		return small_infinity;
	}
	return barrier_out;
}

double 
ConvexFunctionOracle::one_work_unit_f(
	const gsl_vector *price_vars, 
	const gsl_vector* y_vars, 
	const gsl_vector *beta_vars, 
	const MerkleWorkUnit& work_unit, 
	std::vector<double>& supply,
	std::vector<double>& demand) {

	auto& indexed_metadata = work_unit.get_indexed_metadata();

	double objective = 0;

	auto sell_asset = work_unit.get_category().sellAsset;
	auto buy_asset = work_unit.get_category().buyAsset;

	double sell_price = gsl_vector_get(price_vars, sell_asset);
	double buy_price = gsl_vector_get(price_vars, buy_asset);

	double barrier_objective = 0;

	for (size_t i = 1; i < indexed_metadata.size(); i++) {
		double y = gsl_vector_get(y_vars, i-1);
		double beta = gsl_vector_get(beta_vars, i-1);
		double inverse_utility = PriceUtils::to_double(indexed_metadata[i].key);
		double endow = indexed_metadata[i].metadata.endow - indexed_metadata[i-1].metadata.endow;

		std::printf("y=%lf beta = %lf inverse_utility = %lf sell=%lf buy=%lf\n", y, beta, inverse_utility, sell_price, buy_price);
		double obj_delta = sell_price * endow * std::log2l(sell_price / beta) + y * std::log2l(inverse_utility);
		std::printf("obj_delta=%lf\n", obj_delta);
		objective += obj_delta;
		//std::printf("i=%lu objective = %lf\n", i, objective);

		barrier_objective += one_offer_barrier_f(y, beta, inverse_utility, sell_price, buy_price, endow);

		//std::printf("post barrier:%lf\n", barrier_objective);
		supply[sell_asset] += y;
		demand[buy_asset] += y * sell_price / buy_price;
	}

	return objective + barrier_objective;
}

bool
ConvexFunctionOracle::one_work_unit_satisfiable(
	const gsl_vector *price_vars, 
	const gsl_vector *y_vars, 
	const gsl_vector *beta_vars, 
	const MerkleWorkUnit& work_unit, 
	std::vector<double>& supply,
	std::vector<double>& demand) {

	auto sell_asset = work_unit.get_category().sellAsset;
	auto buy_asset = work_unit.get_category().buyAsset;

	double sell_price = gsl_vector_get(price_vars, sell_asset);
	double buy_price = gsl_vector_get(price_vars, buy_asset);

	size_t num_y_vars = y_vars->size;

	double supply_activated = 0;
	for (size_t i = 0; i < num_y_vars; i++) {
		supply_activated += gsl_vector_get(y_vars, i);
	}

	auto [lower_supply_bound, upper_supply_bound] = work_unit.get_supply_bounds(sell_price, buy_price);

	if (supply_activated < lower_supply_bound || supply_activated > upper_supply_bound) {
		return false;
	}

	supply[sell_asset] += supply_activated;

	demand[sell_asset] += supply_activated * sell_price / buy_price;

	return true;
}

gsl_vector_view
ConvexFunctionOracle::get_price_view(gsl_vector* vars) {
	return gsl_vector_subvector(vars, 0, manager.get_num_assets());
}
gsl_vector_const_view
ConvexFunctionOracle::get_price_const_view(const gsl_vector* vars) {
	return gsl_vector_const_subvector(vars, 0, manager.get_num_assets());
}

gsl_vector_view
ConvexFunctionOracle::get_y_vars_view(gsl_vector* vars, size_t offset, size_t index_size) {
	return gsl_vector_subvector(vars, offset, index_size);
}
gsl_vector_const_view
ConvexFunctionOracle::get_y_vars_const_view(const gsl_vector* vars, size_t offset, size_t index_size) {
	return gsl_vector_const_subvector(vars, offset, index_size);
}

gsl_vector_view
ConvexFunctionOracle::get_beta_vars_view(gsl_vector* vars, size_t offset, size_t index_size) {
	return gsl_vector_subvector(vars, offset, index_size);
}
gsl_vector_const_view
ConvexFunctionOracle::get_beta_vars_const_view(const gsl_vector* vars, size_t offset, size_t index_size) {
	return gsl_vector_const_subvector(vars, offset, index_size);
}

double 
tax_to_double(int tax_rate) {
	return 1.0 - std::exp2l(-tax_rate);
}

bool
ConvexFunctionOracle::check_satisfiable(const gsl_vector* vars, const ApproximationParameters approx_params) {
	auto& work_units = manager.get_work_units();

	auto price_view = get_price_const_view(vars);

	size_t offset = manager.get_num_assets(); // prices come first

	std::vector<double> supply, demand;
	supply.resize(manager.get_num_assets(), 0.0);
	demand.resize(manager.get_num_assets(), 0.0);

	for (auto& work_unit : work_units) {
		auto index_size = work_unit.get_index_nnz();
		auto y_vars_view = get_y_vars_const_view(vars, offset, index_size);
		offset += index_size;
		auto beta_vars_view = get_beta_vars_const_view(vars, offset, index_size);
		offset += index_size;

		if (!one_work_unit_satisfiable(&price_view.vector, &y_vars_view.vector, &beta_vars_view.vector, work_unit, supply, demand)) {
			return false;
		}
	}

	for (size_t i = 0; i < manager.get_num_assets(); i++) {
		double supply_delta = supply[i] - (demand[i] * tax_to_double(approx_params.tax_rate));

		if (supply_delta < 0) {
			return false;
		}
	}
	return true;
}

double
ConvexFunctionOracle::eval_f(const gsl_vector* vars) {

	std::printf("starting eval_f\n");
	auto& work_units = manager.get_work_units();

	auto price_view = get_price_const_view(vars);

	size_t offset = manager.get_num_assets(); // prices come first

	double objective = 0.0;

	for (size_t i = 0; i < vars->size; i++) {
		std::printf("%lf ", gsl_vector_get(vars, i));
	}
	std::printf("\n");

	std::vector<double> supply, demand;
	supply.resize(manager.get_num_assets(), 0.0);
	demand.resize(manager.get_num_assets(), 0.0);

	for (auto& work_unit : work_units) {

		//std::printf("starting work unit, obj = %lf\n", objective);
		auto index_size = work_unit.get_index_nnz();
		auto y_vars_view = get_y_vars_const_view(vars, offset, index_size);
		offset += index_size;
		auto beta_vars_view = get_beta_vars_const_view(vars, offset, index_size);
		offset += index_size;

		/*for (size_t i = 0; i < (&y_vars_view.vector)->size; i++) {
			std::printf("%lf ", gsl_vector_get(&y_vars_view.vector, i));
		}
		std::printf("\n");
		for (size_t i = 0; i < (&beta_vars_view.vector)->size; i++) {
			std::printf("%lf ", gsl_vector_get(&beta_vars_view.vector, i));
		}
		std::printf("\n");
		*/
		objective += one_work_unit_f(&price_view.vector, &y_vars_view.vector, &beta_vars_view.vector, work_unit, supply, demand);

	}

	for (size_t i = 0; i < manager.get_num_assets(); i++) {
		double supply_delta = supply[i] - (demand[i] * tax_to_double(approx_params.tax_rate));

		if (supply_delta < 0) {
			objective += gsl_vector_get(&price_view.vector, i) * supply_delta * supply_delta;
		}

//		if (supply_delta > 0) {
//			objective += (-1.0/barrier) * std::log2l(supply_delta);
//		} else {
//			std::printf("what the fuck %lu %lf\n", i, supply_delta);
//			return small_infinity;
//		}
	}
	std::printf("post: obj = %lf\n", objective);
	return objective;
}

void ConvexFunctionOracle::set_feasible_starting_point(const std::vector<double>& starting_prices, UniqueGslVec& vars) {
	auto price_view = get_price_view(vars.vec);
	rescale_prices(starting_prices, &price_view.vector);

	auto price_const_view = get_price_const_view(vars.vec);

	size_t offset = manager.get_num_assets();

	auto& work_units = manager.get_work_units();

	for (auto& work_unit : work_units) {
		auto index_size = work_unit.get_index_nnz();
		auto y_vars_view = get_y_vars_view(vars.vec, offset, index_size);
		offset += index_size;
		auto beta_vars_view = get_beta_vars_view(vars.vec, offset, index_size);
		offset += index_size;

		one_work_unit_set_feasible(&price_const_view.vector, work_unit, &y_vars_view.vector, &beta_vars_view.vector);
	}
}


void 
ConvexFunctionOracle::rescale_prices(const std::vector<double>& starting_prices, gsl_vector* price_vars) {

	// ensure every price is greater than 1, without changing relative values
	double min_price = 1.0;
	for (auto price : starting_prices) {
		min_price = std::min(min_price, price);
	}

	if (min_price < 0) {
		ERROR("minimum price was less than 0");
		return;
	}

	for (size_t i = 0; i < starting_prices.size(); i++) {
		gsl_vector_set(price_vars, i, 2*starting_prices[i]/min_price);
		//prices[i] /= min_price;
		//prices[i] *= 2;
	}
}

void
ConvexFunctionOracle::one_work_unit_set_feasible(const gsl_vector* price_vars, const MerkleWorkUnit& work_unit, gsl_vector * y_vars, gsl_vector * beta_vars) {
	auto sell_asset = work_unit.get_category().sellAsset;
	auto buy_asset = work_unit.get_category().buyAsset;

	double sell_price = gsl_vector_get(price_vars, sell_asset);
	double buy_price = gsl_vector_get(price_vars, buy_asset);

	size_t num_y_vars = y_vars->size;

	//assumption is that every offer will have at least 1 unit available on offer, and that the work unit is not empty.
	double total_y_outflow = 1.0/sell_price;

	if (num_y_vars == 0) {
		//work unit empty, so continue to next work unit.
		return;
	}

	gsl_vector_set_all(y_vars, total_y_outflow / ((double) num_y_vars));

	//Need beta * utility < buy_price for all offers.
	// means beta  max(utility) < buy_price
	// min utility is 1/min(inverse utility) = 1/min (minprice)

	auto& indexed_metadata = work_unit.get_indexed_metadata();

	double min_inverse_utility = PriceUtils::to_double(indexed_metadata[1].key);

	double beta_default = std::min(sell_price, buy_price * min_inverse_utility) / 2.0;

	gsl_vector_set_all(beta_vars, beta_default);
}


} /* edce */