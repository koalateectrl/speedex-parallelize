#include <cxxtest/TestSuite.h>

#include <cstdint>

#include "xdr/types.h"

#include "atomic_offer.h"

using namespace edce;

class AtomicOfferTest : public CxxTest::TestSuite {


	Offer make_offer(
		uint64_t offer_id, 
		AccountID owner,
		uint64_t amount) {
		Offer offer;
		offer.category.sellAsset = 0;
		offer.category.buyAsset = 1;
		offer.category.type = OfferType::SELL;
		offer.offerId = offer_id;
		offer.owner = owner;
		offer.amount = amount;
		offer.minPrice = 1;
		return offer;
	}

public:
	void test_offer() {
		AtomicOffer a_offer(make_offer(1, 0, 10));
		int64_t escrow_change;

		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			a_offer.process_modify(10, make_offer(1, 0, 20), &escrow_change));

		TS_ASSERT_EQUALS(10, escrow_change);

		a_offer.commit_modify(10, make_offer(1, 0, 20), &escrow_change);

		TS_ASSERT_EQUALS(0, escrow_change);


		//earlier seq id reducing escrow test
		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			a_offer.process_modify(8, make_offer(1, 0, 10), &escrow_change));

		TS_ASSERT_EQUALS(0, escrow_change);

		a_offer.commit_modify(8, make_offer(1, 0, 10), &escrow_change);

		TS_ASSERT_EQUALS(0, escrow_change);


		//earlier seq id increasing escrow test
		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			a_offer.process_modify(9, make_offer(1, 0, 30), &escrow_change));

		int64_t escrow_change_2;

		a_offer.commit_modify(9, make_offer(1, 0, 30), &escrow_change_2);

		TS_ASSERT_EQUALS(escrow_change_2, escrow_change);
		TS_ASSERT_EQUALS(0, escrow_change);

		a_offer.commit();

		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::INVALID_OPERATION,
			a_offer.process_modify(10, make_offer(1, 0, 10), &escrow_change));

		a_offer.commit();
	}

	void test_reduce_offer_amount() {
		AtomicOffer a_offer(make_offer(1, 0, 10));
		int64_t escrow_change;

		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			a_offer.process_modify(10, make_offer(1, 0, 20), &escrow_change));


		a_offer.commit_modify(10, make_offer(1, 0, 20), &escrow_change);

		a_offer.commit();



		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			a_offer.process_modify(15, make_offer(1, 0, 10), &escrow_change));

		TS_ASSERT_EQUALS(0, escrow_change);

		a_offer.commit_modify(15, make_offer(1, 0, 10), &escrow_change);


		// 10 units released by the modification
		TS_ASSERT_EQUALS(10, escrow_change);
	}

};