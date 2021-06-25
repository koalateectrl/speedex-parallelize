#pragma once

#include <vector>
#include <cstdint>
#include "xdr/types.h"

namespace edce {

class SimpleSyntheticDataGenerator {
	int num_assets;
public:
	SimpleSyntheticDataGenerator(int num_assets) : num_assets(num_assets) {}

	std::vector<Offer> normal_underlying_prices_sell(int num_txs, 
						std::vector<Price>& prices, 
						uint32_t min_endowment, 
						uint32_t max_endowment,
						double min_ratio,
						double max_ratio);
};
}
