#pragma once

#include "edce_management_structures.h"

#include "convex_function_oracle.h"


namespace edce {

class ConvexOracle {

	EdceManagementStructures& management_structures;
	ConvexFunctionOracle function_oracle;


	void rescale_prices(std::vector<double>& prices);

public:

	ConvexOracle(EdceManagementStructures& management_structures)
		: management_structures(management_structures)
		, function_oracle(management_structures.work_unit_manager) {}

	void solve_f(std::vector<double>& prices);

};


} /* namespace edce */