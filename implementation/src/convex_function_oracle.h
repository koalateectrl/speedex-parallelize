#pragma once

#include "merkle_work_unit_manager.h"
#include "gsl_utils.h"
#include "approximation_parameters.h"


#include <cstdint>
#include <cstddef>
#include <vector>

#include <gsl/gsl_vector.h>

namespace edce {

/*
Empty work units are safely ignored in calculations. 

What is not ignored is if one asset has 0 value change hands at equilibrium (i.e. none on offer)
*/
class ConvexFunctionOracle {

	MerkleWorkUnitManager& manager;

	constexpr static double starting_barrier = 1;
	constexpr static double small_infinity = 1e200;

	double barrier = starting_barrier;

	double
	one_offer_barrier_f(
		double y, double beta, double inverse_utility, double sell_price, double buy_price, double endow);

	double 
	one_work_unit_f(
		const gsl_vector *price_vars, 
		const gsl_vector* y_vars, 
		const gsl_vector *beta_vars, 
		const MerkleWorkUnit& work_unit, 
		std::vector<double>& supply, 
		std::vector<double>& demand);

	bool
	one_work_unit_satisfiable(
		const gsl_vector *price_vars, 
		const gsl_vector *y_vars, 
		const gsl_vector *beta_vars, 
		const MerkleWorkUnit& work_unit, 
		std::vector<double>& supply,
		std::vector<double>& demand);

	void
	one_work_unit_set_feasible(
		const gsl_vector* price_vars, 
		const MerkleWorkUnit& work_unit, 
		gsl_vector * y_vars, 
		gsl_vector * beta_vars);



	gsl_vector_view
	get_price_view(gsl_vector* vars);
	gsl_vector_const_view
	get_price_const_view(const gsl_vector* vars);
	
	gsl_vector_view
	get_y_vars_view(gsl_vector* vars, size_t offset, size_t index_size);
	gsl_vector_const_view
	get_y_vars_const_view(const gsl_vector* vars, size_t offset, size_t index_size);
	
	gsl_vector_view
	get_beta_vars_view(gsl_vector* vars, size_t offset, size_t index_size);
	gsl_vector_const_view
	get_beta_vars_const_view(const gsl_vector* vars, size_t offset, size_t index_size);

	void rescale_prices(const std::vector<double>& starting_prices, gsl_vector* price_vars);


public:

	ConvexFunctionOracle(MerkleWorkUnitManager& manager) : manager(manager) {}

	void boost_barrier(double factor = 2.0) {
		barrier *= factor;
	}

	void reset_barrier() {
		barrier = starting_barrier;
	}

	double eval_f(const gsl_vector* vars);

	static double eval_f_static(const gsl_vector* vars, void* params) {
		ConvexFunctionOracle* instance = static_cast<ConvexFunctionOracle*>(params);
		return instance -> eval_f(vars);
	}

	bool check_satisfiable(const gsl_vector* vars, const ApproximationParameters approx_params);

	void set_feasible_starting_point(const std::vector<double>& starting_prices, UniqueGslVec& vars);


};


} /* namespace edce */