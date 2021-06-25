#pragma once

#include "xdr/types.h"

namespace edce {
class NormalUnderlyingPricesGenerator {
	float min_ratio, max_ratio;
	int min_endowment, max_endowment;
public:
	NormalUnderlyingPricesGenerator (int num_assets, float min_ratio, float max_ratio, int min_endowment, int max_endowment) :
		TransactionGenerator(num_assets), min_ratio(min_ratio), max_ratio(max_ratio), min_endowment(min_endowment), max_endowment(max_endowment)
		{};
	std::vector<SellOrder> get_transaction_block(int num_txs);
};
}