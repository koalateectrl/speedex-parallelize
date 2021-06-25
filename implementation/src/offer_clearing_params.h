#pragma once
#include "fixed_point_value.h"

namespace edce {

struct WorkUnitClearingParams {
	//how much of the available supply in a work unit is activated.
	FractionalAsset supply_activated;
};

struct ClearingParams {
	uint8_t tax_rate;
	std::vector<WorkUnitClearingParams> work_unit_params;
};

}