#pragma once

#include "merkle_work_unit_manager.h"

#include "xdr/types.h"
#include "price_utils.h"
#include "work_unit_manager_utils.h"
#include "fixed_point_value.h"
#include "offer_clearing_params.h"
#include "approximation_parameters.h"

#include <vector>
#include <glpk.h>
#include <cstdint>
#include <memory>
#include <mutex>

namespace edce {

struct LPInstance {

	int* ia;
	int* ja;
	double* ar;

	glp_prob* lp;
	
	const size_t nnz;

	LPInstance(const size_t nnz) : nnz(nnz) {
		ia = new int[nnz];
		ja = new int[nnz];
		ar = new double[nnz];
		lp = glp_create_prob();
	}

	~LPInstance() {
		delete[] ia;
		delete[] ja;
		delete[] ar;
		glp_delete_prob(lp);
	}

	void clear() {
		glp_erase_prob(lp);
	}
};

struct BoundsInfo {
	std::pair<uint64_t, uint64_t> bounds;
	OfferCategory category;
};

class LPSolver {

	MerkleWorkUnitManager& manager;

	void 
	add_work_unit_range_constraint(
		glp_prob* lp, 
		MerkleWorkUnit& work_unit, 
		int idx, 
		Price* prices, 
		int *ia, int *ja, double *ar, 
		int& next_available_nnz,
		const ApproximationParameters approx_params,
		bool use_lower_bound);

	void
	add_work_unit_range_constraint(
		glp_prob* lp,
		BoundsInfo& bounds_info, 
		int idx, 
		Price* prices, 
		int *ia, int *ja, double *ar, 
		int& next_available_nnz,
		const uint8_t tax_rate,
		bool use_lower_bound);


	size_t get_nnz() {
		return 1 + 2 * manager.get_work_units().size();
	}

	std::mutex mtx;

public:
	LPSolver(MerkleWorkUnitManager& manager) : manager(manager) {}

	ClearingParams solve(Price* prices, const ApproximationParameters approx_params, bool use_lower_bound = true);
	bool check_feasibility(Price* prices, std::unique_ptr<LPInstance>& instance, const ApproximationParameters approx_params);

	std::unique_ptr<LPInstance> make_instance() {
		return std::make_unique<LPInstance>(get_nnz());
	}

	uint8_t max_tax_param(FractionalAsset supply, FractionalAsset demand, const uint8_t target_tax) {
		R_INFO("supply=%f demand=%f", supply.to_double(), demand.to_double());
		if (supply >= demand.tax(target_tax)) {
			return target_tax;//manager.get_tax_rate();
		}

		if (supply >= (demand.tax((target_tax - 1)))) {
			return target_tax - 1;
		}
		


		double eps = std::log2((demand-supply).to_double()) - std::log2(demand.to_double());
		std::printf("%f %f\n", demand.to_double(), supply.to_double());
		std::printf("%f %f %f %f\n", (demand-supply).to_double(), std::log2((demand-supply).to_double()), std::log2(demand.to_double()), eps);
		R_INFO("required tax=%f", eps);
		uint8_t tax_rate = std::floor(-eps);
		if (tax_rate < target_tax - 1) {
			throw std::runtime_error("tax rate increased too much due to LP rounding error");
		}
		/*
		 * Known issue: rounding error when number of offers is very small (i.e. total supply/demand is small) and tax rate is very high (i.e. 2^-22),
		 * rounding from float to fixed point introduces error, which causes this check to fail.  The fix is to use fixed point numbers
		 * with a higher level of precision.
		 */
		return tax_rate;
	}
};

}
