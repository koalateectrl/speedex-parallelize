#include "convex_oracle.h"
#include "edce_management_structures.h"

#include "tx_type_utils.h"

#include <vector>

#include "xdr/types.h"
#include "xdr/transaction.h"

using namespace edce;

void make_basic_manager(MerkleWorkUnitManager& manager) {

	int x = 0;

	ProcessingSerialManager serial_manager(manager);
	for (int i = 1; i <= 1; i++) {

		Offer offer;
		offer.category = TxTypeUtils::make_category(0, 1, OfferType::SELL);
		offer.offerId = i;
		offer.owner = 1;
		offer.amount = 100;
		offer.minPrice = PriceUtils::from_double(i/3.0);

		auto offer_idx = manager.look_up_idx(offer.category);
		serial_manager.add_offer(offer_idx, offer, x, x);
	}
	for (int i = 1; i <= 1; i++) {

		Offer offer;
		offer.category = TxTypeUtils::make_category(1, 0, OfferType::SELL);
		offer.offerId = i;
		offer.owner = 1;
		offer.amount = 100;
		offer.minPrice = PriceUtils::from_double(i/3.0);

		auto offer_idx = manager.look_up_idx(offer.category);
		serial_manager.add_offer(offer_idx, offer, x, x);
	}

	serial_manager.finish_merge();

	manager.commit_for_production(1);
}


int main(int argc, char const *argv[])
{
	EdceManagementStructures management_structures {
		MemoryDatabase(),
		MerkleWorkUnitManager(
			5, //smooth_mult
			5, //tax_rate
			2) // num_assets
	};

	auto& manager = management_structures.work_unit_manager;

	ConvexOracle oracle(management_structures);

	std::vector<double> prices;
	prices.resize(2, 1.0);

	make_basic_manager(manager);

	oracle.solve_f(prices);
}