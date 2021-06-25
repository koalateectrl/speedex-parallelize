#include <cxxtest/TestSuite.h>

#include "merkle_work_unit.h"
#include "merkle_work_unit_manager.h"

#include "tx_type_utils.h"

#include "tests/block_processor_test_utils.h"

#include "price_utils.h"

#include "xdr/types.h"

#include "approximation_parameters.h"

#include <cstdint>

#include "simple_debug.h"

using namespace edce;

class WorkUnitTestSuite : public CxxTest::TestSuite {

	void make_basic_db(MemoryDatabase& db, MerkleWorkUnitManager& manager) {
		auto idx = db.add_account_to_db(1);
		db.commit(0);

		db.transfer_available(idx, 0, 1000);

		db.commit(0); // db stuff isn't needed w/o serialtxprocessor

		int x = 0;

		ProcessingSerialManager serial_manager(manager);
		for (int i = 1; i <= 10; i++) {

			Offer offer;
			offer.category = TxTypeUtils::make_category(0, 1, OfferType::SELL);
			offer.offerId = i;
			offer.owner = 1;
			offer.amount = 100;
			offer.minPrice = PriceUtils::from_double(i);

			auto offer_idx = manager.look_up_idx(offer.category);
			serial_manager.add_offer(offer_idx, offer, x, x);
		}
		serial_manager.finish_merge();

		manager.commit_for_production(1);
	}

public:

	void test_basic_supply_demand() {
		TEST_START();
		MemoryDatabase db;
		MerkleWorkUnitManager manager(2);

		ApproximationParameters approx_params{0, 0};

		make_basic_db(db, manager);

		uint128_t supplies[2];
		uint128_t demands[2];
		Price prices[2];

		for (int i = 0; i < 2; i++) {
			supplies[i] = 0;
			demands[i] = 0;
		}

		prices[1] = 100;
		prices[0] = 500;

		auto unit_idx = manager.look_up_idx(TxTypeUtils::make_category(0, 1, OfferType::SELL));
		auto& work_units = manager.get_work_units();

		work_units[unit_idx].calculate_demands_and_supplies(prices, demands, supplies, approx_params.smooth_mult);

		TS_ASSERT_EQUALS(supplies[0]>>PriceUtils::PRICE_RADIX, 500);
		TS_ASSERT_EQUALS(demands[1]>>PriceUtils::PRICE_RADIX, 2500);

		supplies[0] = 0;
		demands[1] = 0;

		prices[0] = 450;

		work_units[unit_idx].calculate_demands_and_supplies(prices, demands, supplies, approx_params.smooth_mult);

		TS_ASSERT_EQUALS(supplies[0]>>PriceUtils::PRICE_RADIX, 400);
		TS_ASSERT_EQUALS(demands[1]>>PriceUtils::PRICE_RADIX, 1800);

		supplies[0] = 0;
		demands[1] = 0;

		prices[0] = 80;
		
		work_units[unit_idx].calculate_demands_and_supplies(prices, demands, supplies, approx_params.smooth_mult);

		TS_ASSERT_EQUALS(supplies[0]>>PriceUtils::PRICE_RADIX, 0);
		TS_ASSERT_EQUALS(demands[1]>>PriceUtils::PRICE_RADIX, 0);

		supplies[0] = 0;
		demands[1] = 0;

		prices[0] = 1200;
		
		work_units[unit_idx].calculate_demands_and_supplies(prices, demands, supplies, approx_params.smooth_mult);

		TS_ASSERT_EQUALS(supplies[0]>>PriceUtils::PRICE_RADIX, 1000);
		TS_ASSERT_EQUALS(demands[1]>>PriceUtils::PRICE_RADIX, 12000);
	}

	void test_smooth_mult() {
		TEST_START();
		MemoryDatabase db;
		MerkleWorkUnitManager manager(2);
		
		ApproximationParameters approx_params{0, 2};
		
		make_basic_db(db, manager);

		uint128_t supplies[2];
		uint128_t demands[2];
		Price prices[2];

		for (int i = 0; i < 2; i++) {
			supplies[i] = 0;
			demands[i] = 0;
		}

		prices[1] = 100;
		prices[0] = 800;

		auto unit_idx = manager.look_up_idx(TxTypeUtils::make_category(0, 1, OfferType::SELL));
		auto& work_units = manager.get_work_units();

		work_units[unit_idx].calculate_demands_and_supplies(prices, demands, supplies, approx_params.smooth_mult);

		TS_ASSERT_EQUALS(supplies[0]>>PriceUtils::PRICE_RADIX, 650);
		TS_ASSERT_EQUALS(demands[1]>>PriceUtils::PRICE_RADIX, 5200);
	}

	void test_max_feasible_smooth_mult() {
		TEST_START();

		MemoryDatabase db;
		MerkleWorkUnitManager manager(2);

		make_basic_db(db, manager);
		
		//ApproximationParameters approx_params{0, 2};

		Price prices[2];

		prices[1] = 100;
		prices[0] = 800;

		auto& work_units = manager.get_work_units();

		TS_ASSERT_EQUALS(work_units[0].max_feasible_smooth_mult(800, prices), 255);
		TS_ASSERT_EQUALS(work_units[0].max_feasible_smooth_mult(701, prices), 255);
		TS_ASSERT_EQUALS(work_units[0].max_feasible_smooth_mult(700, prices), 255);
		TS_ASSERT_EQUALS(work_units[0].max_feasible_smooth_mult(699, prices), 3);

	}

};