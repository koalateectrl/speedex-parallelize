#pragma once

#include <cstdint>
#include <cmath>

namespace edce {

class NormalizationRollingAverage {

	const size_t num_assets;


	double* rolling_averages;

	constexpr static double keep_amt = 1.0/2.0; // 2/3 is good too, possibly
	constexpr static double new_amt = 1.0 - keep_amt;

	constexpr static uint16_t RELATIVE_VOLUME_BASEPT = WorkUnitManagerUtils::RELATIVE_VOLUME_BASEPT;

	void update_formatted_avgs() {
		for (size_t i = 0; i < num_assets; i++) {
			formatted_rolling_avgs[i] = RELATIVE_VOLUME_BASEPT * rolling_averages[i];
			if (formatted_rolling_avgs[i] == 0) {
				formatted_rolling_avgs[i] = 1;
			}
			if (rolling_averages[i] >= UINT16_MAX / RELATIVE_VOLUME_BASEPT) {
				formatted_rolling_avgs[i] = UINT16_MAX;
			}
		}
	}

public:
	uint16_t* formatted_rolling_avgs;
	NormalizationRollingAverage(size_t num_assets)
		: num_assets(num_assets) {
			rolling_averages = new double[num_assets];
			formatted_rolling_avgs = new uint16_t[num_assets];
			for (size_t i = 0; i < num_assets; i++) {
				rolling_averages[i] = 1.0;
			}
		}
	~NormalizationRollingAverage() {
		delete[] formatted_rolling_avgs;
		delete[] rolling_averages;
	}



	void add_to_average(uint16_t* current_normalizers) {
		for (size_t i = 0; i < num_assets; i++) {
			double current_factor = ((double)current_normalizers[i]) / RELATIVE_VOLUME_BASEPT;
			rolling_averages[i] = std::pow(rolling_averages[i], keep_amt) * std::pow(current_factor, new_amt);
		}
		update_formatted_avgs();
	}
};

	
} /* edce */

