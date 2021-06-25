#include <cxxtest/TestSuite.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "merkle_work_unit_manager.h"
#include "database.h"

#include "xdr/transaction.h"

#include "simple_debug.h"

#include "price_utils.h"
#include "work_unit_manager_utils.h"
#include "tx_type_utils.h"
#include "fixed_point_value.h"
#include "approximation_parameters.h"

#include "lp_solver.h"

using namespace edce;

class GlpkSolverTestSuite : public CxxTest::TestSuite {
public:

	void test_simple_twoasset() {
		TEST_START();

		uint8_t tax_rate = 1;
		uint8_t smooth_mult = 1;

		ApproximationParameters approx_params;
		approx_params.tax_rate = tax_rate;
		approx_params.smooth_mult = smooth_mult;

		MemoryDatabase db;
		MerkleWorkUnitManager manager(2);

		auto idx = db.add_account_to_db(1);
		auto idx2 = db.add_account_to_db(2);
		db.commit(0);

		db.transfer_available(idx, 0, 1000);
		db.transfer_available(idx2, 1, 1000);

		db.commit(0);

		int x = 0;

		ProcessingSerialManager serial_manager(manager);

		Offer offer;
		offer.category = TxTypeUtils::make_category(0, 1, OfferType::SELL);
		offer.offerId = 1000;
		offer.owner = 1;
		offer.amount = 100;
		offer.minPrice = PriceUtils::from_double(0.9);

		auto offer_idx = manager.look_up_idx(offer.category);
		serial_manager.add_offer(offer_idx, offer, x, x);

		offer.category = TxTypeUtils::make_category(1, 0, OfferType::SELL);
		offer.offerId = 2000;
		offer.owner = 2;
		offer.amount = 100;
		offer.minPrice = PriceUtils::from_double(0.9);

		offer_idx = manager.look_up_idx(offer.category);
		serial_manager.add_offer(offer_idx, offer, x, x);

		serial_manager.finish_merge();
		manager.commit_for_production(1);
		db.commit(0);

		//sanity checks

		auto& work_units = manager.get_work_units();

		Price prices[2];
		prices[0] = PriceUtils::from_double(1);
		prices[1] = PriceUtils::from_double(1);

		auto bds = work_units[0].get_supply_bounds(prices, approx_params.smooth_mult);
		TS_ASSERT_EQUALS(bds.first, 0);
		TS_ASSERT_EQUALS(bds.second, 100);

		bds = work_units[1].get_supply_bounds(prices, approx_params.smooth_mult);
		TS_ASSERT_EQUALS(bds.first, 0);
		TS_ASSERT_EQUALS(bds.second, 100);

		LPSolver solver(manager);

		auto results = solver.solve(prices, approx_params);

		TS_ASSERT_EQUALS(results.tax_rate, tax_rate);
		auto work_unit_results = results.work_unit_params;

		TS_ASSERT_EQUALS(work_unit_results[0].supply_activated.floor(), 100);
		TS_ASSERT_EQUALS(work_unit_results[1].supply_activated.floor(), 100);
	}

	void test_twoasset_asymmetric() {
		TEST_START();

		uint8_t tax_rate = 1;
		uint8_t smooth_mult = 1;

		ApproximationParameters approx_params;
		approx_params.tax_rate = tax_rate;
		approx_params.smooth_mult = smooth_mult;

		MemoryDatabase db;
		MerkleWorkUnitManager manager(2);

		auto idx = db.add_account_to_db(1);
		auto idx2 = db.add_account_to_db(2);
		db.commit(0);

		int x = 0;

		db.transfer_available(idx, 0, 1000);
		db.transfer_available(idx2, 1, 1000);

		db.commit(0);

		ProcessingSerialManager serial_manager(manager);

		Offer offer;
		offer.category = TxTypeUtils::make_category(0, 1, OfferType::SELL);
		offer.offerId = 1000;
		offer.owner = 1;
		offer.amount = 100;
		offer.minPrice = PriceUtils::from_double(0.75);

		auto offer_idx = manager.look_up_idx(offer.category);
		serial_manager.add_offer(offer_idx, offer, x, x);

		offer.category = TxTypeUtils::make_category(1, 0, OfferType::SELL);
		offer.offerId = 2000;
		offer.owner = 2;
		offer.amount = 1000;
		offer.minPrice = PriceUtils::from_double(0.75);

		offer_idx = manager.look_up_idx(offer.category);
		serial_manager.add_offer(offer_idx, offer, x, x);

		serial_manager.finish_merge();
		manager.commit_for_production(1);
		db.commit(0);

		//sanity checks

		auto& work_units = manager.get_work_units();

		Price prices[2];
		prices[0] = PriceUtils::from_double(1);
		prices[1] = PriceUtils::from_double(1);

		auto bds = work_units[0].get_supply_bounds(prices, approx_params.smooth_mult);
		TS_ASSERT_EQUALS(bds.first, 0);
		TS_ASSERT_EQUALS(bds.second, 100);

		bds = work_units[1].get_supply_bounds(prices, approx_params.smooth_mult);
		TS_ASSERT_EQUALS(bds.first, 0);
		TS_ASSERT_EQUALS(bds.second, 1000);

		LPSolver solver(manager);

		auto results = solver.solve(prices, approx_params);

		TS_ASSERT_EQUALS(results.tax_rate, tax_rate);
		auto work_unit_results = results.work_unit_params;

		TS_ASSERT_EQUALS(work_unit_results[0].supply_activated.floor(), 100);
		TS_ASSERT_EQUALS(work_unit_results[1].supply_activated.floor(), 200); //because of the 50% tax rate
	}


	void test_tax_calculation() {
		TEST_START();
		uint8_t tax_rate = 5;
		uint8_t smooth_mult = 1;
		
		ApproximationParameters approx_params;
		approx_params.tax_rate = tax_rate;
		approx_params.smooth_mult = smooth_mult;
		//MemoryDatabase db;
		MerkleWorkUnitManager manager(2);

		LPSolver solver(manager);

		FractionalAsset supply(64);
		FractionalAsset demand(66);
		auto new_tax_rate = solver.max_tax_param(supply, demand, approx_params.tax_rate);

		TS_ASSERT_EQUALS(5, new_tax_rate);

		demand = FractionalAsset(68);
		new_tax_rate = solver.max_tax_param(supply, demand, approx_params.tax_rate);
		TS_ASSERT_EQUALS(4, new_tax_rate);

	}
};
